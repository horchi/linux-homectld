
include ../Make.config

install:
	@echo thetford service
	install --mode=755 -D thetford.py $(BINDEST)/
	@$(call installUnit,thetford.service,thetford.service)
	if ! test -f $(DESTDIR)/etc/default/thetford2mqtt; then \
		cp thetford2mqtt $(DESTDIR)/etc/default/thetford2mqtt; \
		echo "!! Adjust settings in $(DESTDIR)/etc/default/thetford2mqtt !!"; \
	fi
	if ! test -f $(DESTDIR)/etc/udev/rules.d/50-USBlini.rules; then \
		echo 'SUBSYSTEM=="usb", ATTRS{product}=="USBlini", MODE="0666"' > $(DESTDIR)/etc/udev/rules.d/50-USBlini.rules; \
		udevadm control --reload-rules; \
	fi
   ifeq ($(DESTDIR),)
	   @systemctl is-enabled -q thetford.service || systemctl enable thetford.service
	   systemctl start thetford.service
   endif
