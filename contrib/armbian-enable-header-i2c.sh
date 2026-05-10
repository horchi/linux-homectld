#!/bin/bash
# Enable I2C on 40-pin header pins 3 (SDA) / 5 (SCL)
# Enable GPIO pull-ups via Device Tree Overlay (ODROID M1)
# Unterstützte Boards:
#   ODROID N2+  — DTB-Patch: i2c@1d000 (GPIOX_17/18) aktivieren
#   ODROID M1   — Armbian-Overlay aktivieren (i2c0 in armbianEnv.txt)
#               — GPIO Pull-Up Overlay kompilieren und installieren
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
        return 0
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
        return 0
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

# ── ODROID M1: GPIO Pull-Up Overlay ─────────────────────────────────────────
fix_m1_gpio_pullups() {
    local ENV="/boot/armbianEnv.txt"
    local OVERLAY="gpio-pullups"

    command -v dtc >/dev/null 2>&1 || { echo "FEHLER: 'dtc' fehlt — apt install device-tree-compiler"; exit 1; }

    [[ -f "$ENV" ]] || { echo "FEHLER: $ENV nicht gefunden"; exit 1; }

    local PREFIX
    PREFIX=$(grep "^overlay_prefix=" "$ENV" | cut -d= -f2 | tr -d '[:space:]')
    [[ -n "$PREFIX" ]] || { echo "FEHLER: overlay_prefix nicht in $ENV gefunden"; exit 1; }

    local OVERLAY_DIR
    OVERLAY_DIR=$(find /boot -type d -name "overlay" -path "*/rockchip/*" 2>/dev/null | head -1)
    [[ -n "$OVERLAY_DIR" ]] || { echo "FEHLER: Overlay-Verzeichnis nicht gefunden"; exit 1; }

    local DTBO="${OVERLAY_DIR}/${PREFIX}-${OVERLAY}.dtbo"
    local TMP_DTS
    TMP_DTS=$(mktemp /tmp/gpio-pullups.XXXXXX.dts)

    cat > "$TMP_DTS" << 'EOF'
/dts-v1/;
/plugin/;

/ {
   compatible = "rockchip,rk3568";

   fragment@0 {
      target = <&pinctrl>;
      __overlay__ {
         homectld_pins {
            gpio3d0_pullup: gpio3d0-pullup {
               rockchip,pins = <3 24 0 &pcfg_pull_up>;
            };
         };
      };
   };

   fragment@1 {
      target = <&gpio3>;
      __overlay__ {
         pinctrl-names = "default";
         pinctrl-0 = <&gpio3d0_pullup>;
      };
   };
};
EOF

    echo "Overlay-Dir: $OVERLAY_DIR"
    echo "DTBO:        $DTBO"

    dtc -@ -I dts -O dtb -o "$DTBO" "$TMP_DTS" 2>/dev/null
    rm -f "$TMP_DTS"
    echo "Overlay kompiliert und installiert: $DTBO"

    if grep -qE "(^|\s)${OVERLAY}(\s|$)" "$ENV"; then
        echo "Overlay '${OVERLAY}' bereits in $ENV — kein Patch nötig."
    else
        if grep -q "^overlays=" "$ENV"; then
            sed -i "s/^overlays=.*/& ${OVERLAY}/" "$ENV"
        else
            echo "overlays=${OVERLAY}" >> "$ENV"
        fi
        echo "Overlay '${OVERLAY}' zu $ENV hinzugefügt."
        echo ""
        grep "overlays=" "$ENV"
    fi
}

# ── ODROID M1: 1-Wire Overlay (phys pin 15 = GPIO3_B6) ──────────────────────
fix_m1_onewire() {
    local ENV="/boot/armbianEnv.txt"
    local OVERLAY="onewire"

    command -v dtc >/dev/null 2>&1 || { echo "FEHLER: 'dtc' fehlt — apt install device-tree-compiler"; exit 1; }

    [[ -f "$ENV" ]] || { echo "FEHLER: $ENV nicht gefunden"; exit 1; }

    local PREFIX
    PREFIX=$(grep "^overlay_prefix=" "$ENV" | cut -d= -f2 | tr -d '[:space:]')
    [[ -n "$PREFIX" ]] || { echo "FEHLER: overlay_prefix nicht in $ENV gefunden"; exit 1; }

    local OVERLAY_DIR
    OVERLAY_DIR=$(find /boot -type d -name "overlay" -path "*/rockchip/*" 2>/dev/null | head -1)
    [[ -n "$OVERLAY_DIR" ]] || { echo "FEHLER: Overlay-Verzeichnis nicht gefunden"; exit 1; }

    local DTBO="${OVERLAY_DIR}/${PREFIX}-${OVERLAY}.dtbo"
    local TMP_DTS
    TMP_DTS=$(mktemp /tmp/onewire.XXXXXX.dts)

    # phys pin 15 = GPIO3_B2: bank=3, port B (1) bit 2 → offset=10
    cat > "$TMP_DTS" << 'EOF'
/dts-v1/;
/plugin/;

/ {
   compatible = "rockchip,rk3568";

   fragment@0 {
      target-path = "/";
      __overlay__ {
         onewire {
            compatible = "w1-gpio";
            gpios = <&gpio3 10 0>;
            pinctrl-names = "default";
            pinctrl-0 = <&w1_gpio_pin>;
            status = "okay";
         };
      };
   };

   fragment@1 {
      target = <&pinctrl>;
      __overlay__ {
         onewire {
            w1_gpio_pin: w1-gpio-pin {
               rockchip,pins = <3 10 0 &pcfg_pull_up>;
            };
         };
      };
   };
};
EOF

    echo "Overlay-Dir: $OVERLAY_DIR"
    echo "DTBO:        $DTBO"

    dtc -@ -I dts -O dtb -o "$DTBO" "$TMP_DTS" 2>/dev/null
    rm -f "$TMP_DTS"
    echo "Overlay kompiliert und installiert: $DTBO"

    if grep -qE "(^|\s)${OVERLAY}(\s|$)" "$ENV"; then
        echo "Overlay '${OVERLAY}' bereits in $ENV — kein Patch nötig."
    else
        if grep -q "^overlays=" "$ENV"; then
            sed -i "s/^overlays=.*/& ${OVERLAY}/" "$ENV"
        else
            echo "overlays=${OVERLAY}" >> "$ENV"
        fi
        echo "Overlay '${OVERLAY}' zu $ENV hinzugefügt."
        echo ""
        grep "overlays=" "$ENV"
    fi

    # w1-gpio via DT geladen — manuelle Moduleinträge entfernen
    if grep -qE "^w1[-_]gpio|^w1[-_]therm" /etc/modules 2>/dev/null; then
        sed -i '/^w1[-_]gpio/d;/^w1[-_]therm/d' /etc/modules
        echo "/etc/modules: w1-gpio / w1-therm Einträge entfernt (werden per DT geladen)"
    fi
}

# ── ODROID N2+: 1-Wire (phys pin 15 = PIN_15 = gpiochip0 offset 72) ─────────
fix_n2plus_onewire() {
    local DTB="/boot/dtb/amlogic/meson-g12b-odroid-n2-plus.dtb"
    local ENV="/boot/armbianEnv.txt"
    local PIN15_OFFSET=72

    [[ -f "$DTB" ]] || { echo "FEHLER: DTB nicht gefunden: $DTB"; exit 1; }

    # meson-w1-gpio overlay is broken on N2+ (no 'gpio' symbol in DTB, wrong offset)
    # → remove it from armbianEnv.txt and patch the DTB directly instead
    if grep -qE "(^|\s)w1-gpio(\s|$)" "$ENV" 2>/dev/null; then
        sed -i 's/\bw1-gpio\b//' "$ENV"
        sed -i '/^overlays=/s/  \+/ /g; /^overlays=/s/= /=/; /^overlays=/s/ $//' "$ENV"
        echo "Overlay 'w1-gpio' aus $ENV entfernt (direkter DTB-Patch stattdessen)"
    fi

    if dtc -I dtb -O dts "$DTB" 2>/dev/null | grep -q "w1-gpio"; then
        echo "1-Wire bereits im DTB — kein Patch nötig."
        return 0
    fi

    # Find GPIO controller phandle (node containing PIN_32 in gpio-line-names)
    local GPIO_PHANDLE_HEX GPIO_PHANDLE_DEC
    GPIO_PHANDLE_HEX=$(dtc -I dtb -O dts "$DTB" 2>/dev/null | awk '
        /gpio-line-names.*PIN_32/ { f=1; d=1 }
        f && /\{/ { d++ }
        f && /\}/ { d--; if (d <= 0) f=0 }
        f && /phandle = </ {
            match($0, /phandle = <(0x[0-9a-f]+)>/, a)
            print a[1]; exit
        }')

    [[ -n "$GPIO_PHANDLE_HEX" ]] || { echo "FEHLER: GPIO-Controller phandle nicht gefunden"; exit 1; }
    GPIO_PHANDLE_DEC=$(( GPIO_PHANDLE_HEX ))
    echo "GPIO-Controller phandle: $GPIO_PHANDLE_HEX ($GPIO_PHANDLE_DEC), Pin-15-Offset: $PIN15_OFFSET"

    local BAK="${DTB}.orig"
    if [[ ! -f "$BAK" ]]; then
        cp "$DTB" "$BAK"
        echo "Backup angelegt: $BAK"
    else
        echo "Backup existiert bereits: $BAK"
    fi

    fdtput -cp "$DTB" /onewire
    fdtput -t s  "$DTB" /onewire compatible "w1-gpio"
    fdtput -t s  "$DTB" /onewire status    "okay"
    fdtput -t u  "$DTB" /onewire gpios     $GPIO_PHANDLE_DEC $PIN15_OFFSET 0

    echo "1-Wire (phys pin 15, gpiochip0 offset $PIN15_OFFSET) in DTB eingetragen."
    echo ""
    echo "Verifizierung:"
    dtc -I dtb -O dts "$DTB" 2>/dev/null | awk '/onewire/{f=1} f{print; if(/\}/) exit}'
}

# ── Board-Erkennung ──────────────────────────────────────────────────────────
if echo "$board" | grep -qiE "N2"; then
    echo "→ N2+ erkannt: DTB-Patch"
    echo ""
    fix_n2plus
    echo ""
    echo "── 1-Wire (phys pin 15) ─────────────────────────────────────────────────"
    fix_n2plus_onewire
elif echo "$board" | grep -qiE "M1"; then
    echo "→ M1 erkannt: Armbian-Overlay aktivieren"
    echo ""
    fix_m1
    echo ""
    echo "── GPIO Pull-Up Overlay ─────────────────────────────────────────────────"
    fix_m1_gpio_pullups
    echo ""
    echo "── 1-Wire Overlay (phys pin 15) ─────────────────────────────────────────"
    fix_m1_onewire
else
    echo "FEHLER: Board nicht unterstützt ('$board')"
    echo "Unterstützt: ODROID N2+, ODROID M1"
    exit 1
fi

echo ""
echo "Nach dem Reboot: i2cdetect -l  (Busnummer ermitteln), dann i2cdetect -y <N>  (DHT20 bei 0x38 erwartet)"
echo "                 gpioget -c gpiochip3 --bias=pull-up 24  (Pin 12 offen → sollte '1' liefern)"
echo "                 ls /sys/bus/w1/devices/  (1-Wire Sensoren erwartet)"
echo ""
read -rp "Jetzt rebooten? [j/N] " ans
[[ "$ans" =~ ^[jJyY]$ ]] && reboot || echo "Bitte manuell rebooten."
