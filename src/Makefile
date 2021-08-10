CC = /usr/bin/gcc

EXTRA_CFLAGS += -std=gnu11

obj-m += hid-procon.o
hid-procon-objs += hid-procon-main.o procon-input.o procon-print.o packet.o

SRC_DIR = $(abspath .)

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(SRC_DIR) modules

load: hid-procon.ko
	insmod $(SRC_DIR)/hid-procon.ko

unload: hid-procon.ko
	rmmod hid-procon

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(SRC_DIR) clean