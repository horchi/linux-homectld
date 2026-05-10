
obj-m := w1_gpio_cansleep.o
KDIR := /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE)  M=$(PWD) clean
