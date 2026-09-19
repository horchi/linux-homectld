
include ../Make.config

TARGET = alpicool
ARDUINO_CLI = /usr/local/bin/arduino-cli

# Board-Identifikator für den ESP32
BOARD_TAG = esp32:esp32:esp32

# Serieller Port für den Upload (ALPICOOL_PORT aus Make.user) und
# IP/Hostname des ESP32 für OTA (ALPICOOL_OTA_HOST, ALPICOOL_OTA_PWD aus Make.user)
ALPICOOL_PORT ?= /dev/ttyACM0
MONITOR_PORT = $(ALPICOOL_PORT)
ESPOTA = $(shell ls -d $(HOME)/.arduino15/packages/esp32/hardware/esp32/*/tools/espota.py | tail -1)

CXXFLAGS = --build-property compiler.cpp.extra_flags="-Wno-deprecated-declarations"

# Partitionstabelle 'min_spiffs': zwei App-Slots mit je 1.9 MB statt 1.25 MB, sonst passt der
# Sketch (NimBLE + OTA, ~1.28 MB) nicht mehr in die OTA Partition. Nach dieser Aenderung
# einmal per USB flashen, die Partitionstabelle laesst sich nicht per OTA aendern.
CXXFLAGS += --build-property build.partitions=min_spiffs --build-property upload.maximum_size=1966080

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
	sed s/"<ALPI_MAC>"/"\"$(ALPI_MAC)\""/g   | \
	sed s:"<WIFI_SSID>":"\"$(WIFI_SSID)\"":g   | \
	sed s:"<WIFI_PWD>":"\"$(WIFI_PWD)\"":g   | \
	sed s:"<OTA_PWD>":"\"$(ALPICOOL_OTA_PWD)\"":g > config.h.new
	@if cmp -s config.h.new config.h; then rm config.h.new; else echo "Erzeuge config.h .."; mv config.h.new config.h; fi

upload:
	@echo "Flashing $(TARGET) auf $(MONITOR_PORT) .."
	$(ARDUINO_CLI) upload -p $(MONITOR_PORT) --fqbn $(BOARD_TAG) --input-dir $(BUILD_DIR)

upload-ota:
	@test -n "$(ALPICOOL_OTA_HOST)" || (echo "ALPICOOL_OTA_HOST in Make.user setzen (IP des ESP32)"; exit 1)
	@echo "Flashing $(TARGET) per OTA auf $(ALPICOOL_OTA_HOST) .."
	python3 $(ESPOTA) -i $(ALPICOOL_OTA_HOST) -p 3232 $(if $(ALPICOOL_OTA_PWD),--auth=$(ALPICOOL_OTA_PWD)) -f $(BUILD_DIR)/$(TARGET).ino.bin

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
	$(ARDUINO_CLI) lib install INA226_WE
	@echo "Einrichtung abgeschlossen!"


old_install:
	@echo alpicool service
	install --mode=755 -D alpicool.py $(BINDEST)/
	install --mode=755 -D alpicool.sh $(CONFDEST)/scripts.d/
	cat alpicool.service | sed s:"<BINDEST>":"$(_BINDEST)":g | sed s:"<AFTER>":"$(INIT_AFTER)":g | install --mode=644 -C -D /dev/stdin $(SYSTEMDDEST)/alpicool.service
	chmod a+r $(SYSTEMDDEST)/alpicool.service
	if ! test -f $(DESTDIR)/etc/default/alpicool2mqtt; then \
		cp alpicool2mqtt $(DESTDIR)/etc/default/alpicool2mqtt; \
		echo "!! Adjust settings in $(DESTDIR)/etc/default/alpicool2mqtt !!"; \
	fi
   ifeq ($(DESTDIR),)
	   systemctl enable alpicool.service
	   systemctl daemon-reload
   endif
