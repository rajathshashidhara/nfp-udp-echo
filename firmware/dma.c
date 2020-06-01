#include <nfp.h>
#include <nfp/pcie.h>

#include "dma.h"
#include "config.h"

#define MIN(A, B) ((A) < (B) ? (A) : (B))

/*
 * Fill out all the common parts of the DMA command structure, plus
 * other setup required for DMA engines tests.
 *
 * Once setup, the caller only needs to patch in the PCIe address and
 * is ready to go.
 */
__intrinsic static void
pcie_dma_desc_setup(__gpr struct nfp_pcie_dma_cmd *cmd,
                        int signo,
                        uint32_t len,
                        uint32_t dev_addr_hi,
                        uint32_t dev_addr_lo)
{
    struct nfp_pcie_dma_cfg cfg;
    __xwrite struct nfp_pcie_dma_cfg cfg_wr;
    unsigned int mode_msk_inv;
    unsigned int mode;

    unsigned int meid = __MEID;

    /* Zero the descriptor. */
    cmd->__raw[0] = 0;
    cmd->__raw[1] = 0;
    cmd->__raw[2] = 0;
    cmd->__raw[3] = 0;

    /* We just write config register 0 and 1. no one else is using them */
    cfg.__raw = 0;
    cfg.target_64_even = 1;
    cfg.cpp_target_even = 7;
    cfg.target_64_odd = 1;
    cfg.cpp_target_odd = 7;

    cfg_wr = cfg;
    pcie_dma_cfg_set_pair(0, 0, &cfg_wr);

    /* Signalling setup */
    mode_msk_inv = ((1 << NFP_PCIE_DMA_CMD_DMA_MODE_shf) - 1);
    mode = (((meid & 0xF) << 13) | (((meid >> 4) & 0x3F) << 7) |
            ((__ctx() & 0x7) << 4) | signo);
    cmd->__raw[1] = ((mode << NFP_PCIE_DMA_CMD_DMA_MODE_shf) |
                        (cmd->__raw[1] & mode_msk_inv));

    cmd->cpp_token = 0;
    cmd->cpp_addr_hi = dev_addr_hi;
    cmd->cpp_addr_lo = dev_addr_lo;
    /* On the 6k the length is length - 1 */
    cmd->length = len - 1;
}

__intrinsic static void dma_op(__mem40 void* addr,
                uint32_t len,
                uint64_t pcie_addr,
                int queue,
                SIGNAL_PAIR* sig_pair)
{
    __gpr struct nfp_pcie_dma_cmd dma_cmd;
    __xwrite struct nfp_pcie_dma_cmd dma_cmd_wr;

    pcie_dma_desc_setup(&dma_cmd,
        __signal_number(&sig_pair->even),
        len,
        (uint32_t) (((uint64_t) addr) >> 32),
        (uint32_t) (((uint64_t) addr) & 0xFFFFFFFF));

    dma_cmd.pcie_addr_hi = pcie_addr >> 32;
    dma_cmd.pcie_addr_lo = pcie_addr & 0xFFFFFFFF;

    dma_cmd_wr = dma_cmd;
    __pcie_dma_enq(0, &dma_cmd_wr, queue, sig_done, &sig_pair->odd);
}

void dma_send(__mem40 void* addr,
                uint32_t len,
                uint64_t pcie_addr)
{
    SIGNAL_PAIR cmpl_sig;
    dma_op(addr, len, pcie_addr, NFP_PCIE_DMA_TOPCI_HI, &cmpl_sig);
    wait_for_all(&cmpl_sig.even, &cmpl_sig.odd);
}

void dma_recv(__mem40 void* addr,
                uint32_t len,
                uint64_t pcie_addr)
{
    SIGNAL_PAIR cmpl_sig;
    dma_op(addr, len, pcie_addr, NFP_PCIE_DMA_FROMPCI_HI, &cmpl_sig);
    wait_for_all(&cmpl_sig.even, &cmpl_sig.odd);
}

void dma_packet_send(struct pkt_t* pkt, uint64_t pcie_addr)
{
    unsigned int len = pkt->nbi_meta.pkt_info.len - 2 * MAC_PREPEND_BYTES;
    unsigned int offset = PKT_NBI_OFFSET + 2 * MAC_PREPEND_BYTES;
    unsigned int ctm_transfer_len, buf_transfer_len;
    int island, pnum;
    __mem40 void* pkt_ctm_buffer;
    __mem40 void* pkt_mu_buffer;
    SIGNAL_PAIR ctm_cmpl_sig, buf_cmpl_sig;

    ctm_transfer_len = MIN(256 - PKT_NBI_OFFSET - 2 * MAC_PREPEND_BYTES, len);
    buf_transfer_len = len - ctm_transfer_len;

    island = pkt->nbi_meta.pkt_info.isl;
    pnum = pkt->nbi_meta.pkt_info.pnum;
    pkt_ctm_buffer = pkt_ctm_ptr40(island, pnum, offset);
    dma_op(pkt_ctm_buffer, ctm_transfer_len, pcie_addr, NFP_PCIE_DMA_TOPCI_HI, &ctm_cmpl_sig);

    if (buf_transfer_len > 0)
    {
        pkt_mu_buffer = (__mem40 void*) ((pkt->nbi_meta.pkt_info.muptr << 11) + 256);
        dma_op(pkt_mu_buffer, buf_transfer_len, pcie_addr + ctm_transfer_len, NFP_PCIE_DMA_TOPCI_HI, &buf_cmpl_sig);

        wait_for_all(&ctm_cmpl_sig.even, &ctm_cmpl_sig.odd, &buf_cmpl_sig.even, &buf_cmpl_sig.odd);
        return;
    }

    wait_for_all(&ctm_cmpl_sig.even, &ctm_cmpl_sig.odd);
}

void dma_packet_recv(struct pkt_t* pkt, uint32_t len, uint64_t pcie_addr)
{
    unsigned int offset = PKT_NBI_OFFSET + MAC_PREPEND_BYTES;
    unsigned int ctm_transfer_len, buf_transfer_len;
    int island, pnum;
    __mem40 void* pkt_ctm_buffer;
    __mem40 void* pkt_mu_buffer;
    SIGNAL_PAIR ctm_cmpl_sig, buf_cmpl_sig;

    ctm_transfer_len = MIN(256 - PKT_NBI_OFFSET - MAC_PREPEND_BYTES, len);
    buf_transfer_len = len - ctm_transfer_len;

    island = pkt->nbi_meta.pkt_info.isl;
    pnum = pkt->nbi_meta.pkt_info.pnum;
    pkt_ctm_buffer = pkt_ctm_ptr40(island, pnum, offset);
    dma_op(pkt_ctm_buffer, ctm_transfer_len, pcie_addr, NFP_PCIE_DMA_FROMPCI_HI, &ctm_cmpl_sig);

    if (buf_transfer_len > 0)
    {
        pkt_mu_buffer = (__mem40 void*) ((pkt->nbi_meta.pkt_info.muptr << 11) + 256);
        dma_op(pkt_mu_buffer, buf_transfer_len, pcie_addr + ctm_transfer_len, NFP_PCIE_DMA_FROMPCI_HI, &buf_cmpl_sig);

        wait_for_all(&ctm_cmpl_sig.even, &ctm_cmpl_sig.odd, &buf_cmpl_sig.even, &buf_cmpl_sig.odd);
        return;
    }

    wait_for_all(&ctm_cmpl_sig.even, &ctm_cmpl_sig.odd);
}