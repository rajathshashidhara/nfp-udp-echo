#ifndef ME_RXOPS_H
#define ME_RXOPS_H

#include <stdint.h>
#include <nfp.h>

#include "pktdef.h"

enum
{
    ALLOW,
    DROP
};

extern __mem40 void* receive_packet_with_hdrs(struct pkt_t* pkt);
extern __mem40 void* receive_packet(struct pkt_t* pkt);
extern void read_packet_header(struct pkt_t* pkt);
extern void write_packet_header(struct pkt_t* pkt);
extern int filter_packets(struct pkt_t* pkt);
extern void modify_packet_header(struct pkt_t* pkt);
extern void free_packet(struct pkt_t* pkt);
extern void drop_packet(struct pkt_t* pkt);


#endif /* ME_RXOPS_H */