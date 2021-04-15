#!/bin/bash

DEVICE="/dev/ttyUSB0"
HEX="/usr/share/poold/nano-atmega328-ioctrl.hex"

if [[ -n "$1" ]]; then
   DEVICE="$1"
fi

COMMAND="avrdude -q -V -p atmega328p -D -c arduino -b 57600 -P ${DEVICE} -U flash:w:${HEX}:i"

echo "Calling: ${COMMAND}"
echo ""

${COMMAND}
