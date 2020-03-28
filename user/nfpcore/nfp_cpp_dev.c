#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#define __USE_GNU
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/socket.h>
#include <poll.h>
#include <asm-generic/ioctl.h>
#include <unistd.h>
#include <linux/limits.h>

#include "nfp_ioctl.h"
#include "nfp_cpp.h"
#include "nfp_nsp.h"
#include "nfp_nffw.h"
#include "nfp_cpp_dev.h"

static int nfp_cpp_dev_handle_cmd(struct nfp_cpp_dev_data* data, int fd)
{
    uint64_t tmp;
    size_t count;
    off_t offset;
    unsigned long request;
    int ret;

    ret = read(fd, &tmp, sizeof(tmp));
    if (ret < 0)
        return ret;

    switch(tmp)
    {
    case OP_PREAD:
        ret = read(fd, &tmp, sizeof(tmp));
        if (ret < 0)
            return ret;
        count = (size_t) tmp;

        ret = read(fd, &tmp, sizeof(tmp));
        if (ret < 0)
            return ret;
        offset = (off_t) tmp;

        return nfp_cpp_dev_read(data, fd, count, offset);
    case OP_PWRITE:
        ret = read(fd, &tmp, sizeof(tmp));
        if (ret < 0)
            return ret;
        count = (size_t) tmp;

        ret = read(fd, &tmp, sizeof(tmp));
        if (ret < 0)
            return ret;
        offset = (off_t) tmp;

        return nfp_cpp_dev_write(data, fd, count, offset);
    case OP_IOCTL:
        ret = read(fd, &tmp, sizeof(tmp));
        if (ret < 0)
            return ret;
        request = (unsigned long) tmp;

        return nfp_cpp_dev_ioctl(data, fd, request);
    default:
        return -1;
    }
    return 0;
}

static int nfp_cpp_dev_poll(struct nfp_cpp_dev_data* data)
{
    int ret;
    int i, j;
    int fd;
    int close_count;

    while (1)
    {
        /* Infinite timeout */
        ret = poll(data->connections, data->count, -1);
        if (ret <= 0)
        {
            fprintf(stderr, "%s:%d\n", __func__, __LINE__);
            continue;
        }

        if (data->connections[0].revents & (POLLIN))
        {
            if (data->count < MAX_CONNECTIONS)
            {
                fd = accept(data->listen_fd, NULL, NULL);
                data->connections[data->count].fd = fd;
                data->connections[data->count].events = (POLLIN);
                data->count++;
            }
        }

        close_count = 0;
        for (i = 1; i < data->count; i++)
        {
            if (data->connections[i].revents == 0)
                continue;

            if (data->connections[i].revents & (POLLIN))
            {
                ret = nfp_cpp_dev_handle_cmd(data, data->connections[i].fd);
                if (ret < 0)
                {
                    fprintf(stderr, "%s():%d\n", __func__, __LINE__);
                    close(data->connections[i].fd);
                    data->connections[i].fd = -1;
                    close_count++;
                }
            }

            if (data->connections[i].revents & (POLLHUP))
            {
                close(data->connections[i].fd);
                data->connections[i].fd = -1;
                close_count++;
            }
        }

        /* Cleanup list */
        if (close_count)
        {
            j = 1;
            for (i = 1; i < data->count; i++)
            {
                if (data->connections[i].fd != -1)
                {
                    data->connections[j].fd = data->connections[i].fd;
                    data->connections[j].events = (POLLIN);

                    j++;
                }
            }

            data->count = j;
        }
    }

    return 0;
}

int nfp_cpp_dev_main(struct nfp_cpp* cpp)
{
    int ret;
    struct sockaddr address;
    struct nfp_cpp_dev_data* data =
            malloc(sizeof(struct nfp_cpp_dev_data));
    if (!data)
    {
        fprintf(stderr, "%s():%d\n", __func__, __LINE__);
        return -1;
    }

    unlink("/tmp/nfp_cpp");
    data->cpp = cpp;
    memset(data->connections, 0, sizeof(data->connections));
    data->listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	memset(&address, 0, sizeof(struct sockaddr));
	address.sa_family = AF_UNIX;
	strcpy(address.sa_data, "/tmp/nfp_cpp");
	ret = bind(data->listen_fd, (const struct sockaddr *)&address,
		   sizeof(struct sockaddr));
    if (ret < 0)
    {
        fprintf(stderr, "%s():%d\n", __func__, __LINE__);
        close(data->listen_fd);
        return -1;
    }
    ret = listen(data->listen_fd, LISTEN_BACKLOG);
    if (ret < 0)
    {
        fprintf(stderr, "%s():%d\n", __func__, __LINE__);
        close(data->listen_fd);
        return -1;
    }

    data->connections[0].fd = data->listen_fd;
    data->connections[0].events = (POLLIN);
    data->count = 1;

    return nfp_cpp_dev_poll(data);
}