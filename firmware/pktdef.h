#ifndef ME_TYPES_H_
#define ME_TYPES_H_

#include <stdint.h>

#include <pkt/pkt.h>
#include <net/eth.h>
#include <net/ip.h>
#include <net/udp.h>

struct pkt_t
{
    struct nbi_meta_catamaram nbi_meta;
    struct pkt_hdr_t hdr;
};

__packed struct pkt_hdr_t
{
    struct
    {
        uint32_t mac_timestamp;
        uint32_t mac_prepend;
    };
    struct eth_hdr eth;
    struct ip4_hdr ip;
    struct udp_hdr udp;
};

#endif /* ME_TYPES_H_ */