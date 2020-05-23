#include <stdint.h>
#include <nfp.h>
#include <nfp/mem_bulk.h>
#include <nfp6000/nfp_me.h>
#include <blm.h>

#include "devcfg.h"
#include "pktdef.h"
#include "debug.h"
#include "dma.h"

enum
{
    ALLOW,
    DROP
};

__declspec(export cls) volatile struct device_meta_t cfg = { 0 };
__shared __lmem uint32_t buffer_capacity, packet_size;

__volatile __shared __emem uint32_t debug[4096 * 64];
__volatile __shared __emem uint32_t debug_idx;

__intrinsic __pkt_recv_with_hdrs(__xread void* pkt_xfer)
{
    unsigned int addr = (PKT_NBI_OFFSET >> 2);
    unsigned int count = (sizeof(struct pkt_t) >> 2);
    SIGNAL add_thread_sig;
    struct nfp_mecsr_prev_alu ind;

    ctassert(__is_ct_const(sizeof(struct pkt_t)));
    ctassert(sizeof(struct pkt_t) % 4 == 0);

    ind.__raw = 0;
    ind.ov_len = 1;
    ind.length = count - 1;

    __asm alu[--, --, B, ind.__raw];
    __asm mem[packet_add_thread, *pkt_xfer, addr, 0, __ct_const_val(count)], ctx_swap[*add_thread_sig], indirect_ref;
}

__mem40 void* receive_packet_with_hdrs(struct pkt_t* pkt)
{
    __xread struct pkt_t _xfer_pkt;
    int island, pnum, pkt_off;

/**
 * pkt_nbi_recv_with_hdrs(&_xfer_pkt, sizeof(struct pkt_t), PKT_NBI_OFFSET);
 *
 * API currently does not support count > 8.
 * Use indirect addressing to issue command
 */
    __pkt_recv_with_hdrs(&_xfer_pkt);
    *pkt = _xfer_pkt;

    pkt_off = PKT_NBI_OFFSET;
    island = pkt->nbi_meta.pkt_info.isl;
    pnum = pkt->nbi_meta.pkt_info.pnum;

    return pkt_ctm_ptr40(island, pnum, pkt_off);
}

__mem40 void* receive_packet(struct pkt_t* pkt)
{
    __xread struct nbi_meta_catamaran nbi_meta;
    int island, pnum, pkt_off;

    pkt_nbi_recv(&nbi_meta, sizeof(struct nbi_meta_catamaran));
    pkt->nbi_meta = nbi_meta;

    pkt_off = PKT_NBI_OFFSET;
    island = nbi_meta.pkt_info.isl;
    pnum = nbi_meta.pkt_info.pnum;

    return pkt_ctm_ptr40(island, pnum, pkt_off);
}

void read_packet_header(struct pkt_t* pkt)
{
    __xread struct pkt_hdr_t hdr;
    int island, pnum;
    int pkt_off;
    __mem40 void* pkt_ctm_buffer;

    pkt_off = PKT_NBI_OFFSET;
    island = pkt->nbi_meta.pkt_info.isl;
    pnum = pkt->nbi_meta.pkt_info.pnum;
    pkt_ctm_buffer = pkt_ctm_ptr40(island, pnum, pkt_off);

    mem_read32(&hdr, pkt_ctm_buffer, sizeof(hdr));
    pkt->hdr = hdr;
}

void write_packet_header(struct pkt_t* pkt)
{
    __xwrite struct pkt_hdr_t hdr;
    int island, pnum;
    int pkt_off;
    __mem40 void* pkt_ctm_buffer;

    pkt_off = PKT_NBI_OFFSET;
    island = pkt->nbi_meta.pkt_info.isl;
    pnum = pkt->nbi_meta.pkt_info.pnum;
    pkt_ctm_buffer = pkt_ctm_ptr40(island, pnum, pkt_off);

    hdr = pkt->hdr;
    mem_write32(&hdr, pkt_ctm_buffer, sizeof(hdr));
}

int filter_packets(struct pkt_t* pkt)
{
    /* Drop non-IP packets */
    if (pkt->hdr.eth.type != 0x0800)
        return DROP;

    /* Drop non-UDP packets */
    if (pkt->hdr.ip.proto != 0x11)
        return DROP;

    /* Drop packets with illegal pkt length */
    if ((pkt->nbi_meta.pkt_info.len - 2 * MAC_PREPEND_BYTES) != packet_size)
        return DROP;

    return ALLOW;
}

void modify_packet_header(struct pkt_t* pkt)
{
    struct eth_addr temp_eth_addr;
    uint32_t temp_ip_addr;

    /* Swap IP address */
    temp_eth_addr = pkt->hdr.eth.dst;
    pkt->hdr.eth.dst = pkt->hdr.eth.src;
    pkt->hdr.eth.src = temp_eth_addr;

    /* Swap IP address */
    temp_ip_addr = pkt->hdr.ip.dst;
    pkt->hdr.ip.dst = pkt->hdr.ip.src;
    pkt->hdr.ip.src = temp_ip_addr;
}

void free_packet(struct pkt_t* pkt)
{
    blm_buf_free(pkt->nbi_meta.pkt_info.muptr, pkt->nbi_meta.pkt_info.bls);
    pkt_ctm_free(pkt->nbi_meta.pkt_info.isl, pkt->nbi_meta.pkt_info.pnum);
}

void drop_packet(struct pkt_t* pkt)
{
    __gpr struct pkt_ms_info msi = {0,0};

    /* Free allocated buffers */
    free_packet(pkt);

    /* Drop packet at the sequencer */
    pkt_nbi_drop_seq(pkt->nbi_meta.pkt_info.isl,
                    pkt->nbi_meta.pkt_info.pnum,
                    &msi,
                    pkt->nbi_meta.pkt_info.len,
                    0,
                    0,
                    pkt->nbi_meta.seqr,
                    pkt->nbi_meta.seq,
                    PKT_CTM_SIZE_256);
}

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
