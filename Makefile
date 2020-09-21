# Makefile
#
# See the README file for copyright information and how to reach the author.
#

include Make.config

TARGET         = poold
W1TARGET       = w1mqtt
ARDUINO_IF_CMD = poolai
HISTFILE       = "HISTORY.h"

LIBS += $(shell mysql_config --libs_r) -lrt -lcrypto -lcurl -lpthread -luuid

DEFINES += -D_GNU_SOURCE -DTARGET='"$(TARGET)"'

VERSION      = $(shell grep 'define _VERSION ' $(HISTFILE) | awk '{ print $$3 }' | sed -e 's/[";]//g')
ARCHIVE      = $(TARGET)-$(VERSION)
DEB_BASE_DIR = /root/debs
DEB_DEST     = $(DEB_BASE_DIR)/poold-$(VERSION)

LASTHIST     = $(shell grep '^20[0-3][0-9]' $(HISTFILE) | head -1)
LASTCOMMENT  = $(subst |,\n,$(shell sed -n '/$(LASTHIST)/,/^ *$$/p' $(HISTFILE) | tr '\n' '|'))
LASTTAG      = $(shell git describe --tags --abbrev=0)
BRANCH       = $(shell git rev-parse --abbrev-ref HEAD)
GIT_REV      = $(shell git describe --always 2>/dev/null)

# object files

LOBJS        = lib/db.o lib/dbdict.o lib/common.o lib/serial.o lib/curl.o lib/thread.o lib/json.o
MQTTOBJS     = lib/mqtt.o lib/mqtt_c.o lib/mqtt_pal.o
OBJS         = $(MQTTOBJS) $(LOBJS) main.o gpio.o hass.o websock.o arduinoif.o

CFLAGS    	+= $(shell mysql_config --include)
DEFINES   	+= -DDEAMON=Poold
OBJS      	+= poold.o
W1OBJS      = w1.o lib/common.o lib/thread.o $(MQTTOBJS)
AIOBJS      = aicmd.o arduinoif.o lib/common.o lib/serial.o

ifdef WIRINGPI
  LIBS += -lwiringPi
endif

ifdef GIT_REV
   DEFINES += -DGIT_REV='"$(GIT_REV)"'
endif

# rules:

all: $(TARGET) $(W1TARGET) $(ARDUINO_IF_CMD)

$(TARGET) : $(OBJS) $(W1TARGET)
	$(doLink) $(OBJS) $(LIBS) -o $@

$(W1TARGET): $(W1OBJS)
	$(doLink) $(W1OBJS) $(LIBS) -o $@

$(ARDUINO_IF_CMD): $(AIOBJS)
	$(doLink) $(AIOBJS) $(LIBS) -o $@

install: $(TARGET) $(W1TARGET) install-poold install-web

install-poold: install-config install-scripts
   ifneq ($(DESTDIR),)
	   @cp -r contrib/DEBIAN $(DESTDIR)
	   @chown root:root -R $(DESTDIR)/DEBIAN
		sed -i s:"<VERSION>":"$(VERSION)":g $(DESTDIR)/DEBIAN/control
	   @mkdir -p $(DESTDIR)/usr/bin
   endif
	install --mode=755 -D $(TARGET) $(BINDEST)/
	install --mode=755 -D $(W1TARGET) $(BINDEST)/
	make install-systemd

inst_restart: $(TARGET) install-config # install-scripts
	systemctl stop poold
	@cp -p $(TARGET) $(BINDEST)
	systemctl start poold

install-systemd:
	cat contrib/poold.service | sed s:"<BINDEST>":"$(_BINDEST)":g | sed s:"<AFTER>":"$(INIT_AFTER)":g | install --mode=644 -C -D /dev/stdin $(SYSTEMDDEST)/poold.service
	cat contrib/w1mqtt.service | sed s:"<BINDEST>":"$(_BINDEST)":g | sed s:"<AFTER>":"$(INIT_AFTER)":g | install --mode=644 -C -D /dev/stdin $(SYSTEMDDEST)/w1mqtt.service
	chmod a+r $(SYSTEMDDEST)/poold.service
	chmod a+r $(SYSTEMDDEST)/w1mqtt.service
   ifeq ($(DESTDIR),)
	   systemctl daemon-reload
	   systemctl enable poold
   endif

install-config:
	if ! test -d $(CONFDEST); then \
	   mkdir -p $(CONFDEST); \
	   mkdir -p $(CONFDEST)/scripts.d; \
	   chmod a+rx $(CONFDEST); \
	fi
	if ! test -f $(CONFDEST)/poold.conf; then \
	   install --mode=644 -D ./configs/poold.conf $(CONFDEST)/; \
	fi
	install --mode=644 -D ./configs/poold.dat $(CONFDEST)/;

install-scripts:
	if ! test -d $(BINDEST); then \
		mkdir -p "$(BINDEST)" \
	   chmod a+rx $(BINDEST); \
	fi
	install -D ./scripts/poold-* $(BINDEST)/

iw: install-web

install-web:
	(cd htdocs; $(MAKE) install)

dist: clean
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@mkdir $(TMPDIR)/$(ARCHIVE)
	@cp -a * $(TMPDIR)/$(ARCHIVE)
	@tar czf $(ARCHIVE).tgz -C $(TMPDIR) $(ARCHIVE)
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@echo Distribution package created as $(ARCHIVE).tgz

clean:
	rm -f */*.o *.o core* *~ */*~ lib/t *.jpg
	rm -f $(TARGET) $(ARCHIVE).tgz
	rm -f com2

activate: install
	systemctl restart $(TARGET)
	tail -f /var/log/$(TARGET).log

cppchk:
	cppcheck --template="{file}:{line}:{severity}:{message}" --language=c++ --force *.c *.h

upload:
	avrdude -q -V -p atmega328p -D -c arduino -b 57600 -P $(AVR_DEVICE) -U flash:w:build-nano-atmega328.arduino.hex:i

build-deb:
	rm -rf $(DEB_DEST)
	make -s install-poold DESTDIR=$(DEB_DEST) PREFIX=/usr INIT_AFTER=mysql.service
	dpkg-deb --build $(DEB_BASE_DIR)/poold-$(VERSION)

publish-deb:
	echo 'put $(DEB_BASE_DIR)/poold-${VERSION}.deb' | sftp -i ~/.ssh/id_rsa2 p7583735@home26485763.1and1-data.host:poold
	echo 'put contrib/install-deb.sh' | sftp -i ~/.ssh/id_rsa2 p7583735@home26485763.1and1-data.host:poold
	echo 'rm poold-latest.deb' | sftp -i ~/.ssh/id_rsa2 p7583735@home26485763.1and1-data.host:poold
	echo 'ln -s poold-${VERSION}.deb poold-latest.deb' | sftp -i ~/.ssh/id_rsa2 p7583735@home26485763.1and1-data.host:poold
	echo 'chmod 644 poold-${VERSION}.deb' | sftp -i ~/.ssh/id_rsa2 p7583735@home26485763.1and1-data.host:poold
	echo 'chmod 755 install-deb.sh' | sftp -i ~/.ssh/id_rsa2 p7583735@home26485763.1and1-data.host:poold

#***************************************************************************
# dependencies
#***************************************************************************

HEADER = lib/db.h lib/dbdict.h lib/common.h

lib/common.o    :  lib/common.c    $(HEADER)
lib/db.o        :  lib/db.c        $(HEADER)
lib/dbdict.o    :  lib/dbdict.c    $(HEADER)
lib/curl.o      :  lib/curl.c      $(HEADER)
lib/serial.o    :  lib/serial.c    $(HEADER) lib/serial.h
lib/mqtt.o      :  lib/mqtt.c      lib/mqtt.h lib/mqtt_c.h
lib/mqtt_c.o    :  lib/mqtt_c.c    lib/mqtt_c.h
lib/mqtt_pal.o  :  lib/mqtt_pal.c  lib/mqtt_c.h

main.o          :  main.c          $(HEADER) poold.h HISTORY.h
poold.o         :  poold.c         $(HEADER) poold.h w1.h lib/mqtt.h websock.h aiservice.h
w1.o            :  w1.c            $(HEADER) w1.h lib/mqtt.h
gpio.o          :  gpio.c          $(HEADER)
hass.o          :  hass.c          poold.h
websock.o       :  websock.c       websock.h
arduinoif.o     :  arduinoif.c     arduinoif.h lib/serial.h aiservice.h
aicmd.o         :  aicmd.c         arduinoif.h lib/serial.h aiservice.h

# ------------------------------------------------------
# Git / Versioning / Tagging
# ------------------------------------------------------

vcheck:
	git fetch
	if test "$(LASTTAG)" = "$(VERSION)"; then \
		echo "Warning: tag/version '$(VERSION)' already exists, update HISTORY first. Aborting!"; \
		exit 1; \
	fi

push: vcheck
	echo "tagging git with $(VERSION)"
	git tag $(VERSION)
	git push --tags
	git push

commit: vcheck
	git commit -m "$(LASTCOMMENT)" -a

git: commit push

showv:
	@echo "Git ($(BRANCH)):\\n  Version: $(LASTTAG) (tag)"
	@echo "Local:"
	@echo "  Version: $(VERSION)"
	@echo "  Change:"
	@echo -n "   $(LASTCOMMENT)"
