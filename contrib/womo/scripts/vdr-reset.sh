#!/bin/bash

# 1. Prozesse hart beenden

killall -9 vdr 2>/dev/null
fuser -k /dev/amvideo /dev/amstream /dev/amvdec /dev/ion 2>/dev/null
sleep 1

# 2. Video-Ebene Reset (Der wichtigste Teil)

if [ -f /sys/class/graphics/fb0/blank ]; then
    echo 1 > /sys/class/graphics/fb0/blank
    sleep 0.5
    echo 0 > /sys/class/graphics/fb0/blank
fi

# 3. Speicher-Management (Wichtig gegen CMA-Fragmentierung)

sync
echo 3 > /proc/sys/vm/drop_caches

# 4. HDMI-Handshake via Display-Mode (Sicherer als output_en)

if [ -f /sys/class/display/mode ]; then
    # Wir lesen den Modus und schreiben ihn direkt wieder
    cat /sys/class/display/mode > /sys/class/display/mode 2>/dev/null
fi

echo "Video-Reset abgeschlossen"
