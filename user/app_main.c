#include <stdio.h>

#include <driver.h>

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

    ret = pci_probe(dev);
    if (ret)
    {
        fprintf(stderr, "Probe unsuccessful\n");
        return 0;
    }
    return 0;
}