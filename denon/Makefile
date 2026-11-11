
include ../Make.config

.PHONY: install install-deps clean

# only the python standard library and paho-mqtt (apt) are needed

install: install-deps
	@echo denon service
	install --mode=755 -D denon.py $(BINDEST)/
	@$(call installUnit,denon.service,denon.service)
	if ! test -f $(DESTDIR)/etc/default/denon2mqtt; then \
		cp denon2mqtt $(DESTDIR)/etc/default/denon2mqtt; \
		echo "!! Adjust settings in $(DESTDIR)/etc/default/denon2mqtt !!"; \
	fi
   ifeq ($(DESTDIR),)
	   @systemctl is-enabled -q denon.service || systemctl enable denon.service
   endif

install-deps:
	@if ! python3 -c 'import paho.mqtt.client' 2>/dev/null; then \
		echo "Installing python3-paho-mqtt .."; \
		apt-get install -y python3 python3-paho-mqtt; \
	fi

clean:
	rm -rf __pycache__
