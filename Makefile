#
# Makefile
#
# See the README file for copyright information and how to reach the author.
#
#

include Make.config

TARGET      = poold
HISTFILE    = "HISTORY.h"

LIBS  = $(shell mysql_config --libs_r) -lrt -lcrypto -lcurl -lpthread
LIBS += $(shell xml2-config --libs)
LIBS += -lwiringPi

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

LOBJS        =  lib/db.o lib/dbdict.o lib/common.o lib/serial.o lib/curl.o
OBJS         = $(LOBJS) main.o gpio.o w1.o hass.o
MQTTBJS      = lib/mqtt.c

CFLAGS    	+= $(shell mysql_config --include)
CFLAGS    	+= $(shell xml2-config --cflags)
DEFINES   	+= -DDEAMON=Poold -DUSEMD5
OBJS      	+= poold.o

OBJS    += $(MQTTBJS)
LIBS    += -lpaho-mqtt3cs
FINES   += -DMQTT_HASS

ifdef GIT_REV
   DEFINES += -DGIT_REV='"$(GIT_REV)"'
endif

# rules:

all: $(TARGET)

$(TARGET) : paho-mqtt $(OBJS)
	$(doLink) $(OBJS) $(LIBS) -o $@

install: $(TARGET) install-poold

install-poold: install-config # install-scripts
	@cp -p $(TARGET) $(BINDEST)
	make install-systemd
   ifneq ($(DESTDIR),)
	   @cp -r contrib/DEBIAN $(DESTDIR)
	   @chown root:root -R $(DESTDIR)/DEBIAN
		sed -i s:"<VERSION>":"$(VERSION)":g $(DESTDIR)/DEBIAN/control
	   @mkdir -p $(DESTDIR)/usr/lib
	   @mkdir -p $(DESTDIR)/usr/bin
	   @mkdir -p $(DESTDIR)/usr/share/man/man1
   endif

inst_restart: $(TARGET) install-config # install-scripts
	systemctl stop poold
	@cp -p $(TARGET) $(BINDEST)
	systemctl start poold

install-systemd:
	cat contrib/poold.service | sed s:"<BINDEST>":"$(_BINDEST)":g | sed s:"<AFTER>":"$(INIT_AFTER)":g | install --mode=644 -C -D /dev/stdin $(SYSTEMDDEST)/poold.service
	chmod a+r $(SYSTEMDDEST)/poold.service
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

cppchk:
	cppcheck --template="{file}:{line}:{severity}:{message}" --language=c++ --force *.c *.h

paho-mqtt:
	if [ ! -d ~/build/paho.mqtt.c ]; then \
		mkdir -p ~/build; \
		cd ~/build; \
		git clone https://github.com/eclipse/paho.mqtt.c.git; \
		sed -i '/if test ! -f ..DESTDIR..{libdir}.lib..MQTTLIB_C..so..{MAJOR_VERSION.; then ln -s/d' ~/build/paho.mqtt.c/Makefile; \
		sed -i '/\- ..INSTALL_DATA. .{blddir}.doc.MQTTClient.man.man3.MQTTClient.h.3 ..DESTDIR..{man3dir}/d' ~/build/paho.mqtt.c/Makefile; \
		sed -i '/\- ..INSTALL_DATA. .{blddir}.doc.MQTTAsync.man.man3.MQTTAsync.h.3 ..DESTDIR..{man3dir}/d' ~/build/paho.mqtt.c/Makefile; \
		sed -i s/'int rc1 = sem_getvalue.sem, .val.;.*'/'sem_getvalue(sem, \&val);'/ ~/build/paho.mqtt.c/src/Thread.c; \
		sed -i s/'rm [$$]'/'rm -f $$'/g ~/build/paho.mqtt.c/Makefile; \
	fi
	cd ~/build/paho.mqtt.c; \
	make -s; \
	sudo rm -f /usr/local/lib/libpaho*; \
	sudo make -s uninstall prefix=/usr; \
	sudo make -s install prefix=/usr

build-deb:
	rm -rf $(DEB_DEST)
	make -s install-poold DESTDIR=$(DEB_DEST) PREFIX=/usr INIT_AFTER=mysql.service
	cd ~/build/paho.mqtt.c; \
	make -s install DESTDIR=$(DEB_DEST) prefix=/usr
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
lib/mqtt.o      :  lib/mqtt.c      lib/mqtt.h

main.o          :  main.c          $(HEADER) poold.h
poold.o         :  poold.c         $(HEADER) poold.h w1.h lib/mqtt.h
w1.o            :  w1.c            $(HEADER) w1.h
gpio.o          :  gpio.c          $(HEADER)
hass.o          :  hass.c          poold.h

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
