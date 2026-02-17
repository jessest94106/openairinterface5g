# Description

This document describes the steps needed to run the O-RU + gNB + NR UE on a single server.

# Limitations

The RU does not support fragmentation or compression at this point, therefore the bandwidth is currently limited.

# Dependecies

The O-RU requires a patch applied on top of the usual OAI patch to xran library release F. The patch is available
under `cmake_targets/tools/oran_fhi_integration_patches/F/oru.patch`.

The O-RU was tested with dpdk 22.

## Using a locally installed DPDK

Instead of installing dpdk globally you can install it in a local directory, e.g.

```bash
meson --prefix=<your dpdk install directory> build
cd build
ninja
ninja install
```

Building xran

```bash
cd fhi_lib/lib/
WIRELESS_SDK_TOOLCHAIN=gcc RTE_SDK=<your dpdk directory> XRAN_DIR=<your xran directory> make XRAN_LIB_SO=1
```

In the next step you'll add an extra argument to `cmake` command

# Configuration

Use `setup_ru_ifs.sh` as a basis for your own vf setup script.

# Compilation

Assuming your build directory is `cmake_targets/build`

```bash
cmake ../../ -GNinja -DOAI_FHI72=ON -Dxran_LOCATION=<your xran directory>/fhi_lib/lib
```

OR if using local dpdk installation

```bash
make ../../ -GNinja -DOAI_FHI72=ON -Dxran_LOCATION=<your xran directory>/fhi_lib/lib -DCMAKE_PREFIX_PATH=<your dpdk install directory>
```

Verify DPDK version in stdout:
```
-- Checking for module 'libdpdk'
--   Found libdpdk, version 22.11.11
```

```bash
cmake --build . --target nr-softmodem nr-uesoftmodem ldpc params_libconfig nr-oru oran_fhlib_5g vrtsim
```

# Setup

You need to setup your VFs to enable packet routing between them. An example config with comments is added in this folder.

# Test commands

## My local setup

```bash
sudo ./nr-oru -O ../../targets/PROJECTS/GENERIC-NR-5GC/CONF/ru.band77.mu1.106rb.2x2.conf  --vrtsim.role server
```

```bash
sudo ./nr-softmodem -O ../../targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb.sa.band77.106prb.fhi72.2x2-nr-oru.conf  --gNBs.[0].min_rxtxtime 6
```

```bash
sudo -E LD_LIBRARY_PATH=. ./nr-uesoftmodem -C 4049760000 -r 106 --numerology 1 --ssb 516 --band 77  --device.name vrtsim
```

## Example from peafowl (using locally installed dpdk)

```bash
sudo -E LD_LIBRARY_PATH=/home/bpodrygajlo/dpdk-stable-22.11.11/install/lib/x86_64-linux-gnu taskset -c 15-23 ./nr-oru -O ../../targets/PROJECTS/GENERIC-NR-5GC/CONF/ru.band77.mu1.106rb.2x2.conf  --vrtsim.role server --fhi_72.dpdk_devices 0000:01:01.0,0000:01:01.1
```

```bash
sudo -E LD_LIBRARY_PATH=/home/bpodrygajlo/dpdk-stable-22.11.11/install/lib/x86_64-linux-gnu taskset -c 24-31 ./nr-softmodem -O ../../targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb.sa.band77.106prb.fhi72.2x2-nr-oru.conf  --gNBs.[0].min_rxtxtime 6 --fhi_72.dpdk_devices 0000:01:01.2,0000:01:01.3
```

```bash
sudo -E LD_LIBRARY_PATH=. taskset -c -c 7-14 ./nr-uesoftmodem -C 4049760000 -r 106 --numerology 1 --ssb 516 --band 77  --device.name vrtsim
```
