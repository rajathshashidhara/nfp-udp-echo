#include <stdio.h>
#include <pthread.h>

#include <driver.h>

extern int nfp_cpp_dev_main(struct rte_pci_device* dev, struct nfp_cpp* cpp);

int main(int argc, char* argv[])
{
    struct rte_pci_device* dev;
    int ret;

    dev = pci_scan();
    if (!dev)
    {
        fprintf(stderr, "Cannot find Netronome NIC\n");
        return 0;
    }

    struct nfp_cpp* cpp;
    ret = pci_probe(dev, &cpp);
    if (ret)
    {
        fprintf(stderr, "Probe unsuccessful\n");
        return 0;
    }

    nfp_cpp_dev_main(dev, cpp);
    fprintf(stderr, "Exit CPP handler\n");
    
    while (1) {}

    return 0;
}
