#include <stdint.h>
#include <nfp.h>
#include <nfp/mem_bulk.h>
#include <blm.h>

#include "txops.h"
#include "debug.h"
#include "dma.h"

extern __shared __lmem uint32_t buffer_capacity, packet_size;
extern __export __shared __cls struct ctm_pkt_credits ctm_credits;

__mem40 void* allocate_packet(struct pkt_t* pkt)
{
    unsigned int pkt_num;
    __xread blm_buf_handle_t buf;
    __xwrite struct nbi_meta_catamaran nbi_meta;
    unsigned int blq;
    __mem40 void* pkt_ctm_buffer;

    /* Allocate CTM Buffer */
    while (1)
    {
        // Allocate and Replenish Credits
        pkt_num = pkt_ctm_alloc(&ctm_credits, __ISLAND, PKT_CTM_SIZE_256, 1, 1);
        if (pkt_num != ((unsigned int)-1))
            break;
    }

    /* Allocate MU buffer */
    blq = 0;
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
    pkt_ctm_buffer = pkt_ctm_ptr40(__ISLAND, pkt_num, 0);
    nbi_meta = pkt->nbi_meta;
    mem_write32(&nbi_meta, pkt_ctm_buffer, sizeof(struct nbi_meta_catamaran));

    pkt_ctm_buffer = pkt_ctm_ptr40(__ISLAND, pkt_num, PKT_NBI_OFFSET);
    return pkt_ctm_buffer;
}

void send_packet(struct pkt_t* pkt)
{
    __mem40 void* pkt_data;
    __gpr struct pkt_ms_info msi_gen;
    unsigned int q_dst, seqr, seq;

    pkt_data = pkt_ctm_ptr40(pkt->nbi_meta.pkt_info.isl, pkt->nbi_meta.pkt_info.pnum, 0);
    q_dst = PORT_TO_CHANNEL(3);    // TODO: Hardcoded based on experience
    seqr = 0;
    seq = 0;

    // Write MAC cmd to generate L3/L4 checksums
    pkt_mac_egress_cmd_write(pkt_data, PKT_NBI_OFFSET + MAC_PREPEND_BYTES, 1, 1);
    msi_gen = pkt_msd_write(pkt_data, PKT_NBI_OFFSET);

    pkt_nbi_send(pkt->nbi_meta.pkt_info.isl,
                pkt->nbi_meta.pkt_info.pnum,
                &msi_gen,
                pkt->nbi_meta.pkt_info.len,
                0,
                q_dst,
                seqr,
                seq,
                PKT_CTM_SIZE_256);
}
