.PHONY: install install-deps clean

include ../Make.config

# samsungtvws isn't packaged by the distribution, it is installed with paho-mqtt into a
# venv (the system python is 'externally managed')

VENV = $(DESTDIR)$(PREFIX)/lib/homectld-samsungtv
_VENV = $(PREFIX)/lib/homectld-samsungtv

install: install-deps
	@echo samsungtv service
	if ! test -d $(VENV); then \
		python3 -m venv $(VENV); \
	fi
	$(VENV)/bin/pip install --quiet --upgrade samsungtvws paho-mqtt
	sed "1s:.*:#!$(_VENV)/bin/python3:" samsungtv.py | install --mode=755 -D /dev/stdin $(BINDEST)/samsungtv.py
	@$(call installUnit,samsungtv.service,samsungtv.service)
	if ! test -f $(DESTDIR)/etc/default/samsungtv2mqtt; then \
		cp samsungtv2mqtt $(DESTDIR)/etc/default/samsungtv2mqtt; \
		echo "!! Adjust settings in $(DESTDIR)/etc/default/samsungtv2mqtt !!"; \
	fi
   ifeq ($(DESTDIR),)
	   @systemctl is-enabled -q samsungtv.service || systemctl enable samsungtv.service
   endif

# python3-venv (ensurepip) is needed to create the venv

install-deps:
	@if ! python3 -c 'import ensurepip' 2>/dev/null; then \
		echo "Installing python3-venv .."; \
		apt-get install -y python3-venv python3-pip; \
	fi

clean:
	rm -rf __pycache__
