obj-m += ionopimax.o

ionopimax-objs := module.o
ionopimax-objs += commons/utils/utils.o
ionopimax-objs += commons/gpio/gpio.o
ionopimax-objs += commons/wiegand/wiegand.o
ionopimax-objs += commons/atecc/atecc.o

MODULE_NAME := ionopimax
MODULE_VERSION_DEFINE := IONOPIMAX_MODULE_VERSION
DTS_NAME := ionopimax
UDEV_RULES := 99-ionopimax.rules 99-ionopimax-serial.rules

include commons/scripts/kmod-common.mk
