#!/bin/bash
# Prüft ob I2C auf den 40-pin Header-Pins 3 (SDA) / 5 (SCL) verfügbar ist
# Generisch für Armbian auf beliebigen Boards

RC=0
HEADER_I2C_NODE=""

echo "=== I2C Header-Check ==="
echo ""

# Board & Kernel
board=$(cat /proc/device-tree/model 2>/dev/null | tr -d '\0')
echo "Board:  $board"
echo "Kernel: $(uname -r)"
echo ""

# 1. i2c-tools
if ! command -v i2cdetect >/dev/null 2>&1; then
    echo "[FEHLT]  i2c-tools -> apt install i2c-tools"
    RC=1
else
    echo "[OK]     i2cdetect gefunden"
fi

# 2. dtc
if ! command -v dtc >/dev/null 2>&1; then
    echo "[FEHLT]  dtc -> apt install device-tree-compiler"
    RC=1
else
    echo "[OK]     dtc gefunden"
fi
echo ""

# 3. Verfügbare I2C-Busse
echo "--- Verfügbare I2C-Busse ---"
buses=$(i2cdetect -l 2>/dev/null || true)
if [[ -z "$buses" ]]; then
    echo "[FEHLER] Keine I2C-Busse gefunden"
    RC=1
else
    echo "$buses"
fi
echo ""

# 4. Bus → DT-Node Zuordnung
echo "--- Bus → Device Tree Node ---"
for link in /sys/bus/i2c/devices/i2c-*/of_node; do
    [[ -e "$link" ]] || continue
    bus=$(echo "$link" | grep -oP 'i2c-\d+')
    target=$(readlink "$link" 2>/dev/null | sed 's|.*/base/||')
    printf "  %-8s -> %s\n" "$bus" "$target"
done
echo ""

# 5. DTB-Datei ermitteln (Armbian-generisch via armbianEnv.txt)
DTB=""
if [[ -f /boot/armbianEnv.txt ]]; then
    fdtfile=$(grep "^fdtfile=" /boot/armbianEnv.txt 2>/dev/null | cut -d= -f2 | tr -d ' \r')
    if [[ -n "$fdtfile" ]]; then
        # Armbian legt DTBs unter /boot/dtb/ ab
        for base in /boot/dtb /boot/dtb-$(uname -r); do
            [[ -f "$base/$fdtfile" ]] && DTB="$base/$fdtfile" && break
        done
    fi
fi
# Fallback: DTB aus laufendem Kernel-FDT ableiten
if [[ -z "$DTB" && -f /proc/device-tree/model ]]; then
    DTB=$(find /boot -name "*.dtb" 2>/dev/null \
        | xargs -I{} sh -c 'strings {} 2>/dev/null | grep -qF "'"$board"'" && echo {}' \
        | head -1)
fi

echo "--- DTB-Datei ---"
if [[ -n "$DTB" && -f "$DTB" ]]; then
    echo "[OK]     $DTB"
else
    echo "[WARN]   Nicht gefunden — nutze laufendes Device Tree (/proc/device-tree)"
fi
echo ""

# 6. I2C-Nodes: live DT für Betriebszustand, DTB-Datei für Soll-Zustand
echo "--- I2C Nodes (laufendes System) ---"
if command -v dtc >/dev/null 2>&1; then
    live_dts=$(dtc -I fs -O dts /proc/device-tree 2>/dev/null)
    echo "$live_dts" | grep -oP 'i2c@\K[0-9a-f]+(?= \{)' | sort -u | while read -r addr; do
        status=$(echo "$live_dts" | awk "/i2c@${addr} \{/,/^\s*\};/" | grep "status" | head -1 | tr -d '\t ;')
        printf "  i2c@%-10s %s\n" "$addr" "$status"
    done
fi
echo ""

# 7. Board-spezifische Tiefenprüfung
echo "--- Board-spezifische Prüfung ---"
if command -v dtc >/dev/null 2>&1 && [[ -n "$DTB" && -f "$DTB" ]]; then
    dts=$(dtc -I dtb -O dts "$DTB" 2>/dev/null)

    # ODROID N2/N2+: i2c@1d000 (GPIOX_17=SDA, GPIOX_18=SCL) auf Pins 3/5
    # DTB: /boot/dtb/amlogic/meson-g12b-odroid-n2-plus.dtb
    if echo "$board" | grep -qiE "N2"; then
        n2_dtb="/boot/dtb/amlogic/meson-g12b-odroid-n2-plus.dtb"
        [[ -f "$n2_dtb" ]] && dts=$(dtc -I dtb -O dts "$n2_dtb" 2>/dev/null)
        status=$(echo "$dts" | awk '/i2c@1d000 \{/,/^\s*\};/' | grep "status" | head -1 | tr -d ' \t;')
        pinctrl=$(echo "$dts" | awk '/i2c@1d000 \{/,/^\s*\};/' | grep "pinctrl-0" | head -1 | tr -d ' \t;')
        SDA_HEX=$(echo "$dts" | grep -B3 'groups = "i2c2_sda_x"' | grep -oP 'phandle = <\K0x[0-9a-f]+' || true)
        SCK_HEX=$(echo "$dts" | grep -B3 'groups = "i2c2_sck_x"' | grep -oP 'phandle = <\K0x[0-9a-f]+' || true)

        HEADER_I2C_NODE="i2c@1d000"
        echo "  [N2+] i2c@1d000 (GPIOX_17/18, Pins 3/5): $status"
        if [[ -n "$SDA_HEX" && -n "$SCK_HEX" ]] && echo "$pinctrl" | grep -q "$SDA_HEX" && echo "$pinctrl" | grep -q "$SCK_HEX"; then
            echo "  [OK]  pinctrl-0 korrekt ($SDA_HEX $SCK_HEX)"
        else
            echo "  [FEHLER] i2c@1d000 nicht aktiviert oder falsches pinctrl"
            echo "           Fix: sudo bash contrib/armbian-enable-header-i2c.sh"
            RC=1
        fi

    # ODROID M1 (RK3568): Header-I2C auf Pins 3/5, aktiviert via Overlay
    elif echo "$board" | grep -qiE "M1"; then
        # Overlay-Name dynamisch aus overlay_prefix ableiten
        ovl_prefix=$(grep "^overlay_prefix=" /boot/armbianEnv.txt 2>/dev/null | cut -d= -f2 | tr -d ' \r')
        ovl_dir=$(ls -d /boot/dtb/rockchip/overlay /boot/overlays 2>/dev/null | head -1)
        i2c_overlay="${ovl_prefix}-i2c0"
        i2c_overlay_short="i2c0"
        # Ziel-Node dynamisch aus Overlay-Datei + DT-Alias ableiten
        i2c_addr=""
        if [[ -n "$ovl_dir" && -f "$ovl_dir/${i2c_overlay}.dtbo" ]]; then
            echo "  [OK]  Overlay-Datei gefunden: $ovl_dir/${i2c_overlay}.dtbo"
            ovl_alias=$(dtc -I dtb -O dts "$ovl_dir/${i2c_overlay}.dtbo" 2>/dev/null | grep -oP '^\s+\Ki2c\d+(?= =)' | head -1)
            if [[ -n "$ovl_alias" ]]; then
                # __symbols__ enthält Hardware-Node-Pfade (für Overlay-Fixups), NICHT Linux-Busnummern
                # tail -1 bevorzugt __symbols__-Eintrag (kommt nach aliases im DTB)
                alias_path=$(echo "$dts" | grep "${ovl_alias} = \"/" | grep -oP '= "\K[^"]+' | tail -1)
                i2c_addr=$(basename "$alias_path" | grep -oP '[0-9a-f]+$')
                echo "  [INFO] Overlay-Ziel: ${ovl_alias} → i2c@${i2c_addr}"
            fi
        else
            echo "  [WARN] Overlay-Datei ${i2c_overlay}.dtbo nicht gefunden (Armbian-Version prüfen)"
        fi
        # Overlay in armbianEnv.txt prüfen — Armbian erwartet Kurzname ohne overlay_prefix
        if grep -qE "(^|\s)(${i2c_overlay_short}|${i2c_overlay})(\s|$)" /boot/armbianEnv.txt 2>/dev/null; then
            echo "  [OK]  Overlay ${i2c_overlay_short} in /boot/armbianEnv.txt aktiv"
        else
            echo "  [FEHLER] Overlay ${i2c_overlay_short} fehlt — Header-I2C nicht aktiv"
            echo "           Fix: sed -i 's/^overlays=.*/& ${i2c_overlay_short}/' /boot/armbianEnv.txt && reboot"
            RC=1
        fi
        # Live-Status des ermittelten Header-I2C-Node
        [[ -n "$i2c_addr" ]] && HEADER_I2C_NODE="i2c@${i2c_addr}"
        if [[ -n "$i2c_addr" ]]; then
            live_status=$(echo "$live_dts" | awk "/i2c@${i2c_addr} \{/,/^\s*\};/" | grep "status" | head -1 | tr -d ' \t;')
            echo "  [M1]  i2c@${i2c_addr} (Pins 3/5): ${live_status:-nicht gefunden}"
            if echo "$live_status" | grep -q "okay"; then
                echo "  [OK]  Header-I2C aktiv"
            else
                echo "  [FEHLER] i2c@${i2c_addr} disabled — Overlay aktivieren und neu starten"
                RC=1
            fi
        else
            echo "  [WARN] Header-I2C-Node konnte nicht ermittelt werden"
        fi

    else
        echo "  Board nicht spezifisch bekannt — manuelle Prüfung der Bus→Node Zuordnung oben nötig"
    fi
else
    echo "  Übersprungen (dtc oder DTB nicht verfügbar)"
fi
echo ""

# 8. Pinmux (laufendes System)
echo "--- Aktiver Pinmux (I2C-Pins) ---"
pinmux_i2c=$(cat /sys/kernel/debug/pinctrl/*/pinmux-pins 2>/dev/null | grep -i "i2c" || true)
if [[ -n "$pinmux_i2c" ]]; then
    echo "$pinmux_i2c" | sed 's/^/  /'
    echo "  [OK]  I2C-Pins aktiv gemuxed"
else
    echo "  [WARN]  Keine I2C-Pins im Pinmux gefunden"
    RC=1
fi

# 9. Header-Bus-Nummer aus of_node-Symlinks ermitteln
header_bus=""
if [[ -n "$HEADER_I2C_NODE" ]]; then
    for link in /sys/bus/i2c/devices/i2c-*/of_node; do
        [[ -e "$link" ]] || continue
        if readlink "$link" 2>/dev/null | grep -q "${HEADER_I2C_NODE}"; then
            header_bus=$(echo "$link" | grep -oP '(?<=i2c-)\d+')
            break
        fi
    done
fi

echo ""
if [[ -n "$header_bus" ]]; then
    echo "=== Ergebnis: $([ $RC -eq 0 ] && echo 'I2C auf Pins 3/5 verfügbar' || echo 'Probleme gefunden — siehe oben') ==="
    echo "    Scan: i2cdetect -y $header_bus"
else
    echo "=== Ergebnis: $([ $RC -eq 0 ] && echo 'I2C auf Pins 3/5 verfügbar' || echo 'Probleme gefunden — siehe oben') ==="
fi
exit $RC
