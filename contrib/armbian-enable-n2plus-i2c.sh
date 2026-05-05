#!/bin/bash
# Enable I2C_AO on ODROID N2+ 40-pin header pins 3 (SDA) / 5 (SCL)
#
# Hintergrund: Das Armbian-DTB hat für den i2c@5000 (I2C_AO) Node einen
# falschen zweiten pinctrl-0 Phandle. Dieses Skript ermittelt die korrekten
# Phandle-Werte aus dem DTB und patcht ihn direkt.
#
# Benötigt: device-tree-compiler (apt install device-tree-compiler)
# Ausführen: sudo bash enable-n2plus-i2c.sh

set -euo pipefail

DTB="/boot/dtb/amlogic/meson-g12b-odroid-n2-plus.dtb"
NODE="/soc/bus@ff800000/i2c@5000"

# Checks
[[ $EUID -eq 0 ]] || { echo "FEHLER: als root ausführen (sudo bash $0)"; exit 1; }
[[ -f "$DTB" ]]   || { echo "FEHLER: $DTB nicht gefunden"; exit 1; }

for cmd in dtc fdtput; do
    command -v "$cmd" >/dev/null 2>&1 || {
        echo "FEHLER: '$cmd' fehlt — bitte installieren:"
        echo "  apt install device-tree-compiler"
        exit 1
    }
done

# Phandles aus DTB ermitteln
dts=$(dtc -I dtb -O dts "$DTB" 2>/dev/null)

SDA_HEX=$(echo "$dts" | grep -A5 "i2c-ao-sda {" | grep -oP 'phandle = <\K0x[0-9a-f]+' | head -1)
SCK_HEX=$(echo "$dts" | grep -A5 "i2c-ao-sck {" | grep -oP 'phandle = <\K0x[0-9a-f]+' | head -1)

[[ -n "$SDA_HEX" ]] || { echo "FEHLER: phandle für i2c-ao-sda nicht gefunden"; exit 1; }
[[ -n "$SCK_HEX" ]] || { echo "FEHLER: phandle für i2c-ao-sck nicht gefunden"; exit 1; }

SDA_DEC=$(( SDA_HEX ))
SCK_DEC=$(( SCK_HEX ))

echo "i2c_ao_sda phandle: $SDA_HEX  ($SDA_DEC)"
echo "i2c_ao_sck phandle: $SCK_HEX  ($SCK_DEC)"

# Bereits korrekt?
CURRENT=$(echo "$dts" | awk '/i2c@5000 \{/,/^\s*\};/' | grep "pinctrl-0" | head -1 || true)
echo "Aktuell:  $CURRENT"
echo "Soll:     pinctrl-0 = <$SDA_HEX $SCK_HEX>;"

if echo "$CURRENT" | grep -q "$SDA_HEX" && echo "$CURRENT" | grep -q "$SCK_HEX"; then
    echo "Bereits korrekt — kein Patch nötig."
    exit 0
fi

# Backup (nur beim ersten Mal)
BAK="${DTB}.orig"
if [[ ! -f "$BAK" ]]; then
    cp "$DTB" "$BAK"
    echo "Backup angelegt: $BAK"
else
    echo "Backup existiert bereits: $BAK"
fi

# Patch
fdtput -t u "$DTB" "$NODE" pinctrl-0 $SDA_DEC $SCK_DEC
echo "DTB gepacht."

# Verifizierung
echo ""
echo "Verifizierung:"
dtc -I dtb -O dts "$DTB" 2>/dev/null | grep -A10 "i2c@5000 {" | head -12

echo ""
echo "Nach dem Reboot: i2cdetect -y 0  (DHT20 bei 0x38 erwartet)"
echo ""
read -rp "Jetzt rebooten? [j/N] " ans
[[ "$ans" =~ ^[jJyY]$ ]] && reboot || echo "Bitte manuell rebooten."
