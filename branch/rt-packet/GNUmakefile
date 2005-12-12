obj-m :=rtpkb.o

KDIR :=/lib/modules/$(shell uname -r)/build
PWD :=$(shell pwd)

default:
	$(MAKE) -C $(KDIR) SUBDIR=$(PWD) M=$(PWD) modules
	
	    