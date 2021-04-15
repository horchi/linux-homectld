
TARGET        = ioctrl
BOARD_TAG     = nano
BOARD_SUB     = atmega328
MCU           = atmega328p
ARDUINO_DIR   = /home/wendel/arduino-1.8.11
MONITOR_PORT  = /dev/ttyUSB0
AVRDUDE_ARD_BAUDRATE = 57600
CXXFLAGS     += -Wno-deprecated-declarations

include /usr/share/arduino/Arduino.mk

BIN  = $(TARGET_HEX)
DEST = ../$(OBJDIR).$(TARGET).hex

install:
	cp $(BIN) $(DEST)
