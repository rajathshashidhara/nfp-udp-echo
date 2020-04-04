/* Flowenv */
#include <nfp.h>
#include <stdint.h>

#include <pkt/pkt.h>
#include <net/eth.h>
#include <nfp/mem_atomic.h>
#include <nfp/mem_bulk.h>
#include <nfp/me.h>
#include <pcie/compat.h>
#include <pcie/pcie.h>
#include <std/reg_utils.h>
#include <blm.h>

/*
 * Mapping between channel and TM queue
 */
#ifndef NBI
#define NBI 0
#endif

/* CTM credit defines */
#define MAX_ME_CTM_PKT_CREDITS  256
#define MAX_ME_CTM_BUF_CREDITS  32
#define CTM_ALLOC_ERR 0xffffffff

/* counters for out of credit situations */
volatile __export __mem uint64_t gen_pkt_ctm_wait;
volatile __export __mem uint64_t gen_pkt_blm_wait;

__export __shared __cls struct ctm_pkt_credits ctm_credits_2 =
           {MAX_ME_CTM_PKT_CREDITS, MAX_ME_CTM_BUF_CREDITS};

/* DEBUG MACROS */
__volatile __shared __emem uint32_t debug[8192*64];
__volatile __shared __emem uint32_t debug_idx;

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

#define DEBUG_MEM(_a, _len) do { \
    int i = 0; \
    __xrw uint32_t _idx_val = 4; \
    __xwrite uint32_t _dvals[4]; \
    mem_test_add(&_idx_val, \
            (__mem40 void *)&debug_idx, sizeof(_idx_val)); \
    _dvals[0] = 0x87654321; \
    _dvals[1] = (uint64_t)_a >> 32; \
    _dvals[2] = (uint64_t)_a & 0xffffffff; \
    _dvals[3] = _len; \
    mem_write_atomic(_dvals, (__mem40 void *)\
                    (debug + (_idx_val % (1024 * 64))), sizeof(_dvals)); \
    for (i = 0; i < (_len+15)/16; i++) { \
      mem_test_add(&_idx_val, \
              (__mem40 void *)&debug_idx, sizeof(_idx_val)); \
      _dvals[0] = i*16 < _len ? *((__mem40 uint32_t *)_a+i*4) : 0x12345678; \
      _dvals[1] = i*16 + 4 < _len ? *((__mem40 uint32_t *)_a+i*4+1) : 0x12345678; \
      _dvals[2] = i*16 + 8 < _len ? *((__mem40 uint32_t *)_a+i*4+2) : 0x12345678; \
      _dvals[3] = i*16 + 12 < _len ? *((__mem40 uint32_t *)_a+i*4+3) : 0x12345678; \
      mem_write_atomic(_dvals, (__mem40 void *)\
                      (debug + (_idx_val % (1024 * 64))), sizeof(_dvals)); \
    } } while(0)

#define UDP_PACKET_SZ_BYTES 64

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

__declspec(shared export mem) volatile uint32_t start = 0;
__declspec(shared mem) volatile uint64_t tx_buf_start;
__declspec(shared mem) volatile struct ring_meta tx_meta;

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

void dma_packet_recv(__mem40 void *addr, uint8_t dma_len) {
    __gpr struct nfp_pcie_dma_cmd dma_cmd;
    __xwrite struct nfp_pcie_dma_cmd dma_cmd_rd;
    SIGNAL cmpl_sig, enq_sig;

    pcie_dma_setup(&dma_cmd,
        __signal_number(&cmpl_sig),
        dma_len,
        (uint32_t)((unsigned long long)addr >> 32), addr);

    dma_cmd.pcie_addr_hi = tx_meta.head >> 32;
    dma_cmd.pcie_addr_lo = tx_meta.head & 0xffffffff;

    dma_cmd_rd = dma_cmd;
    __pcie_dma_enq(0, &dma_cmd_rd, NFP_PCIE_DMA_FROMPCI_HI,
                     sig_done, &enq_sig);
    wait_for_all(&cmpl_sig, &enq_sig);

    tx_meta.head += dma_len;
    if (tx_meta.head == (tx_buf_start + tx_meta.len)) {
        tx_meta.head = tx_buf_start;
    }
}

static void build_tx_meta(__imem struct nbi_meta_catamaran *nbi_meta)
{
    __xread blm_buf_handle_t buf;
    int pkt_num;
    int blq = 0;

    // reg_zero(nbi_meta->__raw, sizeof(struct nbi_meta_catamaran));

    /*
     * Poll for a CTM buffer until one is returned
     */
    while (1) {
        pkt_num = pkt_ctm_alloc(&ctm_credits_2, __ISLAND, PKT_CTM_SIZE_256, 1, 0);
        if (pkt_num != CTM_ALLOC_ERR)
            break;
        sleep(1000);
        mem_incr64((__mem void *) gen_pkt_ctm_wait);
    }
    /*
     * Poll for MU buffer until one is returned.
     */
    while (blm_buf_alloc(&buf, blq) != 0) {
        sleep(1000);
        mem_incr64((__mem void *) gen_pkt_blm_wait);
    }

    nbi_meta->pkt_info.isl = __ISLAND;
    nbi_meta->pkt_info.pnum = pkt_num;
    nbi_meta->pkt_info.bls = blq;
    nbi_meta->pkt_info.muptr = buf;

    /* all other fields in the nbi_meta struct are left zero */
}

int main(void)
{
    int pkt_num, pkt_off;
    __xread blm_buf_handle_t buf;
    int blq = 0, i, q_dst;
    __mem40 char *pbuf;

    // Required for packet sending.
    __gpr struct pkt_ms_info msi_gen;
    __imem struct nbi_meta_catamaran nbi_meta_gen;

    if (ctx() != 0) {
        return 0;
    }

    while (start != 1) {
    }

    tx_buf_start = tx_meta.head;
    pkt_off = PKT_NBI_OFFSET + MAC_PREPEND_BYTES;

    DEBUG(0x1234567, 0x1234567, 0x1234567, 0x1234567);

    for (;;) {
        // Allocate a packet

        /*
         * Poll for a CTM buffer until one is returned
         */
        while (1) {
            pkt_num = pkt_ctm_alloc(&ctm_credits_2, __ISLAND, PKT_CTM_SIZE_256, 1, 0);
            if (pkt_num != CTM_ALLOC_ERR)
                break;
            sleep(1000);
            mem_incr64((__mem void *) gen_pkt_ctm_wait);
        }

        /*
         * Poll for MU buffer until one is returned.
         */
        while (blm_buf_alloc(&buf, blq) != 0) {
            sleep(1000);
            mem_incr64((__mem void *) gen_pkt_blm_wait);
        }

        pbuf   = pkt_ctm_ptr40(__ISLAND, pkt_num, 0);

        DEBUG_MEM(pbuf, 256);

        // Prepare the metadata in the CTM buffer
        nbi_meta_gen.pkt_info.isl = __ISLAND;
        nbi_meta_gen.pkt_info.pnum = pkt_num;
        nbi_meta_gen.pkt_info.bls = blq;
        nbi_meta_gen.pkt_info.muptr = buf;
        nbi_meta_gen.pkt_info.len = 8 + UDP_PACKET_SZ_BYTES;
        nbi_meta_gen.port = 3; // Correct?

        for (i=0; i<sizeof(struct nbi_meta_catamaran); i++) {
          *(pbuf+i) = *((__mem40 char*)&nbi_meta_gen + i);
        }

        while (1) {

            // Now check for a packet in TX ring and send.
            if (tx_meta.head < tx_meta.tail) {
                if (tx_meta.tail - tx_meta.head >= UDP_PACKET_SZ_BYTES) {
                    // Can DMA
                    // 76 bytes assuming 40 bytes of padding and 12 bytes of MAC prepend. Look out for this while testing.
                    dma_packet_recv(pbuf+pkt_off+4, UDP_PACKET_SZ_BYTES);
                    break;
                }
            } else {
                if (tx_meta.head == tx_meta.tail) {
                    // TODO - This can happen only when TXing the first packet. Post
                    // that, the user driver will alawys ensure tail is never equal to head. So, optimize this check?
                } else {
                    if (((tx_buf_start + tx_meta.len) - tx_meta.head) + (tx_meta.tail - tx_buf_start) >= UDP_PACKET_SZ_BYTES) {
                        // Can DMA in 2 parts
                        dma_packet_recv(pbuf+pkt_off+4, (tx_buf_start + tx_meta.len) - tx_meta.head);
                        dma_packet_recv(pbuf+pkt_off+4+(tx_buf_start + tx_meta.len) - tx_meta.head,
                            UDP_PACKET_SZ_BYTES - ((tx_buf_start + tx_meta.len) - tx_meta.head));
                        break;
                    }
                }
            }
        }

        DEBUG_MEM(pbuf, 256);
        q_dst  = PORT_TO_CHANNEL(nbi_meta_gen.port);
        pkt_mac_egress_cmd_write(pbuf, pkt_off, 0, 0); // Write data to make the packet MAC egress generate L3 and L4 checksums

        msi_gen = pkt_msd_write(pbuf, pkt_off); // Write a packet modification script of NULL
        pkt_nbi_send(__ISLAND,
                     pkt_num,
                     &msi_gen,
                     UDP_PACKET_SZ_BYTES,
                     NBI,
                     q_dst,
                     nbi_meta_gen.seqr,
                     nbi_meta_gen.seq,
                     PKT_CTM_SIZE_256);
    }

    return 0;
}

/* -*-  Mode:C; c-basic-offset:4; tab-width:4 -*- */

