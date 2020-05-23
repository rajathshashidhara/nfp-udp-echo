#ifndef ME_TXOPS_H
#define ME_TXOPS_H

#include <stdint.h>
#include <nfp.h>

#include "pktdef.h"

extern __mem40 void* allocate_packet(struct pkt_t* pkt);
extern void send_packet(struct pkt_t* pkt);

#endif /* ME_TXOPS_H */