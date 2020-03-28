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

#include "nfp_nsp.h"
#include "nfp_nffw.h"
#include "nfp_cpp_dev.h"

static void do_cpp_identification(struct nfp_cpp* cpp,
            struct nfp_cpp_identification* ident)
{
    int total;

	total = offsetof(struct nfp_cpp_identification, size)
		+ sizeof(ident->size);

	if (ident->size >= offsetof(struct nfp_cpp_identification, model)
			+ sizeof(ident->model)) {
		ident->model = nfp_cpp_model(cpp);
		total = offsetof(struct nfp_cpp_identification, model)
			+ sizeof(ident->model);
	}
	if (ident->size >= offsetof(struct nfp_cpp_identification, interface)
			+ sizeof(ident->interface)) {
		ident->interface = nfp_cpp_interface(cpp);
		total = offsetof(struct nfp_cpp_identification, interface)
			+ sizeof(ident->interface);
	}
	if (ident->size >= offsetof(struct nfp_cpp_identification, serial_hi)
			+ sizeof(ident->serial_hi)) {
		const uint8_t *serial;

		nfp_cpp_serial(cpp, &serial);
		ident->serial_hi = (serial[0] <<  8) |
				   (serial[1] <<  0);
		ident->serial_lo = (serial[2] << 24) |
				   (serial[3] << 16) |
				   (serial[4] <<  8) |
				   (serial[5] <<  0);
		total = offsetof(struct nfp_cpp_identification, serial_hi)
			+ sizeof(ident->serial_hi);
	}

	/* Modify size to our actual size */
	ident->size = total;
}

static int ioctl_cpp_identification(struct nfp_cpp_dev_data* data,
                int fd)
{
    struct nfp_cpp_identification ident;
    uint64_t temp;
    int ret;

    /* Read size */
    ret = read(fd, &ident.size, sizeof(ident.size));
    if (ret < sizeof(ident.size))
        return -1;
    
    do_cpp_identification(data->cpp, &ident);

    /* Send return value */
    temp = sizeof(ident);
    ret = write(fd, &temp, sizeof(temp));
    if (ret < sizeof(temp))
        return -1;

    /* Send out params */
    ret = write(fd, &ident, sizeof(ident));
    if (ret < sizeof(ident))
        return -1;

    return 0;
}

int nfp_cpp_dev_ioctl(struct nfp_cpp_dev_data* data, int fd,
                unsigned long request)
{
    switch(request)
    {
    case NFP_IOCTL_CPP_IDENTIFICATION:
        return ioctl_cpp_identification(data, fd);
    
    default:
        fprintf(stderr, "%s(): Invalid request type\n", __func__);
        return -1;
    }

    return 0;
}

int nfp_cpp_dev_write(struct nfp_cpp_dev_data* data,
        int fd, size_t count, off_t offset)
{
    char* buf = (char*) malloc(count);
	char* buff = buf;
	struct nfp_cpp_area *area;
	off_t nfp_offset;
	uint32_t cpp_id, pos, len;
	size_t curlen, totlen = 0;
	int err = 0;
    uint64_t tmp;
	uint32_t tmpbuf[16];

	/* Read data from user */
	curlen = 0;
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
		/* configure a CPP PCIe2CPP BAR for mapping the CPP target */
		area = nfp_cpp_area_alloc_with_name(data->cpp, cpp_id, "nfp.cdev",
						    nfp_offset, curlen);
		if (!area) {
            err = -EIO;
			goto handle_write_error;
		}

		err = nfp_cpp_area_acquire(area);
		if (err < 0) {
			nfp_cpp_area_free(area);
            err = -EIO;
			goto handle_write_error;
		}

		for (pos = 0; pos < curlen; pos += len) {
			len = curlen - pos;
			if (len > sizeof(tmpbuf))
				len = sizeof(tmpbuf);

			memcpy(tmpbuf, buff + pos, len);

			err = nfp_cpp_area_write(area, pos, tmpbuf, len);
			if (err < 0) {
				nfp_cpp_area_release(area);
				nfp_cpp_area_free(area);
                err = -EIO;
                goto handle_write_error;
			}
		}

		nfp_offset += pos;
		totlen += pos;
		buff += pos;
		nfp_cpp_area_release(area);
		nfp_cpp_area_free(area);

		if (err < 0)
			goto handle_write_error;

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

int nfp_cpp_dev_read(struct nfp_cpp_dev_data* data,
        int fd, size_t count, off_t offset)
{
    char* buf = (char*) malloc(count);
	char* buff = buf;
	struct nfp_cpp_area *area;
	off_t nfp_offset;
	uint32_t cpp_id, pos, len;
	size_t curlen = count, totlen = 0;
	uint32_t tmpbuf[16];
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

		for (pos = 0; pos < curlen; pos += len)
		{
			len = curlen - pos;
			if (len > sizeof(tmpbuf))
				len = sizeof(tmpbuf);

			err = nfp_cpp_area_read(area, pos, tmpbuf, len);
			if (err < 0)
				break;
			memcpy(buff + pos, tmpbuf, len);
		}

		nfp_offset += pos;
		totlen += pos;
		buff += pos;
		nfp_cpp_area_release(area);
		nfp_cpp_area_free(area);

		if (err < 0)
			goto handle_read_error;

		count -= pos;
		curlen = (count > NFP_CPP_MEMIO_BOUNDARY) ?
			NFP_CPP_MEMIO_BOUNDARY : count;
	}

	/* Send return value */
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
