#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <linux/limits.h>

#include <rte_byteorder.h>
#include <rte_common.h>
#include <rte_pci.h>
#include <rte_string_fns.h>

#include "nfpcore/nfp_cpp.h"
#include "nfpcore/nfp_nffw.h"
#include "nfpcore/nfp_hwinfo.h"
#include "nfpcore/nfp_mip.h"
#include "nfpcore/nfp_rtsym.h"
#include "nfpcore/nfp_nsp.h"

/* Probing Netronome NICs */
#define PCI_VENDOR_ID_NETRONOME         0x19ee
#define PCI_DEVICE_ID_NFP4000_PF_NIC    0x4000
#define PCI_DEVICE_ID_NFP6000_PF_NIC    0x6000

/* split string into tokens */
static int
strsplit(char *string, int stringlen,
	     char **tokens, int maxtokens, char delim)
{
	int i, tok = 0;
	int tokstart = 1; /* first token is right at start of string */

	if (string == NULL || tokens == NULL)
		goto einval_error;

	for (i = 0; i < stringlen; i++) {
		if (string[i] == '\0' || tok >= maxtokens)
			break;
		if (tokstart) {
			tokstart = 0;
			tokens[tok++] = &string[i];
		}
		if (string[i] == delim) {
			string[i] = '\0';
			tokstart = 1;
		}
	}
	return tok;

einval_error:
	errno = EINVAL;
	return -1;
}

/*
 * split up a pci address into its constituent parts.
 */
static int
parse_pci_addr_format(const char *buf, int bufsize, struct rte_pci_addr *addr)
{
	/* first split on ':' */
	union splitaddr {
		struct {
			char *domain;
			char *bus;
			char *devid;
			char *function;
		};
		char *str[PCI_FMT_NVAL]; /* last element-separator is "." not ":" */
	} splitaddr;

	char *buf_copy = strndup(buf, bufsize);
	if (buf_copy == NULL)
		return -1;

	if (strsplit(buf_copy, bufsize, splitaddr.str, PCI_FMT_NVAL, ':')
			!= PCI_FMT_NVAL - 1)
		goto error;
	/* final split is on '.' between devid and function */
	splitaddr.function = strchr(splitaddr.devid,'.');
	if (splitaddr.function == NULL)
		goto error;
	*splitaddr.function++ = '\0';

	/* now convert to int values */
	errno = 0;
	addr->domain = strtoul(splitaddr.domain, NULL, 16);
	addr->bus = strtoul(splitaddr.bus, NULL, 16);
	addr->devid = strtoul(splitaddr.devid, NULL, 16);
	addr->function = strtoul(splitaddr.function, NULL, 10);
	if (errno != 0)
		goto error;

	free(buf_copy); /* free the copy made with strdup */
	return 0;
error:
	free(buf_copy);
	return -1;
}

/* parse a sysfs (or other) file containing one integer value */
static int
parse_sysfs_value(const char *path, unsigned long *val)
{
	FILE *f;
	char buf[BUFSIZ];
	char *end = NULL;

	if ((f = fopen(path, "r")) == NULL) {
		fprintf(stderr, "%s(): cannot open sysfs value %s\n",
			__func__, path);
		return -1;
	}

	if (fgets(buf, sizeof(buf), f) == NULL) {
		fprintf(stderr, "%s(): cannot read sysfs value %s\n",
			__func__, path);
		fclose(f);
		return -1;
	}
	*val = strtoul(buf, &end, 0);
	if ((buf[0] == '\0') || (end == NULL) || (*end != '\n')) {
		fprintf(stderr, "%s(): cannot parse sysfs value %s\n",
				__func__, path);
		fclose(f);
		return -1;
	}
	fclose(f);
	return 0;
}

/* parse the "resource" sysfs file */
static int
pci_parse_sysfs_resource(const char *filename, struct rte_pci_device *dev)
{
	FILE *f;
	char buf[BUFSIZ];
	int i;
	uint64_t phys_addr, end_addr, flags;
	union pci_resource_info
    {
		struct {
			char *phys_addr;
			char *end_addr;
			char *flags;
		};
		char *ptrs[PCI_RESOURCE_FMT_NVAL];
	} res_info;

	f = fopen(filename, "r");
	if (f == NULL)
    {
		fprintf(stderr, "Cannot open sysfs resource\n");
		return -1;
	}

	for (i = 0; i<PCI_MAX_RESOURCE; i++)
    {

		if (fgets(buf, sizeof(buf), f) == NULL)
        {
			fprintf(stderr,
				"%s(): cannot read resource\n", __func__);
			goto error;
		}

        if (strsplit(buf, sizeof(buf), res_info.ptrs, 3, ' ') != 3)
        {
			fprintf(stderr,
				"%s(): bad resource format\n", __func__);
            goto error;
        }
        errno = 0;
        phys_addr = strtoull(res_info.phys_addr, NULL, 16);
        end_addr = strtoull(res_info.end_addr, NULL, 16);
        flags = strtoull(res_info.flags, NULL, 16);
        if (errno != 0)
        {
			fprintf(stderr,
				"%s(): bad resource format\n", __func__);
            goto error;
        }

		if (flags & IORESOURCE_MEM)
        {
			dev->mem_resource[i].phys_addr = phys_addr;
			dev->mem_resource[i].len = end_addr - phys_addr + 1;
			/* not mapped for now */
			dev->mem_resource[i].addr = NULL;
		}
	}
	fclose(f);
	return 0;

error:
	fclose(f);
	return -1;
}

/**
 * Setup pci_dev entry from a matched PCI device
 */
static struct rte_pci_device*
pci_setup_device(const char *dirname, const struct rte_pci_addr *addr)
{
    char path[PATH_MAX];
    unsigned long tmp;
    struct rte_pci_device *dev;
    int ret;

    dev = malloc(sizeof(struct rte_pci_device));
    if (dev == NULL)
        return NULL;
    memset(dev, 0, sizeof(struct rte_pci_device));
    dev->addr = *addr;

    /* Get vendor ID */
	snprintf(path, sizeof(path), "%s/vendor", dirname);
	if (parse_sysfs_value(path, &tmp) < 0) {
		free(dev);
		return NULL;
	}
	dev->id.vendor_id = (uint16_t)tmp;

	/* Get device id */
	snprintf(path, sizeof(path), "%s/device", dirname);
	if (parse_sysfs_value(path, &tmp) < 0) {
		free(dev);
		return NULL;
	}
	dev->id.device_id = (uint16_t)tmp;

	/* get subsystem_vendor id */
	snprintf(path, sizeof(path), "%s/subsystem_vendor",
		 dirname);
	if (parse_sysfs_value(path, &tmp) < 0) {
		free(dev);
		return NULL;
	}
	dev->id.subsystem_vendor_id = (uint16_t)tmp;

	/* get subsystem_device id */
	snprintf(path, sizeof(path), "%s/subsystem_device",
		 dirname);
	if (parse_sysfs_value(path, &tmp) < 0) {
		free(dev);
		return NULL;
	}
	dev->id.subsystem_device_id = (uint16_t)tmp;

	/* get class_id */
	snprintf(path, sizeof(path), "%s/class",
		 dirname);
	if (parse_sysfs_value(path, &tmp) < 0) {
		free(dev);
		return NULL;
	}
	/* the least 24 bits are valid: class, subclass, program interface */
	dev->id.class_id = (uint32_t)tmp & PCI_CLASS_ANY_ID;

    /* Set max_cfs */
    dev->max_vfs = 0;
	snprintf(path, sizeof(path), "%s/max_vfs", dirname);
	if (!access(path, F_OK) &&
	    parse_sysfs_value(path, &tmp) == 0)
		dev->max_vfs = (uint16_t)tmp;
    
    /* Set device name */
    snprintf(dev->name, sizeof(dev->name), PCI_PRI_FMT,
			    addr->domain, addr->bus,
			    addr->devid, addr->function);
    dev->device.name = dev->name;
    
	/* parse resources */
	snprintf(path, sizeof(path), "%s/resource", dirname);
	if (pci_parse_sysfs_resource(path, dev) < 0) {
		fprint(stderr, "%s(): cannot parse resource\n", __func__);
		free(dev);
		return NULL;
	}

    return dev;
}

/**
 * Scan sysfs for PCI devices to connect with Netronome NIC
 */
struct rte_pci_device*
pci_scan()
{
    struct dirent *e;
    DIR *dir;
    char path[PATH_MAX];
    struct rte_pci_addr addr;
    unsigned long vendor_id, device_id;

    dir = opendir("/sys/bus/pci/devices/");
    if (dir == NULL)
    {
        fprintf(stderr, "%s(): opendir failed: %s\n",
            __func__, strerror(errno));
        return NULL;
    }

    /* Read every directory in /sys/bus/pci/devices */
    while ((e = readdir(dir)) != NULL)
    {
        if (e->d_name[0] == '.')
            continue;
        
        if (parse_pci_addr_format(e->d_name, sizeof(e->d_name), &addr) != 0)
            continue;
        
        /* Read vendor and device id*/
        snprintf(path, sizeof(path), "%s/%s/%s",
            "/sys/bus/pci/devices", e->d_name, "vendor");
        if (parse_sysfs_value(path, &vendor_id) < 0)
            continue;
        snprintf(path, sizeof(path), "%s/%s/%s",
            "/sys/bus/pci/devices", e->d_name, "device");
        if (parse_sysfs_value(path, &device_id) < 0)
            continue;

        if (vendor_id == PCI_VENDOR_ID_NETRONOME &&
                (device_id == PCI_DEVICE_ID_NFP4000_PF_NIC ||
                device_id == PCI_DEVICE_ID_NFP6000_PF_NIC))
        {
            /* Setup NIC */
            closedir(dir);
            
            snprintf(path, sizeof(path), "%s/%s",
                "/sys/bus/pci/devices", e->d_name);
            return pci_setup_device(path, &addr);
        }
    }

    closedir(dir);
    return NULL;
}

/* Map PCI device */
static int
pci_map_device(struct rte_pci_device *dev)
{
    int ret = -1;
    int fd;
    int i;
    uint64_t phaddr;
    void* mapaddr;
    char devname[PATH_MAX];

    /* Map all BARs */
    for (i = 0; i != PCI_MAX_RESOURCE; i++)
    {
        /* skip empty BAR */
        phaddr = dev->mem_resource[i].phys_addr;
        if (phaddr == 0)
            continue;
        
        snprintf(devname, sizeof(devname),
            "%s/" PCI_PRI_FMT "/resource%d",
            "/sys/bus/pci/devices",
            dev->addr.domain, dev->addr.bus, dev->addr.devid,
            dev->addr.function, i);
        fd = open(devname, O_RDWR);
        if (fd < 0)
        {
            fprintf(stderr, "Cannot open %s: %s\n",
                devname, strerror(errno));
            return -1;
        }

        mapaddr = mmap(NULL, (size_t)dev->mem_resource[i].len,
            PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        if (mapaddr == MAP_FAILED)
        {
            fprintf(stderr, 
                "%s(): cannot mmap(%d, %p, 0x%zx, 0x%llx): %s (%p)\n",
                __func__, fd, 0, dev->mem_resource[i].len,
                (unsigned long long) 0,
                strerror(errno), mapaddr);
            return -1;
        }

        dev->mem_resource[i].addr = mapaddr;
    }

/* TODO: Free other maps in case one fails! */
    return 0;
}

int
pci_probe(struct rte_pci_device *dev)
{
    struct nfp_cpp *cpp;
	struct nfp_hwinfo *hwinfo;
	struct nfp_rtsym_table *sym_tbl;
	struct nfp_eth_table *nfp_eth_table = NULL;
	int total_ports;
	void *priv = 0;
	int ret = -ENODEV;
	int err;
	int i;

	if (!dev)
		return ret;

    cpp = nfp_cpp_from_device_name(dev, 1);
    if (!cpp)
    {
        fprintf(stderr, "%s(): CPP handle can not be obtained\n",
            __func__);
        ret = -EIO;
        goto error;
    }

    hwinfo = nfp_hwinfo_read(cpp);
    if (!hwinfo) {
        fprintf(stderr, "%s(): Error reading hwinfo table",
            __func__);
        return -EIO;
    }

    nfp_eth_table = nfp_eth_read_ports(cpp);
    if (!nfp_eth_table) {
        fprintf(stderr, "%s(): Error reading NFP ethernet table",
            __func__);

        return -EIO;
    }

	sym_tbl = nfp_rtsym_table_read(cpp);
	if (!sym_tbl) {
		fprintf(stderr, "%s(): Something is wrong with the firmware"
				" symbol table", __func__);
		ret = -EIO;
		goto error;
	}

	total_ports = nfp_rtsym_read_le(sym_tbl, "nfd_cfg_pf0_num_ports", &err);
	if (total_ports != (int)nfp_eth_table->count) {
		fprintf(stderr, "%s(): Inconsistent number of ports",
            __func__);
		ret = -EIO;
		goto error;
	}
    
    fprintf(stderr, "Total pf ports: %d", total_ports);

error:
    free(nfp_eth_table);
    return ret;
}