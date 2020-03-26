#ifndef _RTE_ETHDEV_PCI_H_
#define _RTE_ETHDEV_PCI_H_

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#define PCI_PRI_STR_SIZE sizeof("XXXXXXXX:XX:XX.X")

/** Maximum number of PCI resources. */
#define PCI_MAX_RESOURCE 6

/**
 * A structure describing an ID for a PCI driver. Each driver provides a
 * table of these IDs for each device that it supports.
 */
struct rte_pci_id {
	uint32_t class_id;            /**< Class ID or RTE_CLASS_ANY_ID. */
	uint16_t vendor_id;           /**< Vendor ID or PCI_ANY_ID. */
	uint16_t device_id;           /**< Device ID or PCI_ANY_ID. */
	uint16_t subsystem_vendor_id; /**< Subsystem vendor ID or PCI_ANY_ID. */
	uint16_t subsystem_device_id; /**< Subsystem device ID or PCI_ANY_ID. */
};

/**
 * A structure describing the location of a PCI device.
 */
struct rte_pci_addr {
	uint32_t domain;                /**< Device domain */
	uint8_t bus;                    /**< Device bus */
	uint8_t devid;                  /**< Device ID */
	uint8_t function;               /**< Device function. */
};

/*
 * Internal identifier length
 * Sufficiently large to allow for UUID or PCI address
 */
#define RTE_DEV_NAME_MAX_LEN 64

/**
 * A structure describing a generic device.
 */
struct rte_device {
	const char *name;             /**< Device name */
};

/**
 * A generic memory resource representation.
 */
struct rte_mem_resource {
	uint64_t phys_addr; /**< Physical address, 0 if not resource. */
	uint64_t len;       /**< Length of the resource. */
	void *addr;         /**< Virtual address, NULL when not mapped. */
};

/**
 * A structure describing a PCI device.
 */
struct rte_pci_device {
	struct rte_device device;           /**< Inherit core device */
	struct rte_pci_addr addr;           /**< PCI location. */
	struct rte_pci_id id;               /**< PCI ID. */
	struct rte_mem_resource mem_resource[PCI_MAX_RESOURCE];
					    /**< PCI Memory Resource */
	uint16_t max_vfs;                   /**< sriov enable if not zero */
	char name[PCI_PRI_STR_SIZE+1];      /**< PCI location (ASCII) */
};

int rte_pci_read_config(const struct rte_pci_device *dev,
		void *buf, size_t len, off_t offset);

int rte_pci_write_config(const struct rte_pci_device *dev,
		const void *buf, size_t len, off_t offset);

#endif /* _RTE_ETHDEV_PCI_H_ */
