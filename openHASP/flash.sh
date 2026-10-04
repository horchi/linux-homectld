#!/bin/bash
# Flash openHASP (CrowPanel Advance 5.0 build) onto the ESP32-S3.
# Usage: ./flash.sh [/dev/ttyUSB0]

set -e

PORT=${1:-/dev/ttyUSB0}
ESPTOOL=${ESPTOOL:-esptool}   # e.g. ESPTOOL=~/.local/pipx/venvs/esptool/bin/esptool
BIN=$(ls "$(dirname "$0")"/firmware/elecrow-advance-s3-8048c050_full_16MB_*.bin | tail -1)

echo "Flashing $BIN to $PORT"

"$ESPTOOL" -p "$PORT" --chip esp32s3 erase-flash
"$ESPTOOL" -p "$PORT" --chip esp32s3 write-flash 0x0 "$BIN"

echo "Done. Monitor with: picocom -b 115200 --omap crcrlf $PORT"
