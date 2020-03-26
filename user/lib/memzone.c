#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <config.h>
#include <memzone.h>

#define MAX_MEMZONES            1024
#define MEMZONE_HANDLE_INVALID  (MAX_MEMZONES + 1)
#define PFN_MASK_SIZE           8
#define MEMZONE_FILENAME_FMT    "/mnt/huge/memzone-%d"
#define MEMZONE_FILENAME_LEN    64

/**
 * TODO: Encapsulate as a structure and use File-backed arrays
 */
static struct memzone _mz[MAX_MEMZONES];
static uint16_t _free_mz = MAX_MEMZONES;

/**
 * Macro to align a value to a given power-of-two. The resultant value
 * will be of the same type as the first parameter, and will be no
 * bigger than the first parameter. Second parameter must be a
 * power-of-two value.
 */
#define ALIGN_FLOOR(val, align) \
	(typeof(val))((val) & (~((typeof(val))((align) - 1))))

/**
 * Macro to align a value to a given power-of-two. The resultant value
 * will be of the same type as the first parameter, and will be no lower
 * than the first parameter. Second parameter must be a power-of-two
 * value.
 */
#define ALIGN_CEIL(val, align) \
	ALIGN_FLOOR(((val) + ((typeof(val)) (align) - 1)), align)

/**
 * Convert virtual address to physical address.
 * Hacky solution used by DPDK!
 * 
 * @note
 * Refer http://man7.org/linux/man-pages/man5/proc.5.html
 */ 
static uint64_t
mem_virt2phy(const void* virtaddr)
{
    int fd, retval;
    uint64_t page, paddr;
    unsigned long virt_pfn;
    off_t offset;

    fd = open("/proc/self/pagemap", O_RDONLY);
    if (fd < 0)
    {
        fprintf(stderr, "%s(): Cannot open %s: %s\n",
            __func__, "/proc/self/pagemap", strerror(errno));
        return MEMZONE_BAD_IOVA;
    }

    virt_pfn = (unsigned long)virtaddr / PAGE_SIZE;
    offset = sizeof(uint64_t) * virt_pfn;   // Offset into pagemap file
    if (lseek(fd, offset, SEEK_SET) == (off_t) -1)
    {
        fprintf(stderr, "%s(): Cannot seek in %s: %s\n",
            __func__, "/proc/self/pagemap", strerror(errno));
        close(fd);
        return MEMZONE_BAD_IOVA;
    }

    retval = read(fd, &page, PFN_MASK_SIZE);
    close(fd);
	if (retval < 0)
    {
		fprintf(stderr, "%s(): cannot read %s: %s\n",
				__func__, "/proc/self/pagemap", strerror(errno));
		return MEMZONE_BAD_IOVA;
	}
    else if (retval != PFN_MASK_SIZE)
    {
		fprintf(stderr, "%s(): read %d bytes from %s"
				"but expected %d:\n",
				__func__, retval, "/proc/self/pagemap", PFN_MASK_SIZE);
		return MEMZONE_BAD_IOVA;
	}

	/*
	 * the pfn (page frame number) are bits 0-54 (see
	 * pagemap.txt in linux Documentation)
	 */
	if ((page & 0x7fffffffffffffULL) == 0)
		return MEMZONE_BAD_IOVA;

	paddr = ((page & 0x7fffffffffffffULL) * PAGE_SIZE)
		+ ((unsigned long)virtaddr % PAGE_SIZE);

	return paddr;
}

/**
 * Find the first empty memzone to allocate.
 */
static struct memzone*
alloc_memzone()
{
    uint16_t idx;
    struct memzone* alloc_mz = NULL;

    if (_free_mz == 0)
        return NULL;

    for (idx = 0; idx < MAX_MEMZONES; idx++)
    {
        alloc_mz = &_mz[idx];

        if (alloc_mz->handle == MEMZONE_HANDLE_INVALID)
        {
            alloc_mz->handle = idx;
            _free_mz--;

            return alloc_mz;
        }
    }

    return NULL;
}

/**
 * Free memzone.
 */
static void
free_memzone(const struct memzone* mz)
{
    uint16_t idx;

    if (mz == NULL)
        return;

    idx = mz->handle;
    if (idx >= MAX_MEMZONES)
        return;
    if (mz != &_mz[idx])
        return;
    
    _mz[idx].handle = MEMZONE_HANDLE_INVALID;
    _free_mz++;
}

const struct memzone* memzone_reserve(size_t len)
{
    char filename[MEMZONE_FILENAME_LEN];
    struct memzone* mz;
    void* addr;
    int fd;

    /* This is automatically aligned to CACHE_LINE size */
    len = ALIGN_CEIL(len, HUGE_PAGE_SIZE);

    mz = alloc_memzone();
    if (mz == NULL)
    {
        // TODO: Set -ENOSPC
        fprintf(stderr, "%s(): Maximum memzones limit reached: %d\n",
            __func__, MAX_MEMZONES);
        return NULL;
    }

    snprintf(filename, MEMZONE_FILENAME_LEN, 
            MEMZONE_FILENAME_FMT, mz->handle);
    fd = open(filename, O_CREAT | O_RDWR);
    if (fd < 0)
    {
        fprintf(stderr, "%s(): Cannot create file: %s\n",
            __func__, filename);
        free_memzone(mz);
        return NULL;
    }

    addr = mmap(NULL, len, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE,
        fd, 0);
    close(fd);
    if (addr == MAP_FAILED)
    {
        fprintf(stderr, "%s(): Cannot allocate memory: %s\n",
            __func__, strerror(errno));
        free_memzone(mz);
        return NULL;
    }

    mz->addr = (uint64_t) addr;
    mz->len = len;
    mz->iova = mem_virt2phy(addr);
    mz->flags = 0;

    return mz;
}

int 
memzone_free(const struct memzone *mz)
{
    void* addr;
    size_t len;
    int ret;

    if (mz == NULL)
        return 0;

    addr = mz->addr;
    len = mz->len;
    ret = munmap(addr, len);
    if (ret < 0)
    {
        fprintf(stderr, "%s(): Failed to free memzone: %s\n",
            __func__, strerror(errno));
    }

    free_memzone(mz);
    return ret;
}

void 
memzone_init()
{
    unsigned idx;

    for (idx = 0; idx < MAX_MEMZONES; idx++)
    {
        _mz[idx].handle = MEMZONE_HANDLE_INVALID;
    }
    _free_mz = MAX_MEMZONES;
}