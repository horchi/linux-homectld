#!/bin/bash
# Enable I2C on 40-pin header pins 3 (SDA) / 5 (SCL)
# Unterstützte Boards:
#   ODROID N2+  — DTB-Patch: i2c@1d000 (GPIOX_17/18) aktivieren
#   ODROID M1   — Armbian-Overlay aktivieren (i2c0 in armbianEnv.txt)
#
# Benötigt: device-tree-compiler (apt install device-tree-compiler)
# Ausführen: sudo bash armbian-enable-header-i2c.sh

set -euo pipefail

[[ $EUID -eq 0 ]] || { echo "FEHLER: als root ausführen (sudo bash $0)"; exit 1; }

board=$(cat /proc/device-tree/model 2>/dev/null | tr -d '\0')
echo "Board: $board"
echo ""

# ── ODROID N2+ ──────────────────────────────────────────────────────────────
# Header-Pins 3/5 = GPIOX_17/18, I2C-Controller i2c@1d000 (i2c2-Funktion)
# DTB: /boot/dtb/amlogic/meson-g12b-odroid-n2-plus.dtb (via /boot/dtb Symlink)
fix_n2plus() {
    local NODE="/soc/bus@ffd00000/i2c@1d000"
    local DTB="/boot/dtb/amlogic/meson-g12b-odroid-n2-plus.dtb"

    [[ -f "$DTB" ]] || { echo "FEHLER: DTB nicht gefunden: $DTB"; exit 1; }
    echo "DTB: $DTB (→ $(readlink -f "$DTB"))"

    for cmd in dtc fdtput; do
        command -v "$cmd" >/dev/null 2>&1 || { echo "FEHLER: '$cmd' fehlt — apt install device-tree-compiler"; exit 1; }
    done

    local dts SDA_HEX SCK_HEX SDA_DEC SCK_DEC BAK
    dts=$(dtc -I dtb -O dts "$DTB" 2>/dev/null)

    SDA_HEX=$(echo "$dts" | grep -B3 'groups = "i2c2_sda_x"' | grep -oP 'phandle = <\K0x[0-9a-f]+' || true)
    SCK_HEX=$(echo "$dts" | grep -B3 'groups = "i2c2_sck_x"' | grep -oP 'phandle = <\K0x[0-9a-f]+' || true)

    [[ -n "$SDA_HEX" ]] || { echo "FEHLER: phandle für i2c2_sda_x nicht gefunden"; exit 1; }
    [[ -n "$SCK_HEX" ]] || { echo "FEHLER: phandle für i2c2_sck_x nicht gefunden"; exit 1; }

    SDA_DEC=$(( SDA_HEX ))
    SCK_DEC=$(( SCK_HEX ))

    echo "i2c2_sda_x phandle: $SDA_HEX  ($SDA_DEC)"
    echo "i2c2_sck_x phandle: $SCK_HEX  ($SCK_DEC)"

    local cur_status cur_pinctrl
    cur_status=$(echo "$dts" | awk '/i2c@1d000 \{/,/^\s*\};/' | grep "status" | head -1 | tr -d ' \t;"' || true)
    cur_pinctrl=$(echo "$dts" | awk '/i2c@1d000 \{/,/^\s*\};/' | grep "pinctrl-0" | head -1 || true)

    echo "Aktuell status:    ${cur_status:-nicht gesetzt}"
    echo "Aktuell pinctrl-0: ${cur_pinctrl:-nicht gesetzt}"

    if echo "$cur_status" | grep -q "okay" && echo "$cur_pinctrl" | grep -q "$SDA_HEX"; then
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

    fdtput -t s "$DTB" "$NODE" status "okay"
    fdtput -t s "$DTB" "$NODE" pinctrl-names "default"
    fdtput -t u "$DTB" "$NODE" pinctrl-0 $SDA_DEC $SCK_DEC
    echo "DTB gepacht."

    echo ""
    echo "Verifizierung:"
    dtc -I dtb -O dts "$DTB" 2>/dev/null | awk '/i2c@1d000 \{/,/^\s*\};/' | head -10
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
echo "Nach dem Reboot: i2cdetect -l  (Busnummer ermitteln), dann i2cdetect -y <N>  (DHT20 bei 0x38 erwartet)"
echo ""
read -rp "Jetzt rebooten? [j/N] " ans
[[ "$ans" =~ ^[jJyY]$ ]] && reboot || echo "Bitte manuell rebooten."
