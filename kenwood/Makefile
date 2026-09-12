
include ../Make.config

TARGET = kenwood
ARDUINO_CLI = /usr/local/bin/arduino-cli

# Board-Identifikator für den ESP32
BOARD_TAG = esp32:esp32:esp32

# Serieller Port für den Upload
MONITOR_PORT = /dev/ttyACM0

CXXFLAGS = --build-property compiler.cpp.extra_flags="-Wno-deprecated-declarations"

# --- Build-Verzeichnisse ---
BUILD_DIR = build

.PHONY: all compile upload clean

all: compile

compile: patch-config
	@echo "Kompiliere $(TARGET) .."
	mkdir -p $(BUILD_DIR)
	$(ARDUINO_CLI) compile --fqbn $(BOARD_TAG) $(CXXFLAGS) --build-path $(BUILD_DIR) .

patch-config:
	@echo "Erzeuge config.h .."
	cat config-tmpl.h | \
	sed s:"<MQTT_SERVER>":"\"$(MQTT_HOST)\"":g | \
	sed s:"<WIFI_SSID>":"\"$(WIFI_SSID)\"":g   | \
	sed s:"<WIFI_PWD>":"\"$(WIFI_PWD)\"":g > config.h

upload:
	@echo "Flashing $(TARGET) auf $(MONITOR_PORT) .."
	$(ARDUINO_CLI) upload -p $(MONITOR_PORT) --fqbn $(BOARD_TAG) --input-dir $(BUILD_DIR)

clean:
	@echo "Bereinige Build-Verzeichnis .."
	rm -f *.o *~
	rm -rf $(BUILD_DIR) config.h

install-deps:
	@echo "Aktualisiere Board-Index und installiere ESP32-Core .."
	$(ARDUINO_CLI) core update-index
	$(ARDUINO_CLI) core install esp32:esp32
	@echo "Installiere benötigte Bibliotheken .."
	$(ARDUINO_CLI) lib install PubSubClient
	$(ARDUINO_CLI) lib install ArduinoJson
	@echo "Einrichtung abgeschlossen!"
