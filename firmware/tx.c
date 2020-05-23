#include <stdint.h>
#include <nfp.h>

#include "txops.h"
#include "devcfg.h"
#include "debug.h"

__declspec(export cls) volatile struct device_meta_t cfg = { 0 };
__shared __lmem uint32_t buffer_capacity, packet_size;

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
    head = cfg.tx_head;
    updated_head = head + packet_size;
    if (updated_head >= buffer_capacity)
        updated_head = 0;

    while (1)
    {
        tail = cfg.tx_tail;

        /* Buffer empty */
        if (head == tail)
            continue;
        
        break;
    }

    // 3. DMA packet data to CTM buffer
    pcie_addr = cfg.tx_buffer_iova + head;
    dma_packet_recv(&pkt, packet_size, pcie_addr);
    pkt.nbi_meta.pkt_info.len = packet_size + MAC_PREPEND_BYTES;

    // 4. Update RingBuffer
    cfg.tx_head = updated_head;

    // 5. Send packet over NBI
    send_packet(&pkt);
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

    pkt_ctm_init_credits(&ctm_credits, MAX_ME_CTM_PKT_CREDITS, MAX_ME_CTM_BUF_CREDITS);

    while (1)
    {
        tx_process();
    }

    return 0;
}