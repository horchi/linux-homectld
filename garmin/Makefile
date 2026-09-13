include ../Make.config

# the python module 'garminconnect' is installed into a venv (the system python is 'externally managed')

VENV = $(DESTDIR)$(PREFIX)/lib/homectld-garmin
_VENV = $(PREFIX)/lib/homectld-garmin

install:
	@echo garmin activities
	if ! test -d $(VENV); then \
		python3 -m venv $(VENV); \
	fi
	$(VENV)/bin/pip install --quiet --upgrade garminconnect
	sed "1s:.*:#!$(_VENV)/bin/python3:" garmin.py | install --mode=755 -D /dev/stdin $(BINDEST)/garmin.py

clean:
