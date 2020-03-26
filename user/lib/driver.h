#ifndef _USERSPACE_DRIVER_H
#define _USERSPACE_DRIVER_H

#include <rte_pci.h>

/**
 * Scan sysfs for PCI devices to connect with Netronome NIC
 */
struct rte_pci_device* pci_scan();

int pci_probe(struct rte_pci_device *dev);

#endif /* _USERSPACE_DRIVER_H */