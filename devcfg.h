#ifndef UDP_ECHO_CFG_H
#define UDP_ECHO_CFG_H

#include <stdint.h>

#if defined(__NFP_LANG_MICROC)
#include <nfp.h>
__packed struct device_meta_t
#else
struct __attribute__((packed)) device_meta_t
#endif
{
    /* Configuration */
    uint64_t rx_buffer_iova;
    uint64_t tx_buffer_iova;
    uint32_t buffer_size;
    uint32_t packet_size;
    uint64_t start_signal;

    /* RX/TX ring buffers */
    uint32_t rx_head;
    uint32_t rx_tail;
    uint32_t tx_head;
    uint32_t tx_tail;
};

#endif /* UDP_ECHO_CFG_H */
