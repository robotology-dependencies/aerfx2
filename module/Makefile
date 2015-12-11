KVER=$(shell uname -r)
KSRC=/lib/modules/$(KVER)/build
obj-m += aerfx2.o
TARGET=aerfx2.ko

# remove this and you'll get burnt on the stake:
EXTRA_CFLAGS += -Werror

all: aerfx2.april11.c  aerfx2.april2819.c  aerfx2.april28.c  aerfx2.c  aerfx2.c_bck  aerfx2.ko_bck
	make -C $(KSRC) M=$(PWD) modules

clean:
	make -C $(KSRC) M=$(PWD) clean

install: $(TARGET)
	mkdir -p /lib/modules/${KERNEL_NAME}/iCubDrivers/aerfx2/module/
	cp $(TARGET) /lib/modules/${KERNEL_NAME}/iCubDrivers/aerfx2/module/

uninstall:
	-rm -rf lib/modules/${KERNEL_NAME}/iCubDrivers/aerfx2/module/
