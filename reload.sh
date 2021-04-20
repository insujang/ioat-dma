#!/bin/bash

sudo rmmod ioat-dma dax-pmem-compat device-dax
sudo insmod device_dax.ko
sudo ~/assise/utils/use_dax.sh bind
sudo insmod ioat-dma.ko