#include <stdint.h>
#include <nfp.h>
#include <nfp/me.h>
#include <nfp/mem_atomic.h>

#include "rxops.h"
#include "devcfg.h"
#include "debug.h"
#include "dma.h"

__declspec(export cls) volatile struct device_meta_t cfg = { 0 };
__shared __lmem uint32_t buffer_capacity, packet_size;

#ifdef PKT_STATS
__declspec(export imem) uint64_t rx_counters[8];
#endif

__volatile __shared __emem uint32_t debug[4096 * 64];
__volatile __shared __emem uint32_t debug_idx;

__volatile __shared __lmem uint32_t shadow_tail = 0;
__volatile __shared __lmem uint8_t init = 0;

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
    while (1)
    {
        // Access from CLS. Thread will be swapped out!
        head = cfg.rx_head;

        // Access from local memory. No swapping
        tail = shadow_tail;
        updated_tail = tail + packet_size;
        if (updated_tail >= buffer_capacity)
            updated_tail = 0;

        /* Buffer full */
        if (updated_tail == head)
            continue;

        break;
    }
    shadow_tail = updated_tail;

    // 7. DMA the packet to host memory
    pcie_addr = cfg.rx_buffer_iova + tail;
    dma_packet_send(&pkt, pcie_addr);

    // 8. Update RingBuffer
    while (1)
    {
        // Access from CLS. Thread will be swapped out!
        if (cfg.rx_tail != tail)
            continue;

        break;
    }
    // Access to CLS is in-order. No need for atomic update
    cfg.rx_tail = updated_tail;

    // 9. Free packet
    drop_packet(&pkt);
}

int main(void)
{
    volatile uint64_t start;

    /* Initialize configuration */
    if (ctx() == 0)
    {
        /* Wait for start signal to load configuration paramters */
        while (1)
        {
            start = cfg.start_signal;

            if (start)
                break;
        }

        buffer_capacity = cfg.buffer_size;
        packet_size = cfg.packet_size;
        init = 1;
    }
    else
    {
        while (1)
        {
            if (init)
                break;

            ctx_swap();
        }
    }

    while (1)
    {
        rx_process();

#ifdef PKT_STATS
        mem_incr64(&rx_counters[ctx()]);
#endif
    }

    return 0;
}
