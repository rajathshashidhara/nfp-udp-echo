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

static int nfp_cpp_dev_read(struct nfp_cpp_dev_data* data,
        int fd, size_t count, off_t offset)
{
    char* buf = (char*) malloc(count);
	struct nfp_cpp_area *area;
	off_t nfp_offset;
	uint32_t cpp_id, pos, len;
	size_t curlen, totlen = 0;
	int err = 0;
    uint64_t tmp;

	/* Obtain target's CPP ID and offset in target */
	cpp_id = (offset >> 40) << 8;
	nfp_offset = offset & ((1ull << 40) - 1);

	/* Adjust length if not aligned */
	if (((nfp_offset + (off_t)count - 1) & ~(NFP_CPP_MEMIO_BOUNDARY - 1)) !=
	    (nfp_offset & ~(NFP_CPP_MEMIO_BOUNDARY - 1))) {
		curlen = NFP_CPP_MEMIO_BOUNDARY -
			(nfp_offset & (NFP_CPP_MEMIO_BOUNDARY - 1));
	}

    curlen = count;
	while (count > 0) {
		area = nfp_cpp_area_alloc_with_name(data->cpp, cpp_id, "nfp.cdev",
						    nfp_offset, curlen);
		if (!area) {
			err = -EIO;
            goto handle_read_error;
		}

		err = nfp_cpp_area_acquire(area);
		if (err < 0) {
			nfp_cpp_area_free(area);
			err = -EIO;
            goto handle_read_error;
		}

		for (pos = 0; pos < curlen; pos += len) {
			len = curlen - pos;
			if (len > count)
				len = count;

			err = nfp_cpp_area_read(area, pos, buf + totlen + pos, len);
			if (err < 0) {
				nfp_cpp_area_release(area);
				nfp_cpp_area_free(area);
                err = -EIO;
                goto handle_read_error;
			}
		}

		nfp_offset += pos;
		totlen += pos;
		nfp_cpp_area_release(area);
		nfp_cpp_area_free(area);

		count -= pos;
		curlen = (count > NFP_CPP_MEMIO_BOUNDARY) ?
			NFP_CPP_MEMIO_BOUNDARY : count;
	}

    tmp = (uint64_t) totlen;
    err = write(fd, &tmp, sizeof(tmp));
    if (err < sizeof(tmp))
    {
        free(buf);
        return -1;
    }

    curlen = 0;
    while (curlen < totlen)
    {
        err = write(fd, buf + curlen, totlen - curlen);
        if (err < 0)
        {
            free(buf);
            return -1;
        }

        curlen += err;
    }
    free(buf);
    return 0;

handle_read_error:
    free(buf);
    tmp = (uint64_t) err;
    err = write(fd, &tmp, sizeof(tmp));
    if (err < 0)
    {
        return -1;
    }

    return 0;
}

static int nfp_cpp_dev_write(struct nfp_cpp_dev_data* data,
        int fd, size_t count, off_t offset)
{
    char* buf = (char*) malloc(count);
	struct nfp_cpp_area *area;
	off_t nfp_offset;
	uint32_t cpp_id, pos, len;
	size_t curlen, totlen = 0;
	int err = 0;
    uint64_t tmp;

	/* Obtain target's CPP ID and offset in target */
	cpp_id = (offset >> 40) << 8;
	nfp_offset = offset & ((1ull << 40) - 1);

    curlen = 0;
	/* Adjust length if not aligned */
	if (((nfp_offset + (off_t)count - 1) & ~(NFP_CPP_MEMIO_BOUNDARY - 1)) !=
	    (nfp_offset & ~(NFP_CPP_MEMIO_BOUNDARY - 1))) {
		curlen = NFP_CPP_MEMIO_BOUNDARY -
			(nfp_offset & (NFP_CPP_MEMIO_BOUNDARY - 1));
	}

    while (curlen < count)
    {
        err = read(fd, buf + curlen, count - curlen);
        if (err < 0)
        {
            free(buf);
            return -1;
        }
        curlen += err;
    }

	while (count > 0) {
		/* configure a CPP PCIe2CPP BAR for mapping the CPP target */
		area = nfp_cpp_area_alloc_with_name(data->cpp, cpp_id, "nfp.cdev",
						    nfp_offset, curlen);
		if (!area) {
            err = EIO;
			goto handle_write_error;
		}

		/* mapping the target */
		err = nfp_cpp_area_acquire(area);
		if (err < 0) {
			nfp_cpp_area_free(area);
            err = EIO;
			goto handle_write_error;
		}

		for (pos = 0; pos < curlen; pos += len) {
			len = curlen - pos;
			err = nfp_cpp_area_write(area, pos, buf + totlen + pos, len);
			if (err < 0) {
				nfp_cpp_area_release(area);
				nfp_cpp_area_free(area);
                err = EIO;
                goto handle_write_error;
			}
		}

		nfp_offset += pos;
		totlen += pos;
		nfp_cpp_area_release(area);
		nfp_cpp_area_free(area);

		count -= pos;
		curlen = (count > NFP_CPP_MEMIO_BOUNDARY) ?
			 NFP_CPP_MEMIO_BOUNDARY : count;
	}

    tmp = (uint64_t) totlen;
    err = write(fd, &tmp, sizeof(tmp));
    if (err < sizeof(tmp))
    {
        free(buf);
        return -1;
    }
    free(buf);
    return 0;

handle_write_error:
    free(buf);
    tmp = (uint64_t) err;
    err = write(fd, &tmp, sizeof(tmp));
    if (err < 0)
    {
        return -1;
    }

    return 0;
}
/*
#define DEFAULT_FW_PATH       "/lib/firmware/netronome"
static int do_load_fw(struct nfp_cpp_dev_data* data)
{
    int fw_f;
    char *fw_buf;
    struct stat file_stat;
    off_t fsize, bytes;
    char path[PATH_MAX];

    struct nfp_nsp* nsp = nfp_nsp_open(data->cpp);
    if (!nsp)
    {
        return -EIO;
    }
    snprintf(path, PATH_MAX, "%s/%s", DEFAULT_FW_PATH, data->firmware);
    fw_f = open(path, O_RDONLY);
    if (fw_f < 0)
        return -ENOENT;

    if (fstat(fw_f, &file_stat) < 0) {
        close(fw_f);
        return -ENOENT;
    }

    fsize = file_stat.st_size;
    fw_buf = malloc((size_t)fsize);
    if (!fw_buf) {
        close(fw_f);
        return -ENOMEM;
    }
    memset(fw_buf, 0, fsize);
    bytes = read(fw_f, fw_buf, fsize);
    if (bytes != fsize) {
        free(fw_buf);
        close(fw_f);
        return -EIO;
    }

    nfp_nsp_load_fw(nsp, fw_buf, bytes);
    nfp_nsp_close(nsp);

    return 0;
}
*/

static int nfp_cpp_dev_ioctl(struct nfp_cpp_dev_data* data, int fd,
                unsigned long request)
{

    struct nfp_cpp_area_request area_req;
    struct nfp_cpp_event_request event_req;
    struct nfp_cpp_explicit_request explicit_req;
    struct nfp_cpp_identification ident;
    uint64_t arg_size, temp;
    const uint8_t* serial;
    int ret;

    switch(request)
    {
    case NFP_IOCTL_CPP_IDENTIFICATION:
        arg_size = sizeof(ident.size);
        ret = read(fd, &ident.size, arg_size);
        if (ret < 0)
            return -1;
        ident.model = nfp_cpp_model(data->cpp);
        ident.interface = data->cpp->interface;
		nfp_cpp_serial(data->cpp, &serial);
		ident.serial_hi = (serial[0] <<  8) |
				   (serial[1] <<  0);
		ident.serial_lo = (serial[2] << 24) |
				   (serial[3] << 16) |
				   (serial[4] <<  8) |
				   (serial[5] <<  0);

        temp = sizeof(ident);
        ret = write(fd, &temp, sizeof(temp));
        if (ret < sizeof(temp))
            return -1;

        ret = write(fd, &ident, sizeof(ident));
        if (ret < sizeof(ident))
            return -1;
        break;
/*
    case NFP_IOCTL_FIRMWARE_LOAD:
        arg_size = NFP_FIRMWARE_MAX;
        ret = read(fd, data->firmware, NFP_FIRMWARE_MAX);
        if (ret < 0)
            return -1;

        fprintf(stderr, "Firmware: %s\n", data->firmware);
        ret = do_load_fw(data);

        temp = ret;
        ret = write(fd, &temp, sizeof(temp));
        if (ret < sizeof(temp))
            return -1;
        break;

    case NFP_IOCTL_FIRMWARE_LAST:
        temp = 0;
        ret = write(fd, &temp, sizeof(temp));
        if (ret < sizeof(temp))
            return -1;

        ret = write(fd, data->firmware, sizeof(data->firmware));
        if (ret < sizeof(data->firmware))
            return -1;
        break;

    case NFP_IOCTL_CPP_AREA_REQUEST:
*/
    default:
        return -1;

    }
    
    return 0;
}

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