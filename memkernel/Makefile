obj-m += memkernel.o
memkernel-objs := entry.o memory.o process.o

KDIR ?= $(PWD)
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
