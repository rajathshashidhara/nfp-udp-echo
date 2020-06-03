# UDP Echo Server on Netronome (NFP-4000/6000) SmartNICs

Example application to access Netronome SmartNICs using userspace driver - PCIe directly to application memory from the NIC

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
  firmware/init/wire.sh start app_sc.fw	# For single-context firmware
  # or
  firmware/init/wire.sh start app_mc.fw	# For multi-context firmware
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
  user/nfp-user.out
  ```

- Ensure that ARP entry corresponding to the Netronome NIC is added to the test machine (peer connected to host via Netronome NIC interface)

- Send UDP traffic using `iperf` for bandwidth measurement (NOTE: Header size = 42 B. Total packet size = 1408 B)

  ```shell
  # Create UDP server
  iperf -u -s --bind 10.0.0.101 -l 1366 -i 1	# On terminal 1

  # Create UDP client
  # 1408 byte packets at 5200 Mbps for 60 seconds
  iperf -u -c 10.0.0.105 -l 1366 -i 1 -b 5200M -t 60	# On terminal 2

  # Ping for latency measurement
  ping 10.0.0.105 -s 1366		# On terminal 3
  ```

