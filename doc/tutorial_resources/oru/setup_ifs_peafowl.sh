#!/bin/sh
#### 100G interface --> enp1s0f0 | VFs pcie-address --> 01:01.0, 01:01.1
#### 25G interface (PTP) --> enp193s0f1 | VFs pcie-address --> c1:11.0,c1:11.1
set -x

# Set these in the DU config
RU_U_PLANE_MAC=00:11:22:33:64:66
RU_C_PLANE_MAC=00:11:22:33:64:67
# Set these in the RU config
DU_U_PLANE_MAC=00:11:22:33:64:68
DU_C_PLANE_MAC=00:11:22:33:64:69

U_VLAN=4
C_VLAN=5

MTU=9600
IF=enp1s0f0

## It will be something like this --> $DPDK_INST/bin
DPDK_DEVBIND_PREFIX="${DPDK_DEVBIND_PREFIX:-}"

sudo ethtool -G $IF rx 8160 tx 8160
sudo sh -c "echo 0 > /sys/class/net/$IF/device/sriov_numvfs"
sudo sh -c "echo 4 > /sys/class/net/$IF/device/sriov_numvfs"
sudo modprobe -r iavf
sudo modprobe iavf
# this next 2 lines is for C/U planes
sudo ip link set $IF vf 0 mac $RU_U_PLANE_MAC vlan $U_VLAN spoofchk off mtu $MTU
sudo ip link set $IF vf 1 mac $RU_C_PLANE_MAC vlan $C_VLAN spoofchk off mtu $MTU
sudo ip link set $IF vf 2 mac $DU_U_PLANE_MAC vlan $U_VLAN spoofchk off mtu $MTU
sudo ip link set $IF vf 3 mac $DU_C_PLANE_MAC vlan $C_VLAN spoofchk off mtu $MTU
sleep 1
sudo ${DPDK_DEVBIND_PREFIX}dpdk-devbind.py --unbind 0000:01:01.0
sudo ${DPDK_DEVBIND_PREFIX}dpdk-devbind.py --unbind 0000:01:01.1
sudo ${DPDK_DEVBIND_PREFIX}dpdk-devbind.py --unbind 0000:01:01.2
sudo ${DPDK_DEVBIND_PREFIX}dpdk-devbind.py --unbind 0000:01:01.3
sudo modprobe vfio-pci
sudo ${DPDK_DEVBIND_PREFIX}dpdk-devbind.py --bind vfio-pci 0000:01:01.0
sudo ${DPDK_DEVBIND_PREFIX}dpdk-devbind.py --bind vfio-pci 0000:01:01.1
sudo ${DPDK_DEVBIND_PREFIX}dpdk-devbind.py --bind vfio-pci 0000:01:01.2
sudo ${DPDK_DEVBIND_PREFIX}dpdk-devbind.py --bind vfio-pci 0000:01:01.3
sleep 5
