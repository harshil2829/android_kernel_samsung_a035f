obj-m += memkernel_enhanced.o
memkernel_enhanced-objs := entry.o memory.o process.o hwbp.o

KDIR ?= $(PWD)
PWD  := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
