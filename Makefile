TARGET	= ax_usb_nic
KDIR	= /lib/modules/$(shell uname -r)/build
PWD	= $(shell pwd)

FROCE_MAC_ADDR = n

ENABLE_AUTODETACH_FUNC = n
ENABLE_MAC_PASS = n

obj-m := $(TARGET).o
$(TARGET)-objs := ax_main.o ax88179_178a.o ax88179a_772d.o
EXTRA_CFLAGS = -fno-pie -Wimplicit-fallthrough

ifeq ($(FROCE_MAC_ADDR), y)
	EXTRA_CFLAGS += -DFROCE_MAC_ADDR
endif

ifeq ($(ENABLE_AUTODETACH_FUNC), y)
	EXTRA_CFLAGS += -DENABLE_AUTODETACH_FUNC
endif

ifeq ($(ENABLE_MAC_PASS), y)
	EXTRA_CFLAGS += -DENABLE_MAC_PASS
endif

ifneq (,$(filter $(SUBLEVEL),14 15 16 17 18 19 20 21))
MDIR	= kernel/drivers/usb/net
else
MDIR	= kernel/drivers/net/usb
endif

all:
	make -C $(KDIR) M=$(PWD) modules
	$(CC) ax88179_programmer.c -o ax88179_programmer
	$(CC) ax88179a_programmer.c -o ax88179a_programmer
	$(CC) axcmd.c -o axcmd

install:
ifneq (,$(wildcard /lib/modules/$(shell uname -r)/$(MDIR)/ax88179_178a.ko))
	gzip /lib/modules/$(shell uname -r)/$(MDIR)/ax88179_178a.ko
endif
	make -C $(KDIR) M=$(PWD) INSTALL_MOD_DIR=$(MDIR) modules_install
	depmod -a

uninstall:
ifneq (,$(wildcard /lib/modules/$(shell uname -r)/$(MDIR)/$(TARGET).ko))
	rm -f /lib/modules/$(shell uname -r)/$(MDIR)/$(TARGET).ko
endif
	depmod -a

clean:
	make -C $(KDIR) M=$(PWD) clean
	rm -f ax88179_programmer ax88179a_programmer axcmd
