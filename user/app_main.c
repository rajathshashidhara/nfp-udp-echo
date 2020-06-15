#include <stdio.h>
#include <string.h>

#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/limits.h>

#include "rte_pci.h"
#include "io.h"
#include "config.h"
#include "devcfg.h"
#include "ring_buffer.h"

static struct ringbuffer_t ring_rx, ring_tx;
static void *buffer_rx, *buffer_tx;
static struct device_meta_t* meta;

int map_resources(struct rte_pci_addr dev_addr, int resource_idx, off_t offset_cfg)
{
    int fd;
    char devname[PATH_MAX];

    snprintf(devname, sizeof(devname),
        "%s/" PCI_PRI_FMT "/resource%d",
        "/sys/bus/pci/devices",
        dev_addr.domain, dev_addr.bus, dev_addr.devid,
        dev_addr.function, resource_idx);
    fd = open(devname, O_RDWR);
    if (fd < 0)
    {
        fprintf(stderr, "Cannot open %s: %s\n",
            devname, strerror(errno));
        return -1;
    }
    meta = (struct device_meta_t*)
                            mmap(NULL,
                                sizeof(struct device_meta_t),
                                PROT_READ | PROT_WRITE,
                                MAP_SHARED,
                                fd, offset_cfg);
    if (meta == MAP_FAILED)
    {
        fprintf(stderr, "Cannot memory map: %s\n",
            strerror(errno));
        return -1;
    }
    close(fd);

    snprintf(devname, sizeof(devname),
        "/mnt/huge/memzone-%d", 0);
    fd = open(devname, O_RDWR);
    if (fd < 0)
    {
        fprintf(stderr, "Cannot open %s: %s\n",
            devname, strerror(errno));
        return -1;
    }
    buffer_rx = mmap(NULL, RING_BUFFER_SIZE,
                    PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_POPULATE,
                    fd, 0);
    if (buffer_rx == MAP_FAILED)
    {
        fprintf(stderr, "Cannot memory map: %s\n",
            strerror(errno));
        return -1;
    }
    close(fd);

    snprintf(devname, sizeof(devname),
        "/mnt/huge/memzone-%d", 1);
    fd = open(devname, O_RDWR);
    if (fd < 0)
    {
        fprintf(stderr, "Cannot open %s: %s\n",
            devname, strerror(errno));
        return -1;
    }
    buffer_tx = mmap(NULL, RING_BUFFER_SIZE,
                    PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_POPULATE,
                    fd, 0);
    if (buffer_tx == MAP_FAILED)
    {
        fprintf(stderr, "Cannot memory map: %s\n",
            strerror(errno));
        return -1;
    }
    close(fd);

    ring_rx.base_addr = (void*) buffer_rx;
    ring_rx.capacity = RING_BUFFER_SIZE;
    ring_rx.entry_size = UDP_PACKET_SIZE;
    ring_tx.base_addr = (void*) buffer_tx;
    ring_tx.capacity = RING_BUFFER_SIZE;
    ring_tx.entry_size = UDP_PACKET_SIZE;

    return 0;
}

void udp_worker()
{
    while (1)
    {
        ring_rx.tail = nn_readl(&meta->rx_tail);
        ring_tx.head = nn_readl(&meta->tx_head);

        fprintf(stderr, "RING RX [%u ~ %u]\n", ring_rx.head, ring_rx.tail);
        fprintf(stderr, "RING TX [%u ~ %u]\n", ring_tx.head, ring_tx.tail);

        if (!ringbuffer_empty(&ring_rx) && !ringbuffer_full(&ring_tx))
        {
            void* readptr = ringbuffer_front(&ring_rx);
            void* writeptr = ringbuffer_back(&ring_tx);

            memcpy(writeptr, readptr, UDP_PACKET_SIZE);

            ringbuffer_pop(&ring_rx);
            ringbuffer_push(&ring_tx);

            rte_io_wmb();

            nn_writel(ring_tx.tail, &meta->tx_tail);
            nn_writel(ring_rx.head, &meta->rx_head);
        }
    }
}

void usage()
{
    fprintf(stderr, "nfp-user.out [PCI-DEV-ID] [BAR-RESOURCE-IDX] [SYMBOL-OFFSET]\n");
}

int main(int argc, char* argv[])
{
    if (argc < 4)
    {
        usage();
        return 0;
    }

    struct rte_pci_addr addr;
    sscanf(argv[1], "%u:%hhu:%hhu.%hhu", &addr.domain, &addr.bus, &addr.devid, &addr.function);

    int resource_idx;
    sscanf(argv[2], "%d", &resource_idx);

    off_t meta_offset;
    sscanf(argv[3], "%ld", &meta_offset);

    if (map_resources(addr, resource_idx, meta_offset) < 0)
    {
        usage();
        return 0;
    }

    udp_worker();

    return 0;
}