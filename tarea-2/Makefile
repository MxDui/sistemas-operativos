# Makefile para el módulo sysstats (proyecto OSC cap. 2)
# Compila contra los headers del kernel en ejecución.

obj-m += sysstats.o

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD       := $(shell pwd)

all:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean

load:
	sudo insmod sysstats.ko

unload:
	sudo rmmod sysstats

test: all
	./test.sh

.PHONY: all clean load unload test