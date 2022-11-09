obj-m += ionopimax.o

ionopimax-objs := module.o
ionopimax-objs += commons/commons.o
ionopimax-objs += gpio/gpio.o
ionopimax-objs += wiegand/wiegand.o
ionopimax-objs += atecc/atecc.o

all:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(PWD) clean

install:
	sudo install -m 644 -c ionopimax.ko /lib/modules/$(shell uname -r)
	sudo depmod
