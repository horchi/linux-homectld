
include ../../Make.config

TARGET = kenwood-remote
ARDUINO_CLI = /usr/local/bin/arduino-cli

# ESP32-C3 Super Mini (Rueckfallebene Seeed XIAO ESP32C3: esp32:esp32:XIAO_ESP32C3, Pins in config-tmpl.h anpassen)
BOARD_TAG = esp32:esp32:nologo_esp32c3_super_mini

# Serieller Port für den Upload (KENWOOD_REMOTE_PORT aus Make.user), kein OTA:
# das Bedienteil ist nie mit dem WLAN verbunden
KENWOOD_REMOTE_PORT ?= /dev/ttyACM0
MONITOR_PORT = $(KENWOOD_REMOTE_PORT)

# Zielwerte aus Make.user: KENWOOD_MAC (WLAN-MAC des Kenwood-ESP), WIFI_CHANNEL (fester Kanal des Routers)
WIFI_CHANNEL ?= 1

CXXFLAGS = --build-property compiler.cpp.extra_flags="-Wno-deprecated-declarations"

BUILD_DIR = build

.PHONY: all compile upload clean

all: compile

compile: patch-config
	@$(MAKE) --no-print-directory $(BUILD_DIR)/$(TARGET).ino.bin

# nur neu bauen wenn Sketch oder Konfiguration neuer sind als die Firmware

$(BUILD_DIR)/$(TARGET).ino.bin: $(TARGET).ino config.h Makefile
	@echo "Kompiliere $(TARGET) .."
	mkdir -p $(BUILD_DIR)
	$(ARDUINO_CLI) compile --fqbn $(BOARD_TAG) $(CXXFLAGS) --build-path $(BUILD_DIR) .

patch-config:
	@test -n "$(KENWOOD_MAC)" || (echo "KENWOOD_MAC in Make.user setzen (WLAN-MAC des Kenwood-ESP)"; exit 1)
	@cat config-tmpl.h | \
	sed 's|<KENWOOD_MAC>|"$(KENWOOD_MAC)"|g' | \
	sed 's|<WIFI_CHANNEL>|$(WIFI_CHANNEL)|g' > config.h.new || (rm -f config.h.new; exit 1)
	@if cmp -s config.h.new config.h; then rm config.h.new; else echo "Erzeuge config.h .."; mv config.h.new config.h; fi

upload:
	@echo "Flashing $(TARGET) auf $(MONITOR_PORT) .."
	$(ARDUINO_CLI) upload -p $(MONITOR_PORT) --fqbn $(BOARD_TAG) --input-dir $(BUILD_DIR)

clean:
	@echo "Bereinige Build-Verzeichnis .."
	rm -f *.o *~
	rm -rf $(BUILD_DIR) config.h config.h.new

install-deps:
	@echo "Aktualisiere Board-Index und installiere ESP32-Core .."
	$(ARDUINO_CLI) core update-index
	$(ARDUINO_CLI) core install esp32:esp32
	@echo "Einrichtung abgeschlossen (keine externen Bibliotheken noetig)"
