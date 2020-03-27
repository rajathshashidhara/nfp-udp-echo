#ifndef _USERSPACE_DRIVER_H
#define _USERSPACE_DRIVER_H

#include <rte_pci.h>
#include <nfp_nsp.h>

/**
 * Scan sysfs for PCI devices to connect with Netronome NIC
 */
struct rte_pci_device* pci_scan();

int pci_probe(struct rte_pci_device *dev, struct nfp_cpp **cppptr);

#endif /* _USERSPACE_DRIVER_H */