#!/bin/bash
# Firmware update over WLAN via the web updater (/update) of openHASP - no USB needed.
# Usage: ./ota.sh <ip> [firmware.bin]
#  default: the newest *_ota_*.bin from firmware/
# Set HASP_USER/HASP_PASS if a web password is configured.

set -e

IP="${1:-192.168.200.123}"
BIN="${2:-$(ls -t "$(dirname "$0")"/firmware/*_ota_*.bin | head -1)}"
AUTH=${HASP_PASS:+-u ${HASP_USER:-admin}:$HASP_PASS}

echo "uploading $(basename "$BIN") ($(stat -c %s "$BIN") bytes) -> http://$IP/update"
curl -f $AUTH -F "cmd=0" -F "filename=@$BIN" "http://$IP/update" > /dev/null

echo; echo "done, the panel reboots with the new firmware (about 20 s); version: http://$IP/"
