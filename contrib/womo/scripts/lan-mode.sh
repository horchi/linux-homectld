#!/bin/sh
set -eu

STATE_FILE="/run/lan-mode.state"
CONF_LINK="/etc/dnsmasq.d/10-womo.conf"
ONLINE_CONF="/etc/dnsmasq.profiles/womo-online.conf"
OFFLINE_CONF="/etc/dnsmasq.profiles/womo-offline.conf"

# --- WAN-Check (bewusst einfach & stabil) ---
if ping -c1 -W1 8.8.8.8 >/dev/null 2>&1; then
    NEW_STATE="online"
    TARGET_CONF="$ONLINE_CONF"
else
    NEW_STATE="offline"
    TARGET_CONF="$OFFLINE_CONF"
fi

# --- aktuellen Zustand lesen ---
OLD_STATE=""
[ -f "$STATE_FILE" ] && OLD_STATE="$(cat "$STATE_FILE")"

# --- nichts geändert? -> exit
if [ "$NEW_STATE" = "$OLD_STATE" ]; then
    exit 0
fi

# --- Zustand hat sich geändert ---
echo "$NEW_STATE" > "$STATE_FILE"

# Symlink aktualisieren (atomar)
ln -sf "$TARGET_CONF" "$CONF_LINK"

# dnsmasq hart neu starten
systemctl restart dnsmasq

logger "lan-mode: switched to $NEW_STATE"
