CC = /usr/bin/gcc

EXTRA_CFLAGS += -std=gnu11

obj-m += hid-procon.o
hid-procon-objs += hid-procon-main.o procon-input.o procon-print.o packet.o util.o

SRC_DIR = $(abspath .)

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(SRC_DIR) modules

compile_commands.json:
	/usr/bin/python3 /usr/src/kernels/$(shell uname -r)/scripts/clang-tools/gen_compile_commands.py -d /lib/modules/$(shell uname -r)/build $(SRC_DIR)

load: hid-procon.ko
	modprobe hid
	insmod hid-procon.ko

unload: hid-procon.ko
	rmmod hid-procon

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(SRC_DIR) clean