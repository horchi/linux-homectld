
NAME    = w1-gpio-cansleep
VERSION = 1.0
SRC     = /usr/src/$(NAME)-$(VERSION)

obj-m := w1_gpio_cansleep.o
KDIR  := /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install:
	apt-get install -y dkms
	dkms status $(NAME)/$(VERSION) 2>/dev/null | grep -q $(NAME) && \
	    dkms remove $(NAME)/$(VERSION) --all 2>/dev/null || true
	rm -rf $(SRC)
	mkdir -p $(SRC)
	cp w1_gpio_cansleep.c $(SRC)/
	cp dkms.conf          $(SRC)/
	cp Makefile           $(SRC)/
	dkms add     $(NAME)/$(VERSION)
	dkms build   $(NAME)/$(VERSION)
	dkms install $(NAME)/$(VERSION)
	@echo "Done. Module active for kernel $$(uname -r), rebuilt automatically on kernel updates."

uninstall:
	dkms remove $(NAME)/$(VERSION) --all 2>/dev/null || true
	rm -rf $(SRC)
