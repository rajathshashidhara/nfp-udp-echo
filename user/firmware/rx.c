/* Flowenv */
#include <nfp.h>
#include <stdint.h>

#include <pkt/pkt.h>
#include <net/eth.h>
#include <nfp/mem_atomic.h>
#include <nfp/mem_bulk.h>
#include <pcie/compat.h>
#include <pcie/pcie.h>

/*
 * Mapping between channel and TM queue
 */
#ifndef NBI
#define NBI 0
#endif

/* DEBUG MACROS */

__volatile __shared __emem uint32_t debug[8192*64]; // _export
__volatile __shared __emem uint32_t debug_idx; // __export
#define DEBUG(_a, _b, _c, _d) do { \
    __xrw uint32_t _idx_val = 4; \
    __xwrite uint32_t _dvals[4]; \
    mem_test_add(&_idx_val, \
            (__mem40 void *)&debug_idx, sizeof(_idx_val)); \
    _dvals[0] = _a; \
    _dvals[1] = _b; \
    _dvals[2] = _c; \
    _dvals[3] = _d; \
    mem_write_atomic(_dvals, (__mem40 void *)\
                    (debug + (_idx_val % (1024 * 64))), sizeof(_dvals)); \
    } while(0)

// __declspec(shared ctm) is one copy shared by all threads in an ME, in CTM
// __declspec(shared export ctm) is one copy shared by all MEs in an island in CTM (CTM default scope for 'export' of island)
// __declspec(shared export imem) is one copy shared by all MEs on the chip in IMU (IMU default scope for 'export' of global)

struct pkt_hdr {
    struct {
        uint32_t mac_timestamp;
        uint32_t mac_prepend;
    };
    struct eth_vlan_hdr pkt;
};

struct pkt_rxed {
    struct nbi_meta_catamaran nbi_meta;
    struct pkt_hdr            pkt_hdr;
};

struct ring_meta {
    uint64_t head;
    uint64_t tail;
    uint64_t len;
};

void
receive_packet( struct pkt_rxed *pkt_rxed,
                size_t size )
{
    __xread struct pkt_rxed pkt_rxed_in;
    int island, pnum;
    int pkt_off;
    __mem40 struct pkt_hdr *pkt_hdr;

    pkt_nbi_recv(&pkt_rxed_in, sizeof(pkt_rxed->nbi_meta));
    pkt_rxed->nbi_meta = pkt_rxed_in.nbi_meta;

    pkt_off  = PKT_NBI_OFFSET;
    island   = pkt_rxed->nbi_meta.pkt_info.isl;
    pnum     = pkt_rxed->nbi_meta.pkt_info.pnum;
    pkt_hdr  = pkt_ctm_ptr40(island, pnum, pkt_off);

    mem_read32(&(pkt_rxed_in.pkt_hdr), pkt_hdr, sizeof(pkt_rxed_in.pkt_hdr));
    pkt_rxed->pkt_hdr = pkt_rxed_in.pkt_hdr;
}

void
drop_packet( struct nbi_meta_catamaran *nbi_meta)
{
    int island, pnum;
    island = nbi_meta->pkt_info.isl;
    pnum   = nbi_meta->pkt_info.pnum;
    pkt_ctm_free(island, pnum);
}

/*
 * Fill out all the common parts of the DMA command structure, plus
 * other setup required for DMA engines tests.
 *
 * Once setup, the caller only needs to patch in the PCIe address and
 * is ready to go.
 */
__intrinsic static void
pcie_dma_setup(__gpr struct nfp_pcie_dma_cmd *cmd,
               int signo, uint32_t len, uint32_t dev_addr_hi, uint32_t dev_addr_lo)
{
    unsigned int meid = __MEID;

    struct nfp_pcie_dma_cfg cfg;
    __xwrite struct nfp_pcie_dma_cfg cfg_wr;
    unsigned int mode_msk_inv;
    unsigned int mode;

    /* Zero the descriptor. Same size for 3200 and 6000 */
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

__declspec(shared export mem) volatile uint32_t start = 0;
__declspec(shared mem) volatile uint64_t rx_buf_start;
__declspec(shared mem) volatile struct ring_meta rx_meta, tx_meta;

void dma_packet(__mem40 void *addr, uint8_t dma_len) {
    __gpr struct nfp_pcie_dma_cmd dma_cmd;
    __xwrite struct nfp_pcie_dma_cmd dma_cmd_wr;
    SIGNAL cmpl_sig, enq_sig;

    pcie_dma_setup(&dma_cmd,
        __signal_number(&cmpl_sig),
        dma_len,
        (uint32_t)((unsigned long long)addr >> 32), addr);

    dma_cmd.pcie_addr_hi = rx_meta.tail >> 32;
    dma_cmd.pcie_addr_lo = rx_meta.tail & 0xffffffff;

    dma_cmd_wr = dma_cmd;
    __pcie_dma_enq(0, &dma_cmd_wr, NFP_PCIE_DMA_TOPCI_HI,
                     sig_done, &enq_sig);
    wait_for_all(&cmpl_sig, &enq_sig);

    rx_meta.tail += dma_len;
    if (rx_meta.tail == (rx_buf_start + rx_meta.len)) {
        rx_meta.tail = rx_buf_start;
    }
}

int
main(void)
{
    struct pkt_rxed pkt_rxed; /* The packet header received by the thread */
    __mem40 void *pkt_off;    /* The packet in the CTM */
    uint8_t pkt_len, first_dma_len;
    uint64_t temp1, temp2;

    if (ctx() != 0) {
        return 0;
    }

    while (start != 1) {
    }

    rx_buf_start = rx_meta.head;
    // rx_meta.len = 512;
    DEBUG(0x1234567, 0x1234567, 0x1234567, 0x1234567);

    for (;;) {
        /* Receive a packet */
        receive_packet(&pkt_rxed, sizeof(pkt_rxed));
        pkt_off  = pkt_ctm_ptr40(pkt_rxed.nbi_meta.pkt_info.isl,
          pkt_rxed.nbi_meta.pkt_info.pnum,
          PKT_NBI_OFFSET+2*MAC_PREPEND_BYTES);

        // TODO - Fix the function for dropping packets.
        if (pkt_rxed.pkt_hdr.pkt.tpid==0x88cc) {
          // Drop LLDP packets.
          drop_packet(&pkt_rxed.nbi_meta);
          continue;
        }

        pkt_len = pkt_ctm_data_size(pkt_rxed.nbi_meta.pkt_info.len, PKT_NBI_OFFSET, PKT_CTM_SIZE_256) - 2*MAC_PREPEND_BYTES;

        while (1) {
            if (rx_meta.head <= rx_meta.tail) {
                if (rx_meta.len - (rx_meta.tail - rx_meta.head) > pkt_len) {
                  // Can DMA.
                  break;
                }
            } else {
                if (rx_meta.head - rx_meta.tail > pkt_len) {
                  // Can DMA
                  break;
                }
            }
        }

        first_dma_len = pkt_len;
        temp1 = rx_meta.tail + pkt_len;
        temp2 = rx_buf_start + rx_meta.len;

        if ((rx_meta.tail + (uint64_t)pkt_len) > (rx_buf_start + (uint64_t)rx_meta.len)) {
            first_dma_len = rx_buf_start + rx_meta.len - rx_meta.tail;
        }

        dma_packet(pkt_off, first_dma_len);
        if (first_dma_len != pkt_len)
            dma_packet((unsigned long long)pkt_off + first_dma_len, pkt_len-first_dma_len);

        pkt_ctm_free(pkt_rxed.nbi_meta.pkt_info.isl, pkt_rxed.nbi_meta.pkt_info.pnum);
    }

    return 0;
}

/* -*-  Mode:C; c-basic-offset:4; tab-width:4 -*- */
