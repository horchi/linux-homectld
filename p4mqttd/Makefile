# Makefile - p4mqttd, Fröling S 3200 to MQTT, and the command line tool p4
#
# STAND ALONE: this directory is built and installed on its own, 'make' and 'make install' here
# (e.g. on a Raspberry Pi after cloning the repository), nothing of the homectld is built.
# It uses the sources of ../lib directly and compiles them into ./obj, so only these
# libraries are needed (apt install build-essential pkg-config libssl-dev libjansson-dev zlib1g-dev):
# no database, no libwebsockets, no lua.
#
# The main Makefile of the homectld calls it with 'P4 = 1' in Make.user.
#

-include ../Make.user

P4DTARGET   = p4mqttd
P4TARGET    = p4

PREFIX     ?= /usr/local
BINDEST     = $(DESTDIR)$(PREFIX)/bin
_BINDEST    = $(PREFIX)/bin
SYSTEMDDEST = $(DESTDIR)/etc/systemd/system
RSYSLOGDEST = $(DESTDIR)/etc/rsyslog.d
LOGROTDEST  = $(DESTDIR)/etc/logrotate.d

CPP         = g++
DEBUG      ?= 1

# the sources are C++ in files named .c (as everywhere in this repository)

CFLAGS     += $(shell pkg-config --cflags jansson)
CFLAGS     += -pipe -pthread -fPIC -std=c++2a -x c++ -MMD -MP -I..
CFLAGS     += -Wall -Wreturn-type -Wformat -pedantic -Wunused-variable -Wunused-label -Wunused-value -Wunused-function \
              -Wno-long-long -Wno-unused-parameter -Wno-deprecated-enum-enum-conversion -Wno-psabi
CFLAGS     += -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE -D_GNU_SOURCE

# USEMD5: md5 of lib/common (openssl), USEJSON: lib/json (jansson)

DEFINES     = -DTARGET='"$(P4DTARGET)"' -DINSTANCE='"homectld"' -DLOG_PREFIX='""' -DUSEMD5 -DUSEJSON

ifneq ($(DEBUG),0)
   CFLAGS  += -ggdb -O0
else
   CFLAGS  += -O2
endif

LIBS        = $(shell pkg-config --libs jansson) -lcrypto -lz -lrt -lpthread

OBJDIR      = obj

COMMON      = $(OBJDIR)/p4io.o $(OBJDIR)/service.o
P4DOBJS     = $(OBJDIR)/p4mqttd.o $(COMMON)
P4OBJS      = $(OBJDIR)/p4cmd.o $(COMMON)
LOBJS       = $(addprefix $(OBJDIR)/, common.o json.o thread.o serial.o mqtt.o mqtt_c.o mqtt_pal.o)

all: $(P4DTARGET) $(P4TARGET)

$(P4DTARGET): $(P4DOBJS) $(LOBJS)
	$(CPP) -rdynamic $^ $(LIBS) -o $@

$(P4TARGET): $(P4OBJS) $(LOBJS)
	$(CPP) -rdynamic $^ $(LIBS) -o $@

$(OBJDIR)/%.o: %.c
	@mkdir -p $(OBJDIR)
	@echo Compile "$*" ..
	$(CPP) -c $(CFLAGS) $(DEFINES) -o $@ $<

$(OBJDIR)/%.o: ../lib/%.c
	@mkdir -p $(OBJDIR)
	@echo Compile "lib/$*" ..
	$(CPP) -c $(CFLAGS) $(DEFINES) -o $@ $<

-include $(wildcard $(OBJDIR)/*.d)

# install a systemd unit only if its content changed, systemd is reloaded in this case

define installUnit
tmp=$$(mktemp); \
sed -e s:"<BINDEST>":"$(_BINDEST)":g $(1) > $$tmp; \
if ! cmp -s $$tmp $(SYSTEMDDEST)/$(2); then \
   install --mode=644 -D $$tmp $(SYSTEMDDEST)/$(2); \
   echo "  $(2) updated"; \
   if test -z "$(DESTDIR)"; then systemctl daemon-reload; fi; \
fi; \
rm -f $$tmp
endef

install: all
	@echo install $(P4DTARGET) and $(P4TARGET)
	install --mode=755 -D $(P4DTARGET) $(BINDEST)/$(P4DTARGET)
	install --mode=755 -D $(P4TARGET) $(BINDEST)/$(P4TARGET)
	@$(call installUnit,p4mqttd.service,p4mqttd.service)
	if ! test -f $(DESTDIR)/etc/default/p4mqttd; then \
		install --mode=644 -D p4mqttd.default $(DESTDIR)/etc/default/p4mqttd; \
		echo "!! Adjust settings in $(DESTDIR)/etc/default/p4mqttd (serial device) !!"; \
	fi
	@if ! cmp -s p4mqttd.rsyslog $(RSYSLOGDEST)/10-p4mqttd.conf; then \
		install --mode=644 -D p4mqttd.rsyslog $(RSYSLOGDEST)/10-p4mqttd.conf; \
		echo "  rsyslog config 10-p4mqttd.conf updated"; \
		if test -z "$(DESTDIR)"; then \
			if systemctl is-active -q rsyslog.service; then \
				systemctl restart rsyslog.service; \
			else \
				echo "!! rsyslog not running, install it (apt install rsyslog) to get /var/log/p4mqttd.log !!"; \
			fi; \
		fi; \
	fi
	install --mode=644 -C -D p4mqttd.logrotate $(LOGROTDEST)/p4mqttd

uninstall:
	rm -f $(BINDEST)/$(P4DTARGET) $(BINDEST)/$(P4TARGET)
	rm -f $(SYSTEMDDEST)/p4mqttd.service
	rm -f $(DESTDIR)/etc/default/p4mqttd
	rm -f $(RSYSLOGDEST)/10-p4mqttd.conf $(LOGROTDEST)/p4mqttd

clean:
	rm -rf $(OBJDIR) $(P4DTARGET) $(P4TARGET)

.PHONY: all install uninstall clean
