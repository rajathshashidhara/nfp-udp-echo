#ifndef ME_DMA_H
#define ME_DMA_H

#include <nfp.h>
#include <pktdef.h>

void dma_send(__mem40 void* addr,
                uint32_t len,
                uint64_t pcie_addr);
void dma_recv(__mem40 void* addr,
                uint32_t len,
                uint64_t pcie_addr);

void dma_packet_send(struct pkt_t* pkt, uint64_t pcie_addr);
void dma_packet_recv(struct pkt_t* pkt, uint64_t pcie_addr);

#endif /* ME_DMA_H */
