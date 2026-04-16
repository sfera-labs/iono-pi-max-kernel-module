obj-m += ionopimax.o

ionopimax-objs := module.o
ionopimax-objs += commons/commons.o
ionopimax-objs += gpio/gpio.o
ionopimax-objs += wiegand/wiegand.o
ionopimax-objs += atecc/atecc.o

SOURCE_DIR := $(if $(src),$(src),$(CURDIR))
IONOPIMAX_VERSION := $(strip $(shell cat $(SOURCE_DIR)/VERSION))
ccflags-y += -DIONOPIMAX_MODULE_VERSION=\"$(IONOPIMAX_VERSION)\"

KVER ?= $(if $(KERNELRELEASE),$(KERNELRELEASE),$(shell uname -r))
KDIR ?= /lib/modules/$(KVER)/build

all: dtbo
	make -C $(KDIR) M=$(PWD) modules

dtbo: ionopimax.dts
	dtc -@ -Hepapr -I dts -O dtb -o ionopimax.dtbo ionopimax.dts

clean:
	make -C $(KDIR) M=$(PWD) clean
	rm -f ionopimax.dtbo

install:
	sudo install -D -m 644 -c ionopimax.ko /lib/modules/$(KVER)/updates/dkms/ionopimax.ko
	sudo depmod
	sudo $(MAKE) install-extra

install-extra: dtbo
	install -D -m 644 -c ionopimax.dtbo /boot/overlays/ionopimax.dtbo
	install -D -m 644 -c 99-ionopimax.rules /etc/udev/rules.d/99-ionopimax.rules
	install -D -m 644 -c 99-ionopimax-serial.rules /etc/udev/rules.d/99-ionopimax-serial.rules
	udevadm control --reload-rules || true
	udevadm trigger || true

uninstall-extra:
	rm -f /boot/overlays/ionopimax.dtbo
	rm -f /etc/udev/rules.d/99-ionopimax.rules
	rm -f /etc/udev/rules.d/99-ionopimax-serial.rules
	udevadm control --reload-rules || true
	udevadm trigger || true
