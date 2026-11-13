#
# Makefile - libhorchi.a, the static lib of homectld and its services (modules, homematic)
#
# See the README file for copyright information and how to reach the author.
#
#  built by the main Makefile and the modules (Make.module) on demand, the linker takes
#  only the objects of the archive which are used by the program
#

include ../Make.config

LIBTARGET = libhorchi.a

LIBOBJS   = common.o db.o dbdict.o systemdctl.o serial.o curl.o thread.o json.o lua.o tcpchannel.o \
            mqtt.o mqtt_c.o mqtt_pal.o gpio.o

CFLAGS   += $(shell $(SQLCFG) --include)
LIBS     += $(shell $(SQLCFG) --libs_r) -lrt -lcrypto -lcurl -lpthread -luuid

# header dependencies (*.d)

CFLAGS   += -MMD -MP

all: $(LIBTARGET)

$(LIBTARGET): $(LIBOBJS)
	@echo Building lib ...
	@rm -f $@
	@$(doLib) $@ $(LIBOBJS) 2>/dev/null

-include $(LIBOBJS:.o=.d)

%.o: %.c
	@echo Compile "lib/$*" ..
	$(doCompile) -o $@ $<

# test / tools

gpiotst: gpio-test.o $(LIBTARGET)
	$(doLink) gpio-test.o $(LIBTARGET) $(LIBS) -o $@

tst: demo.o $(LIBTARGET)
	$(doLink) demo.o $(LIBTARGET) $(LIBS) -o $@

sdclient: sdclient.o $(LIBTARGET)
	$(doLink) sdclient.o $(LIBTARGET) $(LIBS) -o $@

blescan: blescan.o $(LIBTARGET)
	$(doLink) blescan.o $(LIBTARGET) $(LIBS) -lbluetooth -o $@

clean:
	rm -f *.o *.d *~ core $(LIBTARGET) gpiotst tst sdclient blescan

cppchk:
	cppcheck --template="{file}:{line}:{severity}:{message}" --quiet --force *.c *.h
