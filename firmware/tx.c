#include <stdint.h>
#include <nfp.h>
#include <nfp/mem_bulk.h>
#include <blm.h>

#include "devcfg.h"
#include "pktdef.h"
#include "debug.h"
#include "dma.h"

__declspec(export cls) volatile struct device_meta_t cfg;
__declspec(lmem) uint32_t buffer_capacity, packet_size;

/* CTM credit defines */
#define MAX_ME_CTM_PKT_CREDITS  256
#define MAX_ME_CTM_BUF_CREDITS  32

__export __shared __cls struct ctm_pkt_credits ctm_credits;

__mem40 void* allocate_packet(struct pkt_t* pkt)
{
    /* Allocate CTM Buffer */
    unsigned int pkt_num;
    while (1)
    {
        // Allocate and Replenish Credits
        pkt_num = pkt_ctm_alloc(&ctm_credits, __ISLAND, PKT_CTM_SIZE_256, 1, 1);
        if (pkt_num != ((unsigned int)-1))
            break;
    }

    /* Allocate MU buffer */
    __xread blm_buf_handle_t buf;
    unsigned int blq = 0;
    while (1)
    {
        if (blm_buf_alloc(&buf, blq) == 0)
            break;
    }

    /* Initialize packet metadata */
    pkt->nbi_meta.pkt_info.isl = __ISLAND;
    pkt->nbi_meta.pkt_info.pnum = pkt_num;
    pkt->nbi_meta.pkt_info.bls = blq;
    pkt->nbi_meta.pkt_info.muptr = buf;
    pkt->nbi_meta.pkt_info.split = 1;
    pkt->nbi_meta.pkt_info.len = 0;

    /* Copy packet metadata to CTM */
    __mem40 void* pkt_ctm_buffer;
    pkt_ctm_buffer = pkt_ctm_ptr40(__ISLAND, pkt_num, 0);
    mem_write32(&pkt->nbi_meta, pkt_ctm_buffer, sizeof(pkt->nbi_meta));

    pkt_ctm_buffer = pkt_ctm_ptr40(__ISLAND, pkt_num, PKT_NBI_OFFSET);
    return pkt_ctm_buffer;
}

void send_packet(struct pkt_t* pkt)
{
    __mem40 void* pkt_data;
    __gpr struct pkt_ms_info msi_gen;
    unsigned int q_dst, seqr, seq;

    pkt_data = pkt_ctm_ptr40(pkt->nbi_meta.pkt_info.isl, pkt->nbi_meta.pkt_info.pnum);
    q_dst = PORT_TO_CHANNEL(3);    // TODO: Hardcoded based on experience
    seqr = 0;
    seq = 0;

    // Write MAC cmd to generate L3/L4 checksums
    pkt_mac_egress_cmd_write(pkt_data, PKT_NBI_OFFSET + MAC_PREPEND_BYTES, 1, 1);
    msi_gen = pkt_msd_write(pkt_data, PKT_NBI_OFFSET);

    pkt_nbi_send(pkt->nbi_meta.pkt_info.isl,
                pkt->nbi_meta.pkt_info.pkt_num,
                &msi_gen,
                pkt->nbi_meta.pkt_info.len,
                NBI,
                q_dst,
                seqr,
                seq,
                PKT_CTM_SIZE_256);
}

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
    dma_recv(pkt_data + MAC_PREPEND_BYTES, packet_size, pcie_addr);
    pkt->nbi_meta.pkt_info.len = packet_size + MAC_PREPEND_BYTES;

    // 4. Update RingBuffer
    cfg.tx_head = updated_head;

    // 5. Send packet over NBI
    send_packet(&pkt);
}

int main(void)
{
    /* Restrict to single context */
    if (ctx() != 0)
    {
        return 0;
    }

    /* Wait for start signal to load configuration paramters */
    volatile uint64_t start;
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