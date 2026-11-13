# Makefile - hmmqtt, HomeMatic (BidCos-RF) via CUL to MQTT
#
# built / installed by the main Makefile with 'HOMEMATIC = 1' in Make.user
#

include ../Make.config

HMTARGET  = hmmqtt
LIBS     += $(shell $(SQLCFG) --libs_r) -lrt -lcrypto -lcurl -lpthread -luuid
CFLAGS   += $(shell $(SQLCFG) --include)

HMOBJS    = hmmqtt.o bidcos.o cul.o devices.o
LIBHORCHI = ../lib/libhorchi.a
LFLAGS   += -Wl,--as-needed

# header dependencies (*.d)

CFLAGS   += -MMD -MP

all: $(HMTARGET)

$(HMTARGET): $(HMOBJS) $(LIBHORCHI)
	$(doLink) $(HMOBJS) $(LIBHORCHI) $(LIBS) -o $@

-include $(HMOBJS:.o=.d)

.PHONY: FORCE

$(LIBHORCHI): FORCE
	@$(MAKE) -C ../lib --no-print-directory

install: $(HMTARGET)
	@echo install $(HMTARGET)
	install --mode=755 -D $(HMTARGET) $(BINDEST)/
	@$(call installUnit,hmmqtt.service,hmmqtt.service)
	install --mode=644 -D 77-cul-mm-ignore.rules $(DESTDIR)/etc/udev/rules.d/77-cul-mm-ignore.rules
	-test -n "$(DESTDIR)" || udevadm control --reload-rules
	mkdir -p $(DESTDIR)/var/lib/hmmqtt
	if ! test -f $(DESTDIR)/var/lib/hmmqtt/devices.json; then \
		install --mode=644 -D devices.json $(DESTDIR)/var/lib/hmmqtt/devices.json; \
	fi
	if ! test -f $(DESTDIR)/etc/default/hmmqtt; then \
		install --mode=644 -D hmmqtt.default $(DESTDIR)/etc/default/hmmqtt; \
		echo "!! Adjust settings in $(DESTDIR)/etc/default/hmmqtt (CUL device, hmId) !!"; \
	fi

clean:
	rm -f *.o *.d $(HMTARGET)
