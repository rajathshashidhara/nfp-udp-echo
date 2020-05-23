#ifndef ME_TYPES_H_
#define ME_TYPES_H_

#include <stdint.h>

#include <pkt/pkt.h>
#include <net/eth.h>
#include <net/ip.h>
#include <net/udp.h>
#include <net/icmp.h>

__packed struct pkt_hdr_t
{
    struct
    {
        uint32_t mac_timestamp;
        uint32_t mac_prepend;
    };
    struct eth_hdr eth;
    struct ip4_hdr ip;
    union
    {
        struct udp_hdr udp;
        struct icmp_hdr icmp;
    }
    uint8_t pad[2];
};

__packed struct pkt_t
{
    struct nbi_meta_catamaran nbi_meta;
    struct pkt_hdr_t hdr;
};

#endif /* ME_TYPES_H_ */