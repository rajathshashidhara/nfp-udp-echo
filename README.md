# MMIO performance benchmark for Netronome (NFP-4000/6000) SmartNICs

## Building
- This application uses the `igb_uio` driver distributed with DPDK.
Compile DPDK from source: https://doc.dpdk.org/guides/linux_gsg/build_dpdk.html to obtain the driver.
- Ensure that Netronome [out-of-tree driver](https://help.netronome.com/support/solutions/articles/36000049975-basic-firmware-user-guide#appendix-b-installing-the-out-of-tree-nfp-driver), SDK and Board Support Packages (BSP) are installed.
- Configure packet and buffer size in `user/config.h`
- Run `make` in the top-level directory to build firmware, shim and user application.

## Running
- Make sure `hugetlbfs` is mounted on `/mnt/huge` and enough hugepages are allocated

- Load the firmware

  ```shell
  # Unbind network device from igb_uio driver
  dpdk-devbind.py -u b3:00.0
  rmmod igb_uio
  rmmod uio

  # Load NFP out-of-tree driver
  modprobe nfp nfp_dev_cpp=1 nfp_pf_netdev=1

  # Unload previously flashed firmware
  nfp-nffw unload

  # Stop previously running firmware and start processing with updated firmware
  firmware/init/wire.sh stop
  firmware/init/wire.sh start mmio_perf.fw	# For single-context firmware
  ```

- Run the userspace driver

  ```shell
  # Remove NFP driver
  rmmod nfp

  # Load igb_uio driver
  modprobe uio
  insmod <dpdk-inst>/lib/modules/$(uname -r)/extra/dpdk/igb_uio.ko

  # Bind network device to igb_uio driver
  dpdk-devbind.py --bind=igb_uio b3:00.0

  # Run the userspace driver and application
  user/nfp-user.out [options]
  ```

## Usage

```
  mmio-perf [OPTIONS]
    --lat  -L      Latency test (Default)
    --bw   -B      Bandwidth test
    --read  -r     Read Operation (Default)
    --write -w     Write Operation
    --wrrd  -m     WR-RD Operation
    --cls  -S      Cluster Local Scratch MMIO Target (Default)
    --ctm  -C      Cluster Target Memory MMIO Target
    --imem -I      Internal Memory MMIO Target
    --emem -E      External Memory MMIO Target
    --num_threads= -t  Number of Threads in Bandwidth test (Default: 1)
    --len= -l      Transfer length in bytes (Default: 8)
    --num_ops= -n  Number of transfer operations (Default: 10^6)
    --offset= -o   Start offset in the window (Default: 0)
```
