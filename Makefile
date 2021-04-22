obj-m += ioat-dma.o
obj-m += device_dax.o

device_dax-y := device.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
	$(CC) test.c -o test

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f test