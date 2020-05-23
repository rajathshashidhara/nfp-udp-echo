#include <stdint.h>
#include <nfp.h>

#include "rxops.h"
#include "devcfg.h"
#include "debug.h"

__declspec(export cls) volatile struct device_meta_t cfg = { 0 };
__shared __lmem uint32_t buffer_capacity, packet_size;

__volatile __shared __emem uint32_t debug[4096 * 64];
__volatile __shared __emem uint32_t debug_idx;

void rx_process(void)
{
    struct pkt_t pkt;
    __mem40 void* pkt_data;
    volatile uint32_t head;
    uint32_t tail, updated_tail;
    uint64_t pcie_addr;

/*

    // 1. Receive packet from NBI
    pkt_data = receive_packet(&pkt);
    
    // 2. Read packet header from CTM
    read_packet_header(&pkt);

*/
    // 1. Receive packet from NBI
    // 2. Read packet header from CTM
    pkt_data = receive_packet_with_hdrs(&pkt);

    // 3. Filter packets based on header
    switch (filter_packets(&pkt))
    {
        case ALLOW:
            break;

        case DROP:
            drop_packet(&pkt);
            return;

        default:
            DEBUG(0xdeadbeef, __LINE__, 0, 0);
            return;
    }

    // 4. Modify header fields locally
    modify_packet_header(&pkt);

    // 5. Copy modified header to CTM
    write_packet_header(&pkt);

    // 6. Wait until RX ring is empty
    tail = cfg.rx_tail;
    updated_tail = tail + packet_size;
    if (updated_tail >= buffer_capacity)
        updated_tail = 0;

    while (1)
    {
        head = cfg.rx_head;
        
        /* Buffer full */
        if (updated_tail == head)
            continue;

        break;
    }

    // 7. DMA the packet to host memory
    pcie_addr = cfg.rx_buffer_iova + tail;
    dma_packet_send(&pkt, pcie_addr);

    // 8. Update RingBuffer
    cfg.rx_tail = updated_tail;

    // 9. Free packet
    drop_packet(&pkt);
}

int main(void)
{
    volatile uint64_t start;

    /* Restrict to single context */
    if (ctx() != 0)
    {
        return 0;
    }

    /* Wait for start signal to load configuration paramters */
    while (1)
    {
        start = cfg.start_signal;

        if (start)
            break;
    }

    buffer_capacity = cfg.buffer_size;
    packet_size = cfg.packet_size;


    while (1)
    {
        rx_process();
    }

    return 0;
}
