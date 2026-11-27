include ../Make.config

# openHASP Firmware fuer das CrowPanel Advance 5.0 (ESP32-S3), Details in README.md
#
#  make               Firmware bauen (build.sh, beim ersten Mal ~15 min)
#  make upload        seriell flashen (HASP_PORT), loescht den Flash komplett inkl. WLAN/MQTT
#                     Einstellungen - nur beim ersten Mal oder bei einem Wechsel der Partitionen
#  make upload-ota    Firmware ueber WLAN (HASP_OTA_HOST, HASP_OTA_PWD = Web-Passwort des Panels)
#  make monitor       serielle Konsole (picocom)
#  make install-deps  venv mit PlatformIO und esptool anlegen

TARGET = openhasp

# Serieller Port (HASP_PORT aus Make.user) und IP/Hostname des Panels fuer OTA
# (HASP_OTA_HOST, HASP_OTA_PWD aus Make.user)
HASP_PORT ?= /dev/ttyUSB0
MONITOR_PORT = $(HASP_PORT)

# esptool: aus dem venv (make install-deps), sonst aus dem PATH
ESPTOOL ?= $(shell test -x venv/bin/esptool && echo $(CURDIR)/venv/bin/esptool || command -v esptool || echo esptool)

.PHONY: all compile upload upload-ota monitor clean install-deps

all: compile

compile:
	./build.sh

upload:
	@test -n "$$(ls firmware/*_full_*.bin 2>/dev/null)" || (echo "keine Firmware in firmware/, erst 'make'"; exit 1)
	@command -v $(ESPTOOL) >/dev/null || (echo "esptool fehlt, 'make install-deps'"; exit 1)
	@echo "Flashing $(TARGET) auf $(MONITOR_PORT) .."
	ESPTOOL=$(ESPTOOL) ./flash.sh $(MONITOR_PORT)

# das Passwort geht unverändert ($(value ...), Umgebung) an ota.sh, ein '$' wird sonst ersetzt

upload-ota: export HASP_PASS = $(value HASP_OTA_PWD)

upload-ota:
	@test -n "$(HASP_OTA_HOST)" || (echo "HASP_OTA_HOST in Make.user setzen (IP des Panels)"; exit 1)
	@echo "Flashing $(TARGET) per OTA auf $(HASP_OTA_HOST) .."
	./ota.sh $(HASP_OTA_HOST)

monitor:
	picocom -b 115200 --omap crcrlf $(MONITOR_PORT)

clean:
	@echo "Bereinige Build-Verzeichnis .."
	rm -f *~
	rm -rf src/build_output

install-deps:
	@echo "Lege venv mit PlatformIO und esptool an .."
	test -x venv/bin/pip || python3 -m venv venv
	venv/bin/pip install -q platformio esptool
	@echo "Einrichtung abgeschlossen!"
