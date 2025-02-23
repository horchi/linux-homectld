# Makefile
#
# See the README file for copyright information and how to reach the author.
#

include Make.config

HISTFILE = "HISTORY.h"

LIBS += $(shell $(SQLCFG) --libs_r) -lrt -lcrypto -lcurl -lpthread -luuid -llua5.3

VERSION      = $(shell grep 'define _VERSION ' $(HISTFILE) | awk '{ print $$3 }' | sed -e 's/[";]//g')
ARCHIVE      = $(TARGET)-$(VERSION)
DEB_BASE_DIR = /root/debs
DEB_DEST     = $(DEB_BASE_DIR)/$(TARGET)-$(VERSION)

LASTHIST     = $(shell grep '^20[0-3][0-9]' $(HISTFILE) | head -1)
LASTCOMMENT  = $(subst |,\n,$(shell sed -n '/$(LASTHIST)/,/^ *$$/p' $(HISTFILE) | tr '\n' '|'))
LASTTAG      = $(shell git describe --tags --abbrev=0)
BRANCH       = $(shell git rev-parse --abbrev-ref HEAD)
GIT_REV      = $(shell git describe --always 2>/dev/null)

# object files

LOBJS        = lib/db.o lib/dbdict.o lib/systemdctl.o lib/common.o lib/serial.o lib/curl.o lib/thread.o lib/json.o lib/lua.o lib/tcpchannel.o
MQTTOBJS     = lib/mqtt.o lib/mqtt_c.o lib/mqtt_pal.o

OBJS         = $(MQTTOBJS) $(LOBJS) main.o daemon.o wsactions.o hass.o websock.o webservice.o deconz.o lmc.o lmccom.o
OBJS        += growatt.o
OBJS        += specific.o gpio.o

W1OBJS       = w1.o gpio.o lib/common.o lib/thread.o $(MQTTOBJS)
BMSOBJS      = bms.o lib/common.o lib/thread.o lib/serial.o $(MQTTOBJS)
VOTROOBJS    = votro.o lib/common.o lib/thread.o lib/serial.o $(MQTTOBJS)
VICTRONOBJS  = victron.o lib/common.o lib/thread.o lib/json.o lib/serial.o lib/victron/vecom.o lib/victron/veframehandler.o $(MQTTOBJS)
I2COBJS      = i2cmqtt.o gpio.o lib/common.o lib/json.o lib/thread.o lib/i2c/i2c.o lib/i2c/mcp23017.o lib/i2c/ads1115.o lib/i2c/dht20.o $(MQTTOBJS)

CFLAGS      += $(shell $(SQLCFG) --include)

# main taget

all: $(TARGET) $(W1TARGET) $(BMSTARGET) $(VOTROTARGET) $(VICTRONTARGET) $(I2CTARGET) $(ARDUINO_IF_CMD)

# auto dependencies

MAKEDEP = $(CXX) -MM -MG
DEPFILE = .dependencies

$(DEPFILE): Makefile
	@$(MAKEDEP) $(CFLAGS) $(OBJS:%.o=%.c) $(VICTRONOBJS:%.o=%.c) > $@

-include $(DEPFILE)

# git revision

ifdef GIT_REV
   DEFINES += -DGIT_REV='"$(GIT_REV)"'
endif

# build rules

$(TARGET) : $(OBJS)
	$(doLink) $(OBJS) $(LIBS) -o $@

$(W1TARGET): $(W1OBJS)
	$(doLink) $(W1OBJS) $(LIBS) -o $@

$(BMSTARGET): $(BMSOBJS)
	$(doLink) $(BMSOBJS) $(LIBS) -o $@

$(VOTROTARGET): $(VOTROOBJS)
	$(doLink) $(VOTROOBJS) $(LIBS) -o $@

$(VICTRONTARGET): $(VICTRONOBJS)
	$(doLink) $(VICTRONOBJS) $(LIBS) -o $@

$(I2CTARGET): $(I2COBJS)
	$(doLink) $(I2COBJS) $(LIBS) -o $@

linstall: $(TARGET) $(W1TARGET) $(BMSTARGET) $(VOTROTARGET) $(VICTRONTARGET) $(I2CTARGET)
	make install-daemon
	make install-web
   ifdef MOPEKA
	   (cd mopeka; $(MAKE) install)
   endif
   ifdef THETFORD
	   (cd thetford; $(MAKE) install)
   endif
   ifdef WOMO
	   (cd contrib/womo; $(MAKE) install)
   endif

install:  linstall
	make install-systemd

install-daemon: install-config install-scripts
	@echo install $(TARGET)
   ifneq ($(DESTDIR),)
	   @cp -r contrib/DEBIAN $(DESTDIR)
	   @chown root:root -R $(DESTDIR)/DEBIAN
		sed -i s:"<VERSION>":"$(VERSION)":g $(DESTDIR)/DEBIAN/control
	   @mkdir -p $(DESTDIR)/usr/bin
   endif
	install --mode=755 -D $(TARGET) $(BINDEST)/
	install --mode=755 -D $(W1TARGET) $(BINDEST)/
	install --mode=755 -D $(BMSTARGET) $(BINDEST)/
	install --mode=755 -D $(VOTROTARGET) $(BINDEST)/
	install --mode=755 -D $(VICTRONTARGET) $(BINDEST)/
	install --mode=755 -D $(I2CTARGET) $(BINDEST)/
	mkdir -p $(DESTDIR)$(PREFIX)/share/$(TARGET)/
#	install --mode=644 -D arduino/build-nano-atmega328/ioctrl.hex $(DESTDIR)$(PREFIX)/share/$(TARGET)/nano-atmega328-ioctrl.hex

restart: $(TARGET) install
	systemctl restart $(TARGET)

install-systemd:
	@echo install systemd
	cat contrib/daemon.service | sed s:"<BINDEST>":"$(_BINDEST)":g | sed s:"<AFTER>":"$(INIT_AFTER)":g | sed s:"<TARGET>":"$(TARGET)":g | sed s:"<CLASS>":"$(CLASS)":g |install --mode=644 -C -D /dev/stdin $(SYSTEMDDEST)/$(TARGET).service
	cat contrib/w1mqtt.service | sed s:"<BINDEST>":"$(_BINDEST)":g | sed s:"<AFTER>":"$(INIT_AFTER)":g | install --mode=644 -C -D /dev/stdin $(SYSTEMDDEST)/w1mqtt.service
	cat contrib/bmsmqtt.service | sed s:"<BINDEST>":"$(_BINDEST)":g | sed s:"<AFTER>":"$(INIT_AFTER)":g | install --mode=644 -C -D /dev/stdin $(SYSTEMDDEST)/bmsmqtt.service
	cat contrib/votromqtt.service | sed s:"<BINDEST>":"$(_BINDEST)":g | sed s:"<AFTER>":"$(INIT_AFTER)":g | install --mode=644 -C -D /dev/stdin $(SYSTEMDDEST)/votromqtt.service
	cat contrib/i2cmqtt.service | sed s:"<BINDEST>":"$(_BINDEST)":g | sed s:"<AFTER>":"$(INIT_AFTER)":g | install --mode=644 -C -D /dev/stdin $(SYSTEMDDEST)/i2cmqtt.service
	cat contrib/victronmqtt.service | sed s:"<BINDEST>":"$(_BINDEST)":g | sed s:"<AFTER>":"$(INIT_AFTER)":g | install --mode=644 -C -D /dev/stdin $(SYSTEMDDEST)/victronmqtt.service
	install --mode=664 -D contrib/mosquitto-log.service $(SYSTEMDDEST)/
	chmod a+r $(SYSTEMDDEST)/$(TARGET).service
	chmod a+r $(SYSTEMDDEST)/w1mqtt.service
	chmod a+r $(SYSTEMDDEST)/bmsmqtt.service
	chmod a+r $(SYSTEMDDEST)/votromqtt.service
	chmod a+r $(SYSTEMDDEST)/victronmqtt.service
	chmod a+r $(SYSTEMDDEST)/i2cmqtt.service
   ifeq ($(DESTDIR),)
	   systemctl daemon-reload
	   systemctl enable $(TARGET)
	   systemctl enable mosquitto-log.service
   endif

install-config:
	@echo install config
	if ! test -d $(CONFDEST); then \
	   mkdir -p $(CONFDEST); \
	   mkdir -p $(CONFDEST)/scripts.d; \
	   chmod a+rx $(CONFDEST); \
	fi
	install --mode=755 -D ./configs/sysctl $(CONFDEST)/scripts.d
	install --mode=755 -D ./configs/*.sh $(CONFDEST)/scripts.d
	if ! test -f $(DESTDIR)/etc/msmtprc; then \
	   install --mode=644 -D ./configs/msmtprc $(DESTDIR)/etc/; \
	fi
	if ! test -f $(CONFDEST)/daemon.conf; then \
	   cat configs/daemon.conf | sed s:"<NAME>":$(NAME):g | install --mode=644 -C -D /dev/stdin $(CONFDEST)/daemon.conf; \
	fi
	install --mode=644 -D ./configs/*.dat $(CONFDEST)/;
	mkdir -p $(DESTDIR)/etc/rsyslog.d
	cat contrib/rsyslog.conf | sed s:"<TARGET>":$(TARGET):g | install --mode=644 -C -D /dev/stdin $(DESTDIR)/etc/rsyslog.d/10-$(TARGET).conf; \
	mkdir -p $(DESTDIR)/etc/logrotate.d
	cat contrib/logrotate | sed s:"<TARGET>":$(TARGET):g | install --mode=644 -C -D /dev/stdin $(DESTDIR)/etc/logrotate.d/$(TARGET); \
	mkdir -p $(DESTDIR)/etc/default
	if ! test -f $(DESTDIR)/etc/default/w1mqtt; then \
	   cp contrib/w1mqtt $(DESTDIR)/etc/default/w1mqtt; \
	fi
	if ! test -f $(DESTDIR)/etc/default/bmsmqtt; then \
	   cp contrib/bmsmqtt $(DESTDIR)/etc/default/bmsmqtt; \
	fi
	if ! test -f $(DESTDIR)/etc/default/votromqtt; then \
	   cp contrib/votromqtt $(DESTDIR)/etc/default/votromqtt; \
	fi
	if ! test -f $(DESTDIR)/etc/default/victronmqtt; then \
	   cp contrib/victronmqtt $(DESTDIR)/etc/default/victronmqtt; \
	fi
	if ! test -f $(DESTDIR)/etc/default/i2cmqtt; then \
	   cp contrib/i2cmqtt $(DESTDIR)/etc/default/i2cmqtt; \
	fi

install-scripts:
	@echo install scripts
	if ! test -d $(BINDEST); then \
		mkdir -p "$(BINDEST)"; \
	   chmod a+rx $(BINDEST); \
	fi
	for f in ./scripts/*.sh; do \
		cp -v "$$f" $(BINDEST)/`basename "$$f"`; \
	done
	if ! test -f $(BINDEST)/fwpn; then \
	   cp  ./scripts/fwpn $(DESTDIR)/; \
	fi

iw: install-web

install-web:
	@echo install web
	(cd htdocs; $(MAKE) install)

dist: clean
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@mkdir $(TMPDIR)/$(ARCHIVE)
	@cp -a * $(TMPDIR)/$(ARCHIVE)
	@tar czf $(ARCHIVE).tgz -C $(TMPDIR) $(ARCHIVE)
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@echo Distribution package created as $(ARCHIVE).tgz

clean:
	rm -f */*.o */*/*.o *.o core* *~ */*~ */*/*~ lib/t *.jpg $(DEPFILE)
	rm -f $(TARGET) $(ARCHIVE).tgz
	rm -f com2

build: clean all

uninstall: clean-install

clean-install:
	rm -rf $(CONFDEST)
	rm -rf $(WEBDEST)
	rm -rf $(SYSTEMDDEST)/$(TARGET).service
	rm -rf $(SYSTEMDDEST)/w1mqtt.service
	rm -rf $(SYSTEMDDEST)/bmsmqtt.service
	rm -rf $(SYSTEMDDEST)/votromqtt.service
	rm -rf $(SYSTEMDDEST)/victronmqtt.service
	rm -rf $(SYSTEMDDEST)/i2cmqtt.service
	rm -rf $(DESTDIR)/etc/default/w1mqtt;
	rm -rf $(DESTDIR)/etc/default/bmsmqtt;
	rm -rf $(DESTDIR)/etc/default/votromqtt;
	rm -rf $(DESTDIR)/etc/default/victronmqtt;
	rm -rf $(DESTDIR)/etc/default/i2cmqtt;
	rm -rf $(DESTDIR)/etc/rsyslog.d/10-$(TARGET).conf
	rm -rf $(DESTDIR)/etc/logrotate.d/$(TARGET)
	rm -rf $(BINDEST)/$(TARGET)*
	rm -rf $(BINDEST)/$(W1TARGET)
	rm -rf $(BINDEST)/$(BMSTARGET)
	rm -rf $(BINDEST)/$(VOTROTARGET)
	rm -rf $(BINDEST)/$(VICTRONTARGET)
	rm -rf $(BINDEST)/$(I2CTARGET)

activate: install
	systemctl restart $(TARGET)
#	tail -f /var/log/$(TARGET).log

cppchk:
	cppcheck --enable=all $(CPPCHECK_SUPPRESS) --template="{file}:{line}:1 {severity}:{id}:{message}" --quiet --force --std=c++20 *.c; \

upload:
	avrdude -q -V -p atmega328p -D -c arduino -b 57600 -P $(AVR_DEVICE) -U flash:w:arduino/build-nano-atmega328/ioctrl.hex:i

build-deb:
	rm -rf $(DEB_DEST)
	make -s install-daemon DESTDIR=$(DEB_DEST) PREFIX=/usr INIT_AFTER=mysql.service
	make -s install-web DESTDIR=$(DEB_DEST) PREFIX=/usr
	dpkg-deb --build $(DEB_BASE_DIR)/$(TARGET)-$(VERSION)

publish-deb:
	echo 'put $(DEB_BASE_DIR)/$(TARGET)-${VERSION}.deb' | sftp -i ~/.ssh/id_rsa2 p7583735@home26485763.1and1-data.host:$(TARGET)
	echo 'put contrib/install-deb.sh' | sftp -i ~/.ssh/id_rsa2 p7583735@home26485763.1and1-data.host:$(TARGET)
	echo 'rm $(TARGET)-latest.deb' | sftp -i ~/.ssh/id_rsa2 p7583735@home26485763.1and1-data.host:$(TARGET)
	echo 'ln -s $(TARGET)-${VERSION}.deb $(TARGET)-latest.deb' | sftp -i ~/.ssh/id_rsa2 p7583735@home26485763.1and1-data.host:$(TARGET)
	echo 'chmod 644 $(TARGET)-${VERSION}.deb' | sftp -i ~/.ssh/id_rsa2 p7583735@home26485763.1and1-data.host:$(TARGET)
	echo 'chmod 755 install-deb.sh' | sftp -i ~/.ssh/id_rsa2 p7583735@home26485763.1and1-data.host:$(TARGET)

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
