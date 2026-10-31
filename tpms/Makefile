include ../Make.config

TARGET = tpms
ARDUINO_CLI ?= $(shell command -v arduino-cli || echo /usr/local/bin/arduino-cli)

# Board-Identifikator für den ESP32
BOARD_TAG = esp32:esp32:esp32

# Serieller Port für den Upload (TPMS_PORT aus Make.user) und
# IP/Hostname des ESP32 für OTA (TPMS_OTA_HOST, TPMS_OTA_PWD aus Make.user)
TPMS_PORT ?= /dev/ttyACM0
MONITOR_PORT = $(TPMS_PORT)
ESPOTA = $(shell ls -d $(HOME)/.arduino15/packages/esp32/hardware/esp32/*/tools/espota.py | tail -1)

CXXFLAGS = --build-property compiler.cpp.extra_flags="-Wno-deprecated-declarations"

# Partitionstabelle 'min_spiffs': zwei App-Slots mit je 1.9 MB statt 1.25 MB, sonst passt der
# Sketch (NimBLE + WLAN + OTA) nicht in die OTA Partition. Nach dieser Aenderung
# einmal per USB flashen, die Partitionstabelle laesst sich nicht per OTA aendern.
CXXFLAGS += --build-property build.partitions=min_spiffs --build-property upload.maximum_size=1966080

# --- Build-Verzeichnisse ---
BUILD_DIR = build

.PHONY: all compile upload upload-ota monitor clean install-deps

all: compile

compile: patch-config
	@$(MAKE) --no-print-directory $(BUILD_DIR)/$(TARGET).ino.bin

# nur neu bauen wenn Sketch oder Konfiguration neuer sind als die Firmware

$(BUILD_DIR)/$(TARGET).ino.bin: $(TARGET).ino config.h Makefile
	@echo "Kompiliere $(TARGET) .."
	mkdir -p $(BUILD_DIR)
	$(ARDUINO_CLI) compile --fqbn $(BOARD_TAG) $(CXXFLAGS) --build-path $(BUILD_DIR) .

# die Werte gehen unverändert ($(value ...), Umgebung, perl) in die config.h, ein '$' im
# WLAN Passwort wird sonst von make / der Shell als Variable ersetzt

patch-config: export CFG_MQTT_HOST = $(value MQTT_HOST)
patch-config: export CFG_WIFI_SSID = $(value WIFI_SSID)
patch-config: export CFG_WIFI_PWD = $(value WIFI_PWD)
patch-config: export CFG_OTA_PWD = $(value TPMS_OTA_PWD)

patch-config:
	@perl -pe 's/<MQTT_SERVER>/"$$ENV{CFG_MQTT_HOST}"/g; s/<WIFI_SSID>/"$$ENV{CFG_WIFI_SSID}"/g; \
	           s/<WIFI_PWD>/"$$ENV{CFG_WIFI_PWD}"/g; s/<OTA_PWD>/"$$ENV{CFG_OTA_PWD}"/g' config-tmpl.h > config.h.new
	@if cmp -s config.h.new config.h; then rm config.h.new; else echo "Erzeuge config.h .."; mv config.h.new config.h; fi

upload:
	@echo "Flashing $(TARGET) auf $(MONITOR_PORT) .."
	$(ARDUINO_CLI) upload -p $(MONITOR_PORT) --fqbn $(BOARD_TAG) --input-dir $(BUILD_DIR)

upload-ota:
	@test -n "$(TPMS_OTA_HOST)" || (echo "TPMS_OTA_HOST in Make.user setzen (IP des ESP32)"; exit 1)
	@echo "Flashing $(TARGET) per OTA auf $(TPMS_OTA_HOST) .."
	python3 $(ESPOTA) -i $(TPMS_OTA_HOST) -p 3232 $(if $(TPMS_OTA_PWD),--auth=$(TPMS_OTA_PWD)) -f $(BUILD_DIR)/$(TARGET).ino.bin

monitor:
	$(ARDUINO_CLI) monitor -p $(MONITOR_PORT) --config baudrate=115200

clean:
	@echo "Bereinige Build-Verzeichnis .."
	rm -f *.o *~
	rm -rf $(BUILD_DIR) config.h config.h.new

install-deps:
	@echo "Aktualisiere Board-Index und installiere ESP32-Core .."
	$(ARDUINO_CLI) core update-index
	$(ARDUINO_CLI) core install esp32:esp32
	@echo "Installiere benötigte Bibliotheken .."
	$(ARDUINO_CLI) lib install NimBLE-Arduino
	$(ARDUINO_CLI) lib install PubSubClient
	$(ARDUINO_CLI) lib install ArduinoJson
	@echo "Einrichtung abgeschlossen!"
