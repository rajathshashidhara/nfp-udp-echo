#include <stdint.h>
#include <nfp.h>

#include "txops.h"
#include "devcfg.h"
#include "debug.h"
#include "dma.h"

__declspec(export cls) volatile struct device_meta_t cfg = { 0 };
__shared __lmem uint32_t buffer_capacity, packet_size;

__volatile __shared __lmem uint32_t shadow_head = 0;
__volatile __shared __lmem uint8_t init = 0;

/* CTM credit defines */
#define MAX_ME_CTM_PKT_CREDITS  256
#define MAX_ME_CTM_BUF_CREDITS  32

__export __shared __cls struct ctm_pkt_credits ctm_credits;

void tx_process(void)
{
    struct pkt_t pkt;
    __mem40 void* pkt_data;
    volatile uint32_t tail;
    uint32_t head, updated_head;
    uint64_t pcie_addr;

    // 1. Allocate packet
    pkt_data = allocate_packet(&pkt);

    // 2. Wait until TX ring is non-empty
    while (1)
    {
        // Access from CLS. Thread will be swapped out!
        tail = cfg.tx_tail;

        // Access from local memory. No swapping
        head = shadow_head;
        updated_head = head + packet_size;
        if (updated_head >= buffer_capacity)
            updated_head = 0;

        /* Buffer empty */
        if (head == tail)
            continue;

        break;
    }
    shadow_head = updated_head;

    // 3. DMA packet data to CTM buffer
    pcie_addr = cfg.tx_buffer_iova + head;
    dma_packet_recv(&pkt, packet_size, pcie_addr);
    pkt.nbi_meta.pkt_info.len = packet_size + MAC_PREPEND_BYTES;

    // 4. Update RingBuffer
    while (1)
    {
        // Access from CLS. Thread will be swapped out!
        if (cfg.tx_head != head)
            continue;

        break;
    }
    // Access to CLS is in-order. No need for atomic update
    cfg.tx_head = updated_head;

    // 5. Send packet over NBI
    send_packet(&pkt);
}

int main(void)
{
    volatile uint64_t start;

    /* Wait for start signal to load configuration paramters */
    while (1)
    {
        start = cfg.start_signal;

        if (start)
            break;
    }

    /* Initialize configuration */
    if (ctx() == 0)
    {
        pkt_ctm_init_credits(&ctm_credits, MAX_ME_CTM_PKT_CREDITS, MAX_ME_CTM_BUF_CREDITS);
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
        tx_process();
    }

    return 0;
}
