MODULE_NAME = sysfs_example
SRC := src/main.c

KDIR := /lib/modules/$(shell uname -r)/build

obj-m := $(MODULE_NAME).o
$(MODULE_NAME)-objs = $(SRC:.c=.o)

PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
