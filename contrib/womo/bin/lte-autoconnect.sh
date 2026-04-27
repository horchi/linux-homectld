#!/bin/bash

LOGFILE="/var/log/lte.log"
APN="internet.eplus.de"
FW_SCRIPT="/usr/local/bin/msq.device"

# Modem-Index finden

get_modem_idx() {
   mmcli -L | grep -oP 'Modem/\K[0-9]+' | head -n 1
}

# Functions

do_up() {
   MODEM_IDX=$(get_modem_idx)
   if [ -z "$MODEM_IDX" ]; then
      echo "FEHLER: Kein Modem gefunden." | tee -a $LOGFILE
      exit 1
   fi

   echo "$(date '+%Y-%m-%d %H:%M:%S') - LTE wird gestartet..." >> $LOGFILE

   ip link set wwan0 down
   echo 'Y' > /sys/class/net/wwan0/qmi/raw_ip
   ip link set wwan0 up

   mmcli -m $MODEM_IDX --simple-connect="apn=$APN" >> $LOGFILE 2>&1

   B_IDX=$(mmcli -m $MODEM_IDX | grep -oP 'Bearer/\K[0-9]+' | tail -n 1)
   VALS=$(mmcli -m $MODEM_IDX -b $B_IDX --output-keyvalue)

   ADDR=$(echo "$VALS" | grep "bearer.ipv4-config.address" | cut -d: -f2 | tr -d ' ')
   PREF=$(echo "$VALS" | grep "bearer.ipv4-config.prefix" | cut -d: -f2 | tr -d ' ')
   GATE=$(echo "$VALS" | grep "bearer.ipv4-config.gateway" | cut -d: -f2 | tr -d ' ')

   if [[ "$ADDR" =~ ^[0-9] ]]; then
      ip addr flush dev wwan0
      ip addr add $ADDR/$PREF dev wwan0
      ip route del default dev wwan0 2>/dev/null
      ip route add default via $GATE dev wwan0 metric 10
      mmcli -m $MODEM_IDX --signal-setup=5 >/dev/null

      # Firewall Masquerading auf LTE setzen

      if [ -f "$FW_SCRIPT" ]; then
         $FW_SCRIPT -s wwan0
         echo "Firewall auf wwan0 umgestellt" | tee -a $LOGFILE
      fi

      echo "LTE verbunden: $ADDR" | tee -a $LOGFILE
   else
      echo "FEHLER: IP-Konfiguration fehlgeschlagen" | tee -a $LOGFILE
      exit 1
   fi
}

do_down() {
   MODEM_IDX=$(get_modem_idx)
   echo "$(date '+%Y-%m-%d %H:%M:%S') - LTE wird getrennt..." | tee -a $LOGFILE
   [ -n "$MODEM_IDX" ] && mmcli -m $MODEM_IDX --simple-disconnect >/dev/null 2>&1
   ip addr flush dev wwan0
   ip link set wwan0 down

   # Firewall Masquerading zurück auf WLAN setzen ---

   if [ -f "$FW_SCRIPT" ]; then
      $FW_SCRIPT -s wlan0
      echo "Firewall auf wlan0 zurückgestellt" | tee -a $LOGFILE
   fi

   echo "LTE getrennt"
}

do_status() {
   MODEM_IDX=$(get_modem_idx)

   # Check if modem exists for status report

   if [ -z "$MODEM_IDX" ]; then
      echo "PROVIDER:None"
      echo "INTERNET:OFFLINE"
      exit 0
   fi

   # Gather modem and network data

   DATA=$(mmcli -m $MODEM_IDX --output-keyvalue)
   STATE=$(echo "$DATA" | grep "modem.generic.state" | cut -d: -f2 | xargs)
   PROVIDER=$(echo "$DATA" | grep "modem.3gpp.operator-name" | cut -d: -f2 | xargs)
   [ -z "$PROVIDER" ] && PROVIDER="Searching..."

   # Extract access technology (e.g. lte, umts, gsm)

   TECH=$(echo "$DATA" | grep "modem.generic.access-technologies" | cut -d: -f2 | xargs || echo "Unknown")

   # Simplify tech string for display

   if [[ "$TECH" == *"lte"* ]]; then TECH_DISP="4G";
   elif [[ "$TECH" == *"umts"* || "$TECH" == *"hsdpa"* ]]; then TECH_DISP="3G";
   elif [[ "$TECH" == *"gsm"* || "$TECH" == *"edge"* ]]; then TECH_DISP="2G";
   else TECH_DISP="$TECH"; fi

   # Extract signal quality percentage and calculate approximate dBm

   QUALITY=$(echo "$DATA" | grep "modem.generic.signal-quality.value" | cut -d: -f2 | xargs || echo "0")
   RSSI_CALC=$(( (QUALITY / 2) - 113 ))
   IP=$(ip addr show wwan0 | grep "inet " | awk '{print $2}' || echo "none")

   # Verify internet connectivity strictly through LTE

   INTERNET="OFFLINE"
   if [ "$IP" != "none" ] && ping -I wwan0 -c 1 -W 2 8.8.8.8 >/dev/null 2>&1; then
      INTERNET="CONNECTED"
   fi

   # Output formatted for MQTT script parsing

   echo "MODEM_INDEX:$MODEM_IDX"
   echo "MODEM_STATUS:$STATE"
   echo "PROVIDER:$PROVIDER"
   echo "ACCESS_TECH:$TECH_DISP"
   echo "IP_ADDRESS:$IP"
   echo "SIGNAL_QUALITY:$QUALITY"
   echo "SIGNAL_RSSI_VALUE:$(echo $RSSI_CALC | tr -d '-')"
   echo "SIGNAL_DBM:$RSSI_CALC dBm"
   echo "INTERNET:$INTERNET"
}

# Main

case "$1" in
   up)
      do_up
      ;;
   down)
      do_down
      ;;
   status)
      do_status
      ;;
   *)
      # Cron-Modus: Nur reparieren, wenn systemd-Service aktiv ist

      if systemctl is-active --quiet lte-modem; then
         if ! ping -c 1 -W 2 8.8.8.8 >/dev/null 2>&1; then
            do_up
         fi
      fi
      ;;
esac
