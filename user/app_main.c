#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "devcfg.h"
#include "config.h"
#include "memzone.h"
#include "driver.h"
#include "ring_buffer.h"
#include "io.h"
#include "nfp_cpp.h"
#include "nfp_rtsym.h"

extern int nfp_cpp_dev_main(struct rte_pci_device* dev, struct nfp_cpp* cpp);

static const struct memzone  *buffer_rx, *buffer_tx;
static struct ringbuffer_t ring_rx, ring_tx;

#define SYMBOL_DEVICE_META "_dev_meta"

void* log_main(void* _ptr)
{
    (void) _ptr;
    int fd;

    if ((fd = open("buffer_log_rx", O_CREAT | O_RDWR, 0666)) < 0)
    {
        perror("open failed");
        return NULL;
    }

    if ((ftruncate(fd, RING_BUFFER_SIZE)) < 0)
    {
        perror("trunate failed");
        return NULL;
    }

    while (1)
    {
        if (pwrite(fd, (void*) buffer_rx->addr, RING_BUFFER_SIZE, 0) < 0)
        {
            perror("write failed");
            return NULL;
        }

        sleep(1);
    }

    return NULL;
}

void configure_device(struct device_meta_t* meta)
{
    meta->packet_size = UDP_PACKET_SIZE;
    meta->buffer_size = RING_BUFFER_SIZE;
    meta->rx_buffer_iova = buffer_rx->iova;
    meta->tx_buffer_iova = buffer_tx->iova;
    meta->rx_head = meta->rx_tail = 0;
    meta->tx_head = meta->tx_tail = 0;

    rte_io_wmb();   /* Flush preceding writes! */

    nn_writeq(1, &meta->start_signal);
}

void* udp_worker(void* arg)
{
    struct nfp_cpp* cpp = (struct nfp_cpp*) arg;
    struct nfp_rtsym_table* symbol_table = nfp_rtsym_table_read(cpp);
    struct nfp_cpp_area* device_meta_area = (struct nfp_cpp_area*) malloc(sizeof(struct nfp_cpp_area));
    struct device_meta_t* meta = (struct device_meta_t*)
                                        nfp_rtsym_map(
                                            symbol_table,
                                            SYMBOL_DEVICE_META,
                                            sizeof(struct device_meta_t),
                                            &device_meta_area);

    if (meta == NULL)
        return NULL;

    ring_rx.base_addr = (void*) buffer_rx->addr;
    ring_rx.capacity = RING_BUFFER_SIZE;
    ring_rx.entry_size = UDP_PACKET_SIZE;
    ring_tx.base_addr = (void*) buffer_tx->addr;
    ring_tx.capacity = RING_BUFFER_SIZE;
    ring_tx.entry_size = UDP_PACKET_SIZE;

    configure_device(meta);

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

    buffer_rx = memzone_reserve(RING_BUFFER_SIZE);
    buffer_tx = memzone_reserve(RING_BUFFER_SIZE);

    memset((void*) buffer_rx->addr, 0, RING_BUFFER_SIZE);
    memset((void*) buffer_tx->addr, 0, RING_BUFFER_SIZE);

    fprintf(stderr, "BUFFER RX %u Physical: [0x%p ~ 0x%p]\n",
            RING_BUFFER_SIZE, (char*) buffer_rx->iova, (char*) buffer_rx->iova + RING_BUFFER_SIZE);
    fprintf(stderr, "BUFFER TX %u Physical: [0x%p ~ 0x%p]\n",
            RING_BUFFER_SIZE, (char*) buffer_rx->iova, (char*) buffer_rx->iova + RING_BUFFER_SIZE);

    pthread_t log_thread, worker_thread;
    pthread_create(&log_thread, NULL, log_main, NULL);
    pthread_create(&worker_thread, NULL, udp_worker, (void*) cpp);

    nfp_cpp_dev_main(dev, cpp);
    
    pthread_join(worker_thread, NULL);
    pthread_join(log_thread, NULL);

    return 0;
}
