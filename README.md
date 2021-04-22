# IOAT-DMA for devdax (device-dax) persistent memory module

This repository contains the following:
- `ioat-dma.ko` (built from `ioat-dma.c`): a kernel module that initiates DMA to a DMA device that supports `DMA_MEMCPY` capability, and handles DMA completion via `ioctl()`.
- `device-dax.ko` (built from `device.c`): a modified `deivce-dax.ko` kernel module (`dax_get_device()` helper function).
- `test.py`: a unittest file built with Python unittest package.
- `test.c`: a test file that compares performance (DMA vs `memcpy`).

## Build and Test
Tested under Ubuntu 20.04.2 LTS kernel 5.4.0-66-generic.
Might not work with higher or lower version of kernel.
Sources for `device-dax.ko` has been borrowed from [Linux kernel 5.6](https://github.com/torvalds/linux/tree/v5.6).

For reloading modules, you can use `reload.sh` if your $(ASSISE) is `~/assise`.

```
$ make
$ sudo rmmod ioat-dma dax-pmem-compat device-dax
$ sudo insmod device_dax.ko
$ sudo $(ASSISE)/utils/use_dax.sh bind
$ sudo insmod ioat-dma.ko
$ sudo python3 test.py -vv
Randomly generated data: b'(randomly generated)'... (2097152 bytes)
test_01_dax_src_init (__main__.TestIoatDma) ... ok
test_02_ioat_dma (__main__.TestIoatDma) ... ok
test_03_dax_dst_validate (__main__.TestIoatDma) ... ok

----------------------------------------------------------------------
Ran 3 tests in 0.013s

OK
$ sudo ./test
DMA vs memcpy (data size: 0x2000000 bytes)
perform_dma: data verification done!
perform_memcpy: data verification done!
DMA: 0.008604 s, memcpy: 0.015450 s
```