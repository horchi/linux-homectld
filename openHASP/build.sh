#!/bin/bash
# Build the openHASP firmware for the CrowPanel Advance 5.0 from a fresh clone of the homectld repo:
#   1. fetch openHASP (upstream.conf) into src/ if missing
#   2. apply our changes: patches/openhasp.patch + overlay/ (only on a fresh fetch or with --reset)
#   3. create the PlatformIO venv if missing
#   4. pio run, copy the images to firmware/
# Usage: ./build.sh [--reset]      --reset: discard edits in src/, re-apply patch and overlay
# Afterwards: ./ota.sh <ip> uploads the new firmware over WLAN.

set -e
cd "$(dirname "$0")"
. ./upstream.conf

apply_changes()
{
    echo "applying patches/openhasp.patch and overlay/"
    git -C src checkout -q -- .
    git -C src apply --whitespace=nowarn ../patches/openhasp.patch
    (cd overlay && find . -type f) | while read -r f; do
        mkdir -p "src/$(dirname "$f")"
        cp "overlay/$f" "src/$f"
    done
}

if [ ! -d src/.git ]; then
    echo "fetching openHASP $OPENHASP_COMMIT"
    mkdir -p src
    git -C src init -q
    git -C src remote add origin "$OPENHASP_URL"
    git -C src fetch -q --depth 1 origin "$OPENHASP_COMMIT"
    git -C src checkout -q FETCH_HEAD
    echo "fetching submodules (lib/freetype)"
    git -C src submodule update -q --init --depth 1 || git -C src submodule update -q --init
    apply_changes
elif [ "$1" = "--reset" ]; then
    apply_changes
fi

if [ ! -x venv/bin/pio ]; then
    echo "creating PlatformIO venv"
    python3 -m venv venv
    venv/bin/pip install -q platformio
fi

echo "building $PIO_ENV"
(cd src && nice ../venv/bin/pio run -e "$PIO_ENV" -j 2)

mkdir -p firmware
cp -v src/build_output/firmware/*.bin src/build_output/firmware/*.json firmware/
echo; echo "done - upload with: ./ota.sh <ip>"
