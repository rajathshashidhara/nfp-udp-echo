#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "getopt.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <x86intrin.h>
#include <cpuid.h>

#include "devcfg.h"
#include "config.h"
#include "memzone.h"
#include "driver.h"
#include "io.h"
#include "nfp_cpp.h"
#include "nfp_rtsym.h"

#define SYMBOL_CLS_BUF      "i32.me0._cls_buffer"
#define SYMBOL_CTM_BUF      "i32.me0._ctm_buffer"
#define SYMBOL_IMEM_BUF     "i32.me0._imem_buffer"
#define SYMBOL_EMEM_BUF     "i32.me0._emem_buffer"

#define MAX_COUNT       1000000
#define MAX_CMD_SIZE    1024
#define MAX_THREADS     64

#define DEFAULT_TSC_FREQ    2 * 1000 * 1000 * 1000ull // 2GHz

static uint64_t time_journal[MAX_COUNT];

unsigned start_bw_test = 0;
unsigned end_bw_test = 0;

static uint64_t tsc_frequency;

static unsigned int
rte_cpu_get_model(uint32_t fam_mod_step)
{
	uint32_t family, model, ext_model;

	family = (fam_mod_step >> 8) & 0xf;
	model = (fam_mod_step >> 4) & 0xf;

	if (family == 6 || family == 15) {
		ext_model = (fam_mod_step >> 16) & 0xf;
		model += (ext_model << 4);
	}

	return model;
}

static int32_t
rdmsr(int msr, uint64_t *val)
{
	int fd;
	int ret;

	fd = open("/dev/cpu/0/msr", O_RDONLY);
	if (fd < 0)
		return fd;

	ret = pread(fd, val, sizeof(uint64_t), msr);

	close(fd);

	return ret;
}

static uint32_t
check_model_wsm_nhm(uint8_t model)
{
	switch (model) {
	/* Westmere */
	case 0x25:
	case 0x2C:
	case 0x2F:
	/* Nehalem */
	case 0x1E:
	case 0x1F:
	case 0x1A:
	case 0x2E:
		return 1;
	}

	return 0;
}

static uint32_t
check_model_gdm_dnv(uint8_t model)
{
	switch (model) {
	/* Goldmont */
	case 0x5C:
	/* Denverton */
	case 0x5F:
		return 1;
	}

	return 0;
}

uint64_t
get_tsc_freq_arch(void)
{
	uint64_t tsc_hz = 0;
	uint32_t a, b, c, d, maxleaf;
	uint8_t mult, model;
	int32_t ret;

	/*
	 * Time Stamp Counter and Nominal Core Crystal Clock
	 * Information Leaf
	 */
	maxleaf = __get_cpuid_max(0, NULL);

	if (maxleaf >= 0x15) {
		__cpuid(0x15, a, b, c, d);

		/* EBX : TSC/Crystal ratio, ECX : Crystal Hz */
		if (b && c)
			return c * (b / a);
	}

	__cpuid(0x1, a, b, c, d);
	model = rte_cpu_get_model(a);

	if (check_model_wsm_nhm(model))
		mult = 133;
	else if ((c & bit_AVX) || check_model_gdm_dnv(model))
		mult = 100;
	else
		return DEFAULT_TSC_FREQ;

	ret = rdmsr(0xCE, &tsc_hz);
	if (ret < 0)
		return DEFAULT_TSC_FREQ;

	return ((tsc_hz >> 8) & 0xff) * mult * 1E6;
}

enum mmio_tests
{
    MMIO_LAT_RD,
    MMIO_LAT_WR,
    MMIO_LAT_WRRD,
    MMIO_BW_RD,
    MMIO_BW_WR,
    MMIO_BW_WRRD
};

static int compare(const void* a, const void* b)
{
    uint64_t c = *(uint64_t*)a;
    uint64_t d = *(uint64_t*)b;

    if (c < d) return -1;
    else if (c == d) return 0;
    else return 1;
}

static inline long double time_usec(uint64_t cycles)
{
    return ((long double) cycles * 1E6)/tsc_frequency;
}

void mmio_lat_cmd(uint8_t* buf, size_t buflen, size_t oplen, off_t offset, unsigned count, int test)
{
    // fill with unique pattern
    static uint8_t cmd_buffer[MAX_CMD_SIZE];
    for (unsigned i = 0; i < MAX_CMD_SIZE; i += sizeof(uint32_t))
    {
        *((uint32_t*) (cmd_buffer + i)) = 0xdeadbeef;
    }

    off_t current_offset = offset;
    uint64_t start_time, end_time;
    unsigned index = 0;

    while (index < count)
    {
        switch (test)
        {
        case MMIO_LAT_RD:
            start_time = _rdtsc();

            rte_io_mb();

            memcpy(cmd_buffer, buf + current_offset, oplen);
            // memcpy_fromio_relaxed(cmd_buffer, buf + current_offset, oplen);

            rte_io_mb();

            end_time = _rdtsc();
            break;

        case MMIO_LAT_WR:
            start_time = _rdtsc();

            rte_io_mb();

            memcpy(buf + current_offset, cmd_buffer, oplen);
            // memcpy_toio_relaxed(buf + current_offset, cmd_buffer, oplen);

            rte_io_mb();

            end_time = _rdtsc();
            break;

        case MMIO_LAT_WRRD:
            start_time = _rdtsc();

            rte_io_mb();

            memcpy(cmd_buffer, buf + current_offset, oplen);
            memcpy(buf + current_offset, cmd_buffer, oplen);

            // memcpy_fromio_relaxed(cmd_buffer, buf + current_offset, oplen);
            // memcpy_toio_relaxed(buf + current_offset, cmd_buffer, oplen);
            rte_io_mb();

            end_time = _rdtsc();
            break;

        default:
            fprintf(stderr, "Incorrect latency operation %d\n", test);
            return;
        }

        time_journal[index] = end_time - start_time;
        index++;
        current_offset += oplen;
        if (current_offset + oplen > buflen)
            current_offset = offset;
    }

    long double mean;
    uint64_t sum, median, percentile95, percentile99, min, max;
    qsort(time_journal, count, sizeof(uint64_t), compare);

    sum = 0;
    for (index = 0; index < count; index++)
        sum += time_journal[index];
    mean = (1.0l * sum)/count;
    median = time_journal[count/2];
    percentile95 = time_journal[95*count/100];
    percentile99 = time_journal[99*count/100];
    min = time_journal[0];
    max = time_journal[count - 1];

    printf("Latency Mean=%Lf us Median=%Lf us Min=%Lf us Max=%Lf us Percentile95=%Lf us Percentile99=%Lf us\n",
        time_usec(mean), time_usec(median), time_usec(min), time_usec(max), time_usec(percentile95), time_usec(percentile99));
}

struct bw_worker_args {
    uint8_t* buf;
    size_t buflen;
    size_t oplen;
    off_t offset;
    unsigned count;
    int test;
};
static struct bw_worker_args bw_args;

void* mmio_bw_cmd_worker(void* arg)
{
    uint8_t cmd_buffer[MAX_CMD_SIZE];
    for (unsigned i = 0; i < MAX_CMD_SIZE; i += sizeof(uint32_t))
    {
        *((uint32_t*) (cmd_buffer + i)) = 0xdeadbeef;
    }

    uint8_t* buf = bw_args.buf;
    size_t buflen = bw_args.buflen;
    size_t oplen = bw_args.oplen;
    off_t offset = bw_args.offset;
    unsigned count = bw_args.count;
    int test = bw_args.test;

    off_t current_offset = offset;
    unsigned index = 0;

    while (start_bw_test == 0) {}

    while (index < count)
    {
        switch (test)
        {
        case MMIO_BW_RD:
            memcpy(cmd_buffer, buf + current_offset, oplen);
            // memcpy_fromio_relaxed(cmd_buffer, buf + current_offset, oplen);
            break;

        case MMIO_BW_WR:
            memcpy(buf + current_offset, cmd_buffer, oplen);
            // memcpy_toio_relaxed(buf + current_offset, cmd_buffer, oplen);
            break;

        case MMIO_BW_WRRD:
            memcpy(cmd_buffer, buf + current_offset, oplen);
            memcpy(buf + current_offset, cmd_buffer, oplen);

            // memcpy_fromio_relaxed(cmd_buffer, buf + current_offset, oplen);
            // memcpy_toio_relaxed(buf + current_offset, cmd_buffer, oplen);
            break;

        default:
            fprintf(stderr, "Incorrect bw operation %d\n", test);
            return NULL;
        }

        rte_io_mb();

        index++;
        current_offset += oplen;
        if (current_offset + oplen > buflen)
            current_offset = offset;
    }

    __sync_fetch_and_add(&end_bw_test, 1);

    return NULL;
}

void mmio_bw_cmd(uint8_t* buf, size_t buflen, size_t oplen, off_t offset, unsigned count, unsigned threads, int test)
{
    bw_args.buf = buf;
    bw_args.buflen = buflen;
    bw_args.oplen = oplen;
    bw_args.offset = offset;
    bw_args.count = count;
    bw_args.test = test;

    pthread_t bw_workers[MAX_THREADS];
    for (unsigned i = 0; i < threads; i++)
    {
        pthread_create(&bw_workers[i], NULL, mmio_bw_cmd_worker, NULL);
    }

    uint64_t start_time, end_time;

    start_time = _rdtsc();

    start_bw_test = 1;
    while (end_bw_test != threads) {}

    end_time = _rdtsc();

    for (unsigned i = 0; i < threads; i++)
    {
        pthread_join(bw_workers[i], NULL);
    }

    // TODO: Correct by TSC frequency
    long double bw;
    bw = (count * threads * oplen * 8.0l)/time_usec(end_time - start_time);

    printf("Bandwidth %Lf MBits/sec\n", bw);
}

void usage()
{
    fprintf(stderr, "mmio-perf [OPTIONS]\n");
    fprintf(stderr, "--lat  -L      Latency test (Default)\n");
    fprintf(stderr, "--bw   -B      Bandwidth test\n");
    fprintf(stderr, "--read  -r     Read Operation (Default)\n");
    fprintf(stderr, "--write -w     Write Operation\n");
    fprintf(stderr, "--wrrd  -m     WR-RD Operation\n");
    fprintf(stderr, "--cls  -S      Cluster Local Scratch MMIO Target (Default)\n");
    fprintf(stderr, "--ctm  -C      Cluster Target Memory MMIO Target\n");
    fprintf(stderr, "--imem -I      Internal Memory MMIO Target\n");
    fprintf(stderr, "--emem -E      External Memory MMIO Target\n");
    fprintf(stderr, "--num_threads= -t  Number of Threads in Bandwidth test (Default: 1)\n");
    fprintf(stderr, "--len= -l      Transfer length in bytes (Default: 8)\n");
    fprintf(stderr, "--num_ops= -n  Number of transfer operations (Default: 10^6)\n");
    fprintf(stderr, "--offset= -o   Start offset in the window (Default: 0)\n");
}

static void* memory_map(struct nfp_cpp* cpp, int memory, size_t* len)
{
    struct nfp_rtsym_table* symbol_table = nfp_rtsym_table_read(cpp);
    struct nfp_cpp_area* device_window_area = (struct nfp_cpp_area*) malloc(sizeof(struct nfp_cpp_area));

#define MAP(NAME, SIZE) \
    nfp_rtsym_map(symbol_table, NAME, SIZE, &device_window_area)

    switch (memory) {
        case 0:
            *len = CLS_WINDOW_SIZE;
            return MAP(SYMBOL_CLS_BUF, CLS_WINDOW_SIZE);

        case 1:
            *len = CTM_WINDOW_SIZE;
            return MAP(SYMBOL_CTM_BUF, CTM_WINDOW_SIZE);

        case 2:
            *len = IMEM_WINDOW_SIZE;
            return MAP(SYMBOL_IMEM_BUF, IMEM_WINDOW_SIZE);

        case 3:
            *len = EMEM_WINDOW_SIZE;
            return MAP(SYMBOL_EMEM_BUF, EMEM_WINDOW_SIZE);

        default:
            fprintf(stderr, "Invalid memory location (%d)\n", memory);
            return NULL;
    }

    return NULL;
}

int main(int argc, char* argv[])
{
    struct rte_pci_device* dev;
    int ret;

    memzone_init();

    dev = pci_scan();
    if (!dev)
    {
        fprintf(stderr, "Cannot find Netronome NIC\n");
        return 0;
    }

    struct nfp_cpp* cpp;
    ret = pci_probe(dev, &cpp);
    if (ret)
    {
        fprintf(stderr, "Probe unsuccessful\n");
        return 0;
    }

    tsc_frequency = get_tsc_freq_arch();
    printf("TSC frequency: %lu Hz\n", tsc_frequency);

    /* Parameters */
    int bw = 0;
    int location = 0;
    unsigned num_threads = 1;
    size_t len = 8;
    off_t offset = 0;
    unsigned num_ops = MAX_COUNT;
    int test = 0;
    void* buf = NULL;
    size_t buflen = 0;

    /* Parse args */
    int c;
    int option_index;
    static struct option long_options[] = {
        {"lat", no_argument, NULL, 'L'},
        {"bw", no_argument, NULL, 'B'},
        {"read", no_argument, NULL, 'r'},
        {"write", no_argument, NULL, 'w'},
        {"wrrd", no_argument, NULL, 'm'},
        {"cls", no_argument, NULL, 'S'},
        {"ctm", no_argument, NULL, 'C'},
        {"imem", no_argument, NULL, 'I'},
        {"emem", no_argument, NULL, 'E'},
        {"num_threads", required_argument, NULL, 't'},
        {"len", required_argument, NULL, 'l'},
        {"num_ops", required_argument, NULL, 'n'},
        {"offset", required_argument, NULL, 'o'},
        {0, 0, 0, 0}
    };

    while (1) {
        c = getopt_long(argc, argv, "LBrwmSCIEt:l:n:o:", long_options, &option_index);

        if (c == -1)
            break;

        switch (c) {
        case 0:
            break;

        case 'L':
            bw = 0;
            break;

        case 'B':
            bw = 1;
            break;

        case 'S':
            location = 0;
            break;

        case 'C':
            location = 1;
            break;

        case 'I':
            location = 2;
            break;

        case 'E':
            location = 3;
            break;

        case 't':
            num_threads = (unsigned) atoi(optarg);
            if (num_threads > MAX_THREADS)
            {
                fprintf(stderr, "Too many threads (MAX:%u)\n", MAX_THREADS);
                usage();
                return 0;
            }
            break;

        case 'l':
            len = (size_t) atoll(optarg);
            break;

        case 'n':
            num_ops = (unsigned) atoi(optarg);
            if (num_ops > MAX_COUNT)
            {
                fprintf(stderr, "Number of transfer operations too large (MAX:%u)\n", MAX_COUNT);
                usage();
                return 0;
            }
            break;

        case 'o':
            offset = (off_t) atoll(optarg);
            if (offset > CACHE_LINE_SIZE)
            {
                fprintf(stderr, "Offset greater than CACHE_LINE_SIZE: %u\n", CACHE_LINE_SIZE);
                usage();
                return 0;
            }
            break;

        case 'r':
            if (bw)
                test = MMIO_BW_RD;
            else
                test = MMIO_LAT_RD;
            break;

        case 'w':
            if (bw)
                test = MMIO_BW_WR;
            else
                test = MMIO_LAT_WR;
            break;

        case 'm':
            if (bw)
                test = MMIO_BW_WRRD;
            else
                test = MMIO_LAT_WRRD;
            break;

        case '?':
        default:
            usage();
            return 0;
        }
    }

    buf = memory_map(cpp, location, &buflen);
    if (buf == NULL)
    {
        fprintf(stderr, "Unable to memory map SYMBOLS\n");
        return 0;
    }

    printf("\n");
    if (bw)
    {
        mmio_bw_cmd(buf, buflen, len, offset, num_ops, num_threads, test);
    }
    else
    {
        mmio_lat_cmd(buf, buflen, len, offset, num_ops, test);
    }

    return 0;
}
