#!/bin/bash
# Enable I2C on 40-pin header pins 3 (SDA) / 5 (SCL)
# Unterstützte Boards:
#   ODROID N2+  — DTB-Patch (falscher pinctrl-0 Phandle in i2c_AO)
#   ODROID M1   — Armbian-Overlay aktivieren (i2c0 in armbianEnv.txt)
#
# Benötigt: device-tree-compiler (apt install device-tree-compiler)
# Ausführen: sudo bash armbian-enable-n2plus-i2c.sh

set -euo pipefail

[[ $EUID -eq 0 ]] || { echo "FEHLER: als root ausführen (sudo bash $0)"; exit 1; }

board=$(cat /proc/device-tree/model 2>/dev/null | tr -d '\0')
echo "Board: $board"
echo ""

# ── ODROID N2+ ──────────────────────────────────────────────────────────────
fix_n2plus() {
    local DTB="/boot/dtb/amlogic/meson-g12b-odroid-n2-plus.dtb"
    local NODE="/soc/bus@ff800000/i2c@5000"

    [[ -f "$DTB" ]] || { echo "FEHLER: $DTB nicht gefunden"; exit 1; }

    for cmd in dtc fdtput; do
        command -v "$cmd" >/dev/null 2>&1 || { echo "FEHLER: '$cmd' fehlt — apt install device-tree-compiler"; exit 1; }
    done

    local dts SDA_HEX SCK_HEX SDA_DEC SCK_DEC CURRENT BAK
    dts=$(dtc -I dtb -O dts "$DTB" 2>/dev/null)

    SDA_HEX=$(echo "$dts" | grep -A5 "i2c-ao-sda {" | grep -oP 'phandle = <\K0x[0-9a-f]+' | head -1)
    SCK_HEX=$(echo "$dts" | grep -A5 "i2c-ao-sck {" | grep -oP 'phandle = <\K0x[0-9a-f]+' | head -1)

    [[ -n "$SDA_HEX" ]] || { echo "FEHLER: phandle für i2c-ao-sda nicht gefunden"; exit 1; }
    [[ -n "$SCK_HEX" ]] || { echo "FEHLER: phandle für i2c-ao-sck nicht gefunden"; exit 1; }

    SDA_DEC=$(( SDA_HEX ))
    SCK_DEC=$(( SCK_HEX ))

    echo "i2c_ao_sda phandle: $SDA_HEX  ($SDA_DEC)"
    echo "i2c_ao_sck phandle: $SCK_HEX  ($SCK_DEC)"

    CURRENT=$(echo "$dts" | awk '/i2c@5000 \{/,/^\s*\};/' | grep "pinctrl-0" | head -1 || true)
    echo "Aktuell:  $CURRENT"
    echo "Soll:     pinctrl-0 = <$SDA_HEX $SCK_HEX>;"

    if echo "$CURRENT" | grep -q "$SDA_HEX" && echo "$CURRENT" | grep -q "$SCK_HEX"; then
        echo "Bereits korrekt — kein Patch nötig."
        exit 0
    fi

    BAK="${DTB}.orig"
    if [[ ! -f "$BAK" ]]; then
        cp "$DTB" "$BAK"
        echo "Backup angelegt: $BAK"
    else
        echo "Backup existiert bereits: $BAK"
    fi

    fdtput -t u "$DTB" "$NODE" pinctrl-0 $SDA_DEC $SCK_DEC
    echo "DTB gepacht."

    echo ""
    echo "Verifizierung:"
    dtc -I dtb -O dts "$DTB" 2>/dev/null | grep -A10 "i2c@5000 {" | head -12
}

# ── ODROID M1 ────────────────────────────────────────────────────────────────
fix_m1() {
    local ENV="/boot/armbianEnv.txt"
    local OVERLAY="i2c0"

    [[ -f "$ENV" ]] || { echo "FEHLER: $ENV nicht gefunden"; exit 1; }

    if grep -qE "(^|\s)${OVERLAY}(\s|$)" "$ENV"; then
        echo "Overlay '${OVERLAY}' bereits in $ENV — kein Patch nötig."
        exit 0
    fi

    if grep -q "^overlays=" "$ENV"; then
        sed -i "s/^overlays=.*/& ${OVERLAY}/" "$ENV"
    else
        echo "overlays=${OVERLAY}" >> "$ENV"
    fi

    echo "Overlay '${OVERLAY}' zu $ENV hinzugefügt."
    echo ""
    grep "overlays=" "$ENV"
}

# ── Board-Erkennung ──────────────────────────────────────────────────────────
if echo "$board" | grep -qiE "N2"; then
    echo "→ N2+ erkannt: DTB-Patch"
    echo ""
    fix_n2plus
elif echo "$board" | grep -qiE "M1"; then
    echo "→ M1 erkannt: Armbian-Overlay aktivieren"
    echo ""
    fix_m1
else
    echo "FEHLER: Board nicht unterstützt ('$board')"
    echo "Unterstützt: ODROID N2+, ODROID M1"
    exit 1
fi

echo ""
echo "Nach dem Reboot: i2cdetect -y 0  (DHT20 bei 0x38 erwartet)"
echo ""
read -rp "Jetzt rebooten? [j/N] " ans
[[ "$ans" =~ ^[jJyY]$ ]] && reboot || echo "Bitte manuell rebooten."
