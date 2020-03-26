#ifndef _MEMZONE_H_
#define _MEMZONE_H_

#include <stdint.h>
#include <stddef.h>

#define MEMZONE_BAD_IOVA    -1

/**
 * @file
 * Memzones allow user-space application to reserve a contiguous
 * space of physical memory that can be mapped for DMA transfers
 * through IO address space and also into the VA space of the 
 * application.
 * 
 * @note
 * This code is largely borrowed from DPDK memzone implementation.
 * Refer: lib/librte_eal/common/include/rte_memzone.h 
 *          @ https://github.com/DPDK/dpdk/
 * 
 * https://www.dpdk.org/blog/2019/08/21/memory-in-dpdk-part-1-general-concepts/
 * - Hugepages cannot be swapped out under memory pressure.
 * - Allocate memory using hugepages
 * - Read the PA from /proc/self/pagemap
 * 
 * @todo
 *      1. Freelist management
 *      2. Thread-safety!
 *      3. NUMA
 */

struct memzone
{
    uint64_t iova;          /*> IO address: For use by device */
    uint64_t addr;          /*> Virtual address */
    size_t len;             /*> Length of memzone */
    uint16_t flags;         /*> Unused */
    uint16_t handle;        /*> Opaque identifier of memzone */
} __attribute__((__packed__));

/**
 * Reserve a portion of contiguous physical memory.
 *
 * This function reserves some memory and returns a pointer to a
 * correctly filled memzone descriptor. If the allocation cannot be
 * done, return NULL.
 * 
 * @note
 * Currently, minimum allocation unit is HUGE_PAGE_SIZE.
 * Best to make large alloctions and manage the internal fragmantation 
 * within the application.
 * 
 * @param len
 *   The size of the memory to be reserved. If it
 *   is 0, the biggest contiguous zone will be reserved.
 * @return
 *   A pointer to a correctly-filled read-only memzone descriptor, or NULL
 *   on error.
 *   On error case, errno will be set appropriately:
 *    - ENOSPC - the maximum number of memzones has already been allocated
 *    - ENOMEM - no appropriate memory area found in which to create memzone
 *    - EINVAL - invalid parameters
 */
const struct memzone* memzone_reserve(size_t len);

/**
 * Free a memzone.
 *
 * @param mz
 *   A pointer to the memzone
 * @return
 *  -EINVAL - invalid parameter.
 *  0 - success
 */
int memzone_free(const struct memzone *mz);

/**
 * Initialize memzone allocator.
 * Must be called before performs any allocations.
 *
 */
void memzone_init();

#endif /* _MEMZONE_H_ */
