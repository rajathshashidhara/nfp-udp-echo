#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#define __USE_GNU
#include <sys/types.h>
#include <sys/socket.h>
#include <asm-generic/ioctl.h>
#include <unistd.h>
#include <dlfcn.h>
#include <pthread.h>

#include "nfp_ioctl.h"

#define MEM_BARRIER() __asm__ volatile("" ::: "memory")

static int (*libc_open)(const char* pathname, int flags) = NULL;
static int (*libc_open64)(const char* pathname, int flags) = NULL;
static int (*libc_openat)(int dirfd, const char* pathname,
    int flags) = NULL;
static int (*libc_close)(int fd) = NULL;
static ssize_t (*libc_pread)(int fd, void* buf, size_t count,
            off_t offset) = NULL;
static ssize_t (*libc_pread64)(int fd, void* buf, size_t count,
            off_t offset) = NULL;
static ssize_t (*libc_pwrite)(int fd, const void* buf,
        size_t count, off_t offset) = NULL;
static ssize_t (*libc_pwrite64)(int fd, const void* buf,
        size_t count, off_t offset) = NULL;
static int (*libc_ioctl)(int fd, unsigned long request, char* argp);

#define MAX_FD  1024 * 1024

enum fd_type {
    FD_UNUSED,
    FD_CPP,
    FD_LIBC
};

enum op_type {
    OP_UNUSED,
    OP_PREAD,
    OP_PWRITE,
    OP_IOCTL
};

static int fd_status[MAX_FD];
static inline void ensure_init(void);

int open(const char* pathname, int flags)
{
    int fd;
    int temp;
    struct sockaddr address;

    fprintf(stderr, "SHIM: %s\n", __func__);

    ensure_init();

    if (strcmp(pathname, "/dev/nfp-cpp-0") == 0)
    {
        fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0)
            return -1;

        if (fd >= MAX_FD)
        {
            errno = EMFILE;
            return -1;
        }

        memset(&address, 0, sizeof(struct sockaddr));
        address.sa_family = AF_UNIX;
        strcpy(address.sa_data, "/tmp/nfp_cpp");
        if (connect(fd, &address, sizeof(struct sockaddr)) < 0)
        {
            temp = errno;
            libc_close(fd);
            errno = temp;

            return -1;
        }

        fd_status[fd] = FD_CPP;
        return 0;
    }
    else
    {
        return libc_open(pathname, flags);
    }
}

int open64(const char* pathname, int flags)
{
    int fd;
    int temp;
    struct sockaddr address;

    fprintf(stderr, "SHIM: %s\n", __func__);

    ensure_init();

    if (strcmp(pathname, "/dev/nfp-cpp-0") == 0)
    {
        fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0)
            return -1;

        if (fd >= MAX_FD)
        {
            errno = EMFILE;
            return -1;
        }

        memset(&address, 0, sizeof(struct sockaddr));
        address.sa_family = AF_UNIX;
        strcpy(address.sa_data, "/tmp/nfp_cpp");
        if (connect(fd, &address, sizeof(struct sockaddr)) < 0)
        {
            temp = errno;
            libc_close(fd);
            errno = temp;

            return -1;
        }

        fd_status[fd] = FD_CPP;
        return fd;
    }
    else
    {
        return libc_open64(pathname, flags);
    }
}

int openat(int dirfd, const char* pathname, int flags)
{
    fprintf(stderr, "SHIM: %s\n", __func__);

    ensure_init();

    if (pathname[0] == '/')
    {
        return open(pathname, flags);
    }
    else
    {
        fprintf(stderr, "Relative addressing not supported!\n");

        errno = ENOTDIR;
        return -1;
    }
}

/* TODO: add error checking */
int close(int fd)
{
    fprintf(stderr, "SHIM: %s\n", __func__);

    ensure_init();

    int ret = libc_close(fd);

    if (ret == 0)
    {
        fd_status[fd] = FD_UNUSED;
    }

    return ret;
}

ssize_t pread(int fd, void* buf, size_t count, off_t offset)
{
    ssize_t ret;

    fprintf(stderr, "SHIM: %s\n", __func__);

    ensure_init();

    if (fd_status[fd] == FD_CPP)
    {
        uint64_t temp;
        ssize_t len;

        temp = (uint64_t) OP_PREAD;
        ret = write(fd, &temp, sizeof(temp));
        if (ret < sizeof(temp))
            goto handle_pread_error;

        temp = (uint64_t) count;
        ret = write(fd, &temp, sizeof(temp));
        if (ret < sizeof(temp))
            goto handle_pread_error;

        temp = (uint64_t) offset;
        ret = write(fd, &temp, sizeof(temp));
        if (ret < sizeof(temp))
            goto handle_pread_error;

        ret = read(fd, &temp, sizeof(temp));
        if (ret < sizeof(temp))
            goto handle_pread_error;

        if (temp < count)
            goto handle_pread_error;

        len = 0;
        while (len < count)
        {
            ret = read(fd, ((char*) buf) + len, count - len);
            if (ret < 0)
                goto handle_pread_error;
            len += ret;
        }

        return count;

handle_pread_error:
        fprintf(stderr, "Error when writing to socket: %s",
            strerror(errno));
        close(fd);
        errno = EIO;
        return -1;
    }
    else
        return libc_pread(fd, buf, count, offset);
}

ssize_t pread64(int fd, void* buf, size_t count, off_t offset)
{
    ssize_t ret;

    fprintf(stderr, "SHIM: %s\n", __func__);

    ensure_init();

    if (fd_status[fd] == FD_CPP)
    {
        uint64_t temp;
        ssize_t len;

        temp = (uint64_t) OP_PREAD;
        ret = write(fd, &temp, sizeof(temp));
        if (ret < sizeof(temp))
            goto handle_pread_error;

        temp = (uint64_t) count;
        ret = write(fd, &temp, sizeof(temp));
        if (ret < sizeof(temp))
            goto handle_pread_error;

        temp = (uint64_t) offset;
        ret = write(fd, &temp, sizeof(temp));
        if (ret < sizeof(temp))
            goto handle_pread_error;

        ret = read(fd, &temp, sizeof(temp));
        if (ret < sizeof(temp))
            goto handle_pread_error;

        if (temp < count)
            goto handle_pread_error;

        len = 0;
        while (len < count)
        {
            ret = read(fd, ((char*) buf) + len, count - len);
            if (ret < 0)
                goto handle_pread_error;
            len += ret;
        }

        return count;

handle_pread_error:
        fprintf(stderr, "Error when writing to socket: %s",
            strerror(errno));
        close(fd);
        errno = EIO;
        return -1;
    }
    else
        return libc_pread64(fd, buf, count, offset);
}

ssize_t pwrite(int fd, const void* buf, size_t count, off_t offset)
{
    ssize_t ret;

    fprintf(stderr, "SHIM: %s\n", __func__);

    ensure_init();

    if (fd_status[fd] == FD_CPP)
    {
        uint64_t temp;
        ssize_t len;

        temp = (uint64_t) OP_PWRITE;
        ret = write(fd, &temp, sizeof(temp));
        if (ret < sizeof(temp))
            goto handle_pwrite_error;

        temp = (uint64_t) count;
        ret = write(fd, &temp, sizeof(temp));
        if (ret < sizeof(temp))
            goto handle_pwrite_error;

        temp = (uint64_t) offset;
        ret = write(fd, &temp, sizeof(temp));
        if (ret < sizeof(temp))
            goto handle_pwrite_error;

        len = 0;
        while (len < count)
        {
            ret = write(fd, ((char*) buf) + len, count - len);
            if (ret < 0)
                goto handle_pwrite_error;
            len += ret;
        }

        ret = read(fd, &temp, sizeof(temp));
        if (ret < sizeof(temp))
            goto handle_pwrite_error;

        return temp;

handle_pwrite_error:
        fprintf(stderr, "Error when writing to socket: %s",
            strerror(errno));
        close(fd);
        errno = EIO;
        return -1;
    }
    else
        return libc_pwrite(fd, buf, count, offset);
}

ssize_t pwrite64(int fd, const void* buf, size_t count, off_t offset)
{
    ssize_t ret;

    fprintf(stderr, "SHIM: %s\n", __func__);

    ensure_init();

    if (fd_status[fd] == FD_CPP)
    {
        uint64_t temp;
        ssize_t len;

        temp = (uint64_t) OP_PWRITE;
        ret = write(fd, &temp, sizeof(temp));
        if (ret < sizeof(temp))
            goto handle_pwrite_error;

        temp = (uint64_t) count;
        ret = write(fd, &temp, sizeof(temp));
        if (ret < sizeof(temp))
            goto handle_pwrite_error;

        temp = (uint64_t) offset;
        ret = write(fd, &temp, sizeof(temp));
        if (ret < sizeof(temp))
            goto handle_pwrite_error;

        len = 0;
        while (len < count)
        {
            ret = write(fd, ((char*) buf) + len, count - len);
            if (ret < 0)
                goto handle_pwrite_error;
            len += ret;
        }

        ret = read(fd, &temp, sizeof(temp));
        if (ret < sizeof(temp))
            goto handle_pwrite_error;

        return temp;

handle_pwrite_error:
        fprintf(stderr, "Error when writing to socket: %s",
            strerror(errno));
        close(fd);
        errno = EIO;
        return -1;
    }
    else
        return libc_pwrite64(fd, buf, count, offset);
}

int ioctl(int fd, unsigned long request, char* argp)
{
    fprintf(stderr, "SHIM: %s\n", __func__);

    ensure_init();

    if (fd_status[fd] == FD_CPP)
    {
        struct nfp_cpp_area_request area_req;
        struct nfp_cpp_event_request event_req;
        struct nfp_cpp_explicit_request explicit_req;
        struct nfp_cpp_identification ident;
        uint64_t arg_size, temp;
        int ret;

        temp = (uint64_t) OP_IOCTL;
        ret = write(fd, &temp, sizeof(temp));
        if (ret < sizeof(temp))
            goto handle_ioctl_error;

        switch (request)
        {
        case NFP_IOCTL_CPP_IDENTIFICATION:
            fprintf(stderr, "IOCTL: NFP_IOCTL_CPP_IDENTIFICATION\n");
            if (!argp)
                return sizeof(ident);

            arg_size = sizeof(ident.size);
            break;

        case NFP_IOCTL_FIRMWARE_LOAD:
            fprintf(stderr, "IOCTL: NFP_IOCTL_FIRMWARE_LOAD\n");
            arg_size = NFP_FIRMWARE_MAX;
            break;

        case NFP_IOCTL_FIRMWARE_LAST:
            fprintf(stderr, "IOCTL: NFP_IOCTL_FIRMWARE_LAST\n");
            arg_size = 0;
            break;

        case NFP_IOCTL_CPP_AREA_REQUEST:
        case NFP_IOCTL_CPP_AREA_RELEASE:
            fprintf(stderr, "IOCTL: NFP_IOCTL_CPP_AREA_REQUEST\n");
            arg_size = sizeof(area_req);
            break;

        case NFP_IOCTL_CPP_AREA_RELEASE_OBSOLETE:
            fprintf(stderr, "IOCTL: NFP_IOCTL_CPP_AREA_REQUEST_OBSOLETE\n");
            arg_size = sizeof(area_req.offset);
            break;

        case NFP_IOCTL_CPP_EXPL_REQUEST:
            fprintf(stderr, "IOCTL: NFP_IOCTL_CPP_EXPL_REQUEST\n");
            arg_size = sizeof(explicit_req);
            break;

        case NFP_IOCTL_CPP_EVENT_ACQUIRE:
        case NFP_IOCTL_CPP_EVENT_RELEASE:
            fprintf(stderr, "IOCTL: NFP_IOCTL_CPP_EVENT_ACQUIRE\n");
            arg_size = sizeof(event_req);
            break;

        default:
            fprintf(stderr, "IOCTL: %lu\n", request);
            errno = EINVAL;
            return -1;
        }

        temp = request;
        ret = write(fd, &temp, sizeof(temp));
        if (ret < sizeof(temp))
            goto handle_ioctl_error;

        if (arg_size > 0)
        {
            ret = write(fd, argp, arg_size);
            if (ret < arg_size)
                goto handle_ioctl_error;
        }

        ret = read(fd, &temp, sizeof(temp));
        if (ret < sizeof(temp))
            goto handle_ioctl_error;

        switch(request)
        {
        case NFP_IOCTL_CPP_IDENTIFICATION:
            ret = read(fd, (void*) &ident, sizeof(ident));
            if (ret < sizeof(ident))
                goto handle_ioctl_error;
            memcpy(argp, (void*) &ident, ident.size);
            break;
        case NFP_IOCTL_FIRMWARE_LAST:
            ret = read(fd, argp, NFP_FIRMWARE_MAX);
            if (ret < sizeof(NFP_FIRMWARE_MAX))
                goto handle_ioctl_error;
            break;
        case NFP_IOCTL_CPP_AREA_REQUEST:
            ret = read(fd, (void*) &area_req, sizeof(area_req));
            if (ret < sizeof(ident))
                goto handle_ioctl_error;
            memcpy(argp, (void*) &argp, sizeof(area_req));
            break;
        case NFP_IOCTL_CPP_EXPL_REQUEST:
            ret = read(fd, (void*) &explicit_req, sizeof(explicit_req));
            if (ret < sizeof(ident))
                goto handle_ioctl_error;
            memcpy(argp, (void*) &argp, sizeof(explicit_req));
            break;
        }

        return (int)temp;

handle_ioctl_error:
        fprintf(stderr, "Error when writing to socket: %s",
            strerror(errno));
        close(fd);
        errno = EIO;
        return -1;
    }
    else
        return libc_ioctl(fd, request, argp);
}

static void *bind_symbol(const char *sym)
{
  void *ptr;
  if ((ptr = dlsym(RTLD_NEXT, sym)) == NULL) {
    fprintf(stderr, "flextcp socket interpose: dlsym failed (%s)\n", sym);
    abort();
  }
  return ptr;
}

static void init(void)
{
    libc_open = bind_symbol("open");
    libc_open64 = bind_symbol("open64");
    libc_openat = bind_symbol("openat");
    libc_close = bind_symbol("close");
    libc_pread = bind_symbol("pread");
    libc_pwrite = bind_symbol("write");
    libc_pread64 = bind_symbol("pread64");
    libc_pwrite64 = bind_symbol("pwrite64");
    libc_ioctl = bind_symbol("ioctl");
}

/**
 * TODO: Handle multi-threaded cases
 */
static inline void ensure_init(void)
{
    static volatile uint32_t init_cnt = 0;
    static volatile uint8_t init_done = 0;

    if (init_done == 0)
    {

        if (__sync_fetch_and_add(&init_cnt, 1) == 0)
        {
            init();
            MEM_BARRIER();
            init_done = 1;
        }
        else
        {
            while (init_done == 0)
            {
                pthread_yield();
            }
            MEM_BARRIER();
        }
    }
}
