#ifndef UDP_ECHO_CFG_H
#define UDP_ECHO_CFG_H

#include <stdint.h>

struct device_meta_t
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
} __attribute__((__packed__));

#endif /* UDP_ECHO_CFG_H */
