# Makefile - hmmqtt, HomeMatic (BidCos-RF) via CUL to MQTT
#
# built / installed by the main Makefile with 'HOMEMATIC = 1' in Make.user
#

include ../Make.config

HMTARGET  = hmmqtt
LIBS     += $(shell $(SQLCFG) --libs_r) -lrt -lcrypto -lcurl -lpthread -luuid
CFLAGS   += $(shell $(SQLCFG) --include)

HMOBJS    = hmmqtt.o bidcos.o cul.o devices.o
LOBJS     = ../lib/common.o ../lib/json.o ../lib/thread.o ../lib/mqtt.o ../lib/mqtt_c.o ../lib/mqtt_pal.o

all: $(HMTARGET)

$(HMTARGET): $(HMOBJS) $(LOBJS)
	$(doLink) $(HMOBJS) $(LOBJS) $(LIBS) -o $@

$(HMOBJS): hmmqtt.h

$(LOBJS):
	(cd ..; $(MAKE) --no-print-directory $(subst ../,,$@))

install: $(HMTARGET)
	@echo install $(HMTARGET)
	install --mode=755 -D $(HMTARGET) $(BINDEST)/
	@$(call installUnit,hmmqtt.service,hmmqtt.service)
	mkdir -p $(DESTDIR)/var/lib/hmmqtt
	if ! test -f $(DESTDIR)/var/lib/hmmqtt/devices.json; then \
		install --mode=644 -D devices.json $(DESTDIR)/var/lib/hmmqtt/devices.json; \
	fi
	if ! test -f $(DESTDIR)/etc/default/hmmqtt; then \
		install --mode=644 -D hmmqtt.default $(DESTDIR)/etc/default/hmmqtt; \
		echo "!! Adjust settings in $(DESTDIR)/etc/default/hmmqtt (CUL device, hmId) !!"; \
	fi

clean:
	rm -f *.o $(HMTARGET)
