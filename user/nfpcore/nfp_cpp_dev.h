#ifndef _NFP_CPP_DEV_OPS_H_
#define _NFP_CPP_DEV_OPS_H_

#include <poll.h>

#include "nfp_cpp.h"
#include "nfp_ioctl.h"

#define LISTEN_BACKLOG 8
#define MAX_CONNECTIONS 64

#define NFP_CPP_MEMIO_BOUNDARY		(1 << 20)

enum op_type {
    OP_UNUSED,
    OP_PREAD,
    OP_PWRITE,
    OP_IOCTL
};

struct nfp_cpp_dev_data
{
    struct nfp_cpp* cpp;
    int listen_fd;
    struct pollfd connections[MAX_CONNECTIONS];
    int count;
    char firmware[NFP_FIRMWARE_MAX];
};

int nfp_cpp_dev_ioctl(struct nfp_cpp_dev_data* data, int fd,
                unsigned long request);
int nfp_cpp_dev_write(struct nfp_cpp_dev_data* data,
        int fd, size_t count, off_t offset);
int nfp_cpp_dev_read(struct nfp_cpp_dev_data* data,
        int fd, size_t count, off_t offset);

#endif /* _NFP_CPP_DEV_OPS_H_ */