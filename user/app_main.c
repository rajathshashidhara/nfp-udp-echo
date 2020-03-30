#include <stdio.h>
#include <pthread.h>

#include <driver.h>
#include <memzone.h>
#include <pthread.h>

#include <nfp_rtsym.h>
#include <nfp_cpp.h>
#include <nfp_net_pmd.h>
#include <rte_byteorder.h>

#define RX_HEAD_SYM "i32._rx_head"
#define RX_TAIL_SYM "i32._rx_tail"
#define TX_HEAD_SYM "i32._tx_head"
#define TX_TAIL_SYM "i32._tx_tail"
#define RX_BUF_START_SYM "i32._rx_buf_start"
#define RX_BUF_LEN_SYM "i32._rx_buf_len"
#define TX_BUF_START_SYM "i32._tx_buf_start"
#define TX_BUF_LEN_SYM "i32._tx_buf_len"

#define START_SYM "i32._start"

#define UDP_PACKET_SIZE 64
#define BUF_SIZE 512

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

extern int nfp_cpp_dev_main(struct rte_pci_device* dev, struct nfp_cpp* cpp);

const struct memzone* mzone_rx;
const struct memzone* mzone_tx;

uint8_t *rx_head_sym, *rx_tail_sym, *tx_head_sym, *tx_tail_sym, *rx_buf_start_sym, *rx_buf_len_sym, *tx_buf_start_sym, *tx_buf_len_sym;
uint8_t *start_sym;

uint64_t rx_head_virt;
uint64_t tx_tail_virt;
uint64_t rx_buf_start, tx_buf_start, rx_buf_len, tx_buf_len;

static inline uint64_t RD(volatile void *addr)
{
	const volatile uint32_t *p = addr;
	uint32_t low, high;

	low = nn_readl((volatile const void *)(p + 1));
	high = nn_readl((volatile const void *)p);

	return low + ((uint64_t)high << 32);
}

static inline void WR(uint64_t val, volatile void *addr)
{
	nn_writel(val, (volatile char *)addr + 4);
	nn_writel(val >> 32, addr);
}

void *buffer_logger( void *ptr ) {
  int fd;
  if ((fd = open("buffer_log_rx", O_CREAT | O_RDWR, 0666)) == -1) {
    perror("open failed");
  }

  if (ftruncate(fd, BUF_SIZE) != 0) {
    perror("util_create_shmsiszed: ftruncate failed");
    return NULL;
  }

  int ret;
  while(1) {
	  sleep(1);
    ret = pwrite(fd, (void*) mzone_rx->addr, BUF_SIZE, 0);
	if(ret < 0) {
		fprintf(stderr, "Failed to log buffer\n");
	}
  }
}

uint64_t copy_rx_tx(uint64_t copy_len) {
    if (RD(tx_tail_sym) < RD(tx_head_sym)) {
      copy_len = MIN(copy_len, RD(tx_head_sym) - RD(tx_tail_sym) - 1);
    } else {
      if (*tx_head_sym == tx_buf_start)
        copy_len = MIN(copy_len, tx_buf_start + tx_buf_len - *tx_tail_sym - 1);
      else
        copy_len = MIN(copy_len, tx_buf_start + tx_buf_len - *tx_tail_sym);
    }
    memcpy((void*)tx_tail_virt, (void*)rx_head_virt, copy_len);

    tx_tail_virt += copy_len;
    if (tx_tail_virt == mzone_tx->addr + tx_buf_len)
      tx_tail_virt = mzone_tx->addr;

    uint64_t maybe_tx_tail_sym = *tx_tail_sym + copy_len;
    *tx_tail_sym = (maybe_tx_tail_sym != tx_buf_start + tx_buf_len) ? maybe_tx_tail_sym : tx_buf_start;
    return copy_len;
}

void *rx_tx_manage(void *arg) {
    struct nfp_cpp* cpp = arg;
    void *tbl = nfp_rtsym_table_read(cpp);

    struct nfp_cpp_area * areas[4];
    for (int i=0; i<4; i++) {
      areas[i] = (struct nfp_cpp_area* )malloc(sizeof(struct nfp_cpp_area));
    }

    rx_head_sym = nfp_rtsym_map(tbl, RX_HEAD_SYM, 8, &areas[0]);
    rx_tail_sym = nfp_rtsym_map(tbl, RX_TAIL_SYM, 8, &areas[1]);
    start_sym = nfp_rtsym_map(tbl, START_SYM, 4, &areas[2]);

    //rx_buf_start_sym = nfp_rtsym_map(tbl, RX_BUF_START_SYM, 8, &areas[2]);
    //rx_buf_len_sym = nfp_rtsym_map(tbl, RX_BUF_LEN_SYM, 8, &areas[2]);

    //tx_head_sym = nfp_rtsym_map(tbl, TX_HEAD_SYM, 8, &areas[2]);
    //tx_tail_sym = nfp_rtsym_map(tbl, TX_TAIL_SYM, 8, &areas[3]);
    //tx_buf_start_sym = nfp_rtsym_map(tbl, TX_BUF_LEN_SYM, 8, &areas[5]);
    //tx_buf_len_sym = nfp_rtsym_map(tbl, TX_BUF_LEN_SYM, 8, &areas[7]);

    WR(mzone_rx->iova, rx_head_sym);
    WR(mzone_rx->iova, rx_tail_sym);

    // TODO - Update tx_head, tx_tail, tx_buf_len when running TX path as well
    // rx_buf_len (hard coded right now for RX)

    sleep(5);

    nn_writel(1, start_sym);

    rx_buf_start = mzone_rx->iova;
    rx_buf_len = BUF_SIZE;
    tx_buf_start = mzone_tx->iova;
    tx_buf_len = BUF_SIZE;

    while (1) {
      sleep(1);
      printf("%s=0x%" PRIx64 "\n", RX_HEAD_SYM, RD(rx_head_sym));
      printf("%s=0x%" PRIx64 "\n", RX_TAIL_SYM, RD(rx_tail_sym));
      printf("%s=0x%" PRIx64 "\n\n", START_SYM, RD(start_sym));

      // uint64_t just_after_head = RD(rx_head_sym) == rx_buf_start + rx_buf_len - 1 ? rx_buf_start : RD(rx_head_sym) + 1;
      if (RD(rx_head_sym) != RD(rx_tail_sym)) {
        // There is something available for RX.
        printf("Got packet for RX HEAD=0x%" PRIx64 " TAIL=0x%" PRIx64 "\n", RD(rx_head_sym), RD(rx_tail_sym));
        uint64_t copy_len;

        if (RD(rx_tail_sym) > RD(rx_head_sym)) {
            copy_len = RD(rx_tail_sym) - RD(rx_head_sym);
        }
        else {
            copy_len = rx_buf_start + rx_buf_len - RD(rx_head_sym);
        }

        printf("Copy len=%ld\n", copy_len);

        // Copy copy_len of data from rx to tx buffer for echoing.
        uint64_t copied_len = copy_len; // copy_rx_tx(copy_len); Uncomment this when testing TX path.
        uint64_t maybe_rx_head_sym = RD(rx_head_sym) + copied_len;
        maybe_rx_head_sym = maybe_rx_head_sym != rx_buf_start + rx_buf_len ? maybe_rx_head_sym: rx_buf_start;

        printf("Before updating rx head sym %s=0x%" PRIx64 " update=0x%" PRIx64 "\n", RX_HEAD_SYM, RD(rx_head_sym), maybe_rx_head_sym);
        WR(maybe_rx_head_sym, rx_head_sym);
        printf("After updating rx head sym %s=0x%" PRIx64 "\n", RX_HEAD_SYM, RD(rx_head_sym));

        rx_head_virt += copied_len;
        memset((void *)rx_head_virt, 0, copied_len); // Remove this when tesing TX path.

        if (rx_head_virt == mzone_rx->addr + rx_buf_len) {
            rx_head_virt = mzone_rx->addr;
        }
      }
    }
    return NULL;
}

int main(int argc, char* argv[])
{
    pthread_t buf_logger;
    pthread_t rx_tx_manager;
    struct rte_pci_device* dev;
    int ret;

    memzone_init();
    mzone_rx = memzone_reserve(BUF_SIZE);
    mzone_tx = memzone_reserve(BUF_SIZE);

    printf("Physical address RX start - 0x%" PRIx64 ", end - 0x%" PRIx64 ", BUF_SIZE=%ld\n", mzone_rx->iova, mzone_rx->iova + BUF_SIZE, BUF_SIZE);
    // printf("Physical from Virtual address RX start - 0x%" PRIx64 ", end - 0x%" PRIx64 ", BUF_SIZE=%ld\n", mem_virt2phy((void*)mzone_rx->addr), mem_virt2phy((void*)(mzone_rx->addr + BUF_SIZE)), BUF_SIZE);

    memset((void *)mzone_rx->addr, 0, BUF_SIZE);
    memset((void *)mzone_tx->addr, 0, BUF_SIZE);

    rx_head_virt = mzone_rx->addr;
    tx_tail_virt = mzone_tx->addr;
    rx_buf_len = BUF_SIZE;
    tx_buf_len = BUF_SIZE;

    pthread_create( &buf_logger, NULL, buffer_logger, NULL);

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

    pthread_create( &rx_tx_manager, NULL, rx_tx_manage, cpp);
    nfp_cpp_dev_main(dev, cpp);
    fprintf(stderr, "Exit CPP handler\n");

    while (1) {
      sleep(1);
    }

    return 0;
}
