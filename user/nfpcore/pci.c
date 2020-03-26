#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/pciio.h>
#include <dev/pci/pcireg.h>
#include <fcntl.h>

#include <rte_pci.h>

/* Read PCI config space. */
int rte_pci_read_config(const struct rte_pci_device *dev,
		void *buf, size_t len, off_t offset)
{
	int fd = -1;
	int size;
	/* Copy Linux implementation's behaviour */
	const int return_len = len;
	struct pci_io pi = {
		.pi_sel = {
			.pc_domain = dev->addr.domain,
			.pc_bus = dev->addr.bus,
			.pc_dev = dev->addr.devid,
			.pc_func = dev->addr.function,
		},
		.pi_reg = offset,
	};

	fd = open("/dev/pci", O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "%s(): error opening /dev/pci\n", __func__);
		goto error;
	}

	while (len > 0) {
		size = (len >= 4) ? 4 : ((len >= 2) ? 2 : 1);
		pi.pi_width = size;

		if (ioctl(fd, PCIOCREAD, &pi) < 0)
			goto error;
		memcpy(buf, &pi.pi_data, size);

		buf = (char *)buf + size;
		pi.pi_reg += size;
		len -= size;
	}
	close(fd);

	return return_len;

 error:
	if (fd >= 0)
		close(fd);
	return -1;
}

/* Write PCI config space. */
int rte_pci_write_config(const struct rte_pci_device *dev,
		const void *buf, size_t len, off_t offset)
{
	int fd = -1;

	struct pci_io pi = {
		.pi_sel = {
			.pc_domain = dev->addr.domain,
			.pc_bus = dev->addr.bus,
			.pc_dev = dev->addr.devid,
			.pc_func = dev->addr.function,
		},
		.pi_reg = offset,
		.pi_data = *(const uint32_t *)buf,
		.pi_width = len,
	};

	if (len == 3 || len > sizeof(pi.pi_data)) {
		RTE_LOG(ERR, EAL, "%s(): invalid pci read length\n", __func__);
		goto error;
	}

	memcpy(&pi.pi_data, buf, len);

	fd = open("/dev/pci", O_RDWR);
	if (fd < 0) {
		RTE_LOG(ERR, EAL, "%s(): error opening /dev/pci\n", __func__);
		goto error;
	}

	if (ioctl(fd, PCIOCWRITE, &pi) < 0)
		goto error;

	close(fd);
	return 0;

 error:
	if (fd >= 0)
		close(fd);
	return -1;
}
