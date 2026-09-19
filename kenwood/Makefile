
include ../Make.config

TARGET = kenwood
ARDUINO_CLI = arduino-cli

# Board-Identifikator für den ESP32
BOARD_TAG = esp32:esp32:esp32
# alternativ passend zum Wemos MINI D1 ESP32:
# BOARD_TAG = esp32:esp32:d1_mini32

# Serieller Port für den Upload (KENWOOD_PORT aus Make.user) und
# IP/Hostname des ESP32 für OTA (KENWOOD_OTA_HOST, KENWOOD_OTA_PWD aus Make.user)
KENWOOD_PORT ?= /dev/ttyACM0
MONITOR_PORT = $(KENWOOD_PORT)
ESPOTA = $(shell ls -d $(HOME)/.arduino15/packages/esp32/hardware/esp32/*/tools/espota.py | tail -1)

CXXFLAGS = --build-property compiler.cpp.extra_flags="-Wno-deprecated-declarations"

# --- Build-Verzeichnisse ---
BUILD_DIR = build

.PHONY: all compile upload upload-ota clean

all: compile

compile: patch-config
	@$(MAKE) --no-print-directory $(BUILD_DIR)/$(TARGET).ino.bin

# nur neu bauen wenn Sketch oder Konfiguration neuer sind als die Firmware

$(BUILD_DIR)/$(TARGET).ino.bin: $(TARGET).ino config.h Makefile
	@echo "Kompiliere $(TARGET) .."
	mkdir -p $(BUILD_DIR)
	$(ARDUINO_CLI) compile --fqbn $(BOARD_TAG) $(CXXFLAGS) --build-path $(BUILD_DIR) .

patch-config:
	@cat config-tmpl.h | \
	sed s:"<MQTT_SERVER>":"\"$(MQTT_HOST)\"":g | \
	sed s:"<WIFI_SSID>":"\"$(WIFI_SSID)\"":g   | \
	sed s:"<WIFI_PWD>":"\"$(WIFI_PWD)\"":g   | \
	sed s:"<OTA_PWD>":"\"$(KENWOOD_OTA_PWD)\"":g > config.h.new
	@if cmp -s config.h.new config.h; then rm config.h.new; else echo "Erzeuge config.h .."; mv config.h.new config.h; fi

upload:
	@echo "Flashing $(TARGET) auf $(MONITOR_PORT) .."
	$(ARDUINO_CLI) upload -p $(MONITOR_PORT) --fqbn $(BOARD_TAG) --input-dir $(BUILD_DIR)

upload-ota:
	@test -n "$(KENWOOD_OTA_HOST)" || (echo "KENWOOD_OTA_HOST in Make.user setzen (IP des ESP32)"; exit 1)
	@echo "Flashing $(TARGET) per OTA auf $(KENWOOD_OTA_HOST) .."
	python3 $(ESPOTA) -i $(KENWOOD_OTA_HOST) -p 3232 $(if $(KENWOOD_OTA_PWD),--auth=$(KENWOOD_OTA_PWD)) -f $(BUILD_DIR)/$(TARGET).ino.bin

clean:
	@echo "Bereinige Build-Verzeichnis .."
	rm -f *.o *~
	rm -rf $(BUILD_DIR) config.h config.h.new

install-deps:
	@echo "Aktualisiere Board-Index und installiere ESP32-Core .."
	$(ARDUINO_CLI) core update-index
	$(ARDUINO_CLI) core install esp32:esp32
	@echo "Installiere benötigte Bibliotheken .."
	$(ARDUINO_CLI) lib install PubSubClient
	$(ARDUINO_CLI) lib install ArduinoJson
	@echo "Einrichtung abgeschlossen!"
