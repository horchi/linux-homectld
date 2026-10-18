#!/usr/bin/env python3

import argparse
import sys
import os
import time
import json
import re
import syslog
import serial
import pynmea2
import paho.mqtt.client as mqtt

import urllib.request
from geopy.geocoders import Nominatim
from geopy.exc import GeocoderServiceError

syslog.openlog(ident="gpsmqtt", logoption=syslog.LOG_PID)

# Hardware Config (defaults, options -d / -b)
SERIAL_PORT = "/dev/ttyGps"
BAUDRATE = 9600

# Global State to match your framework patterns
initial = True
parameters = {}
args = None

# receiver settings requested by homectld (Setup -> Konfiguration -> GPS), applied on every
#  port open and on every 'config' message: { "action": "config", "staticHold": <cm/s>, "dynModel": 0|4, "galileo": true|false }
ubloxConfig = None
ubloxConfigPending = False

# ── homectld widget parameters ────────────────────────────────────────────────
# address → parameter JSON appended on initial publish

parameters = [None] * 12

# 0: Sat State (Fix / No Fix Status)
parameters[0] = '{"parameter": {"cloneable": false, "widgettype": 0, "symbol": "mdi:mdi-satellite-variant", "symbolOn": "mdi:mdi-satellite-variant"}}'
# 1: Latitude (Geodaten Breitengrad)
parameters[1] = '{"parameter": {"cloneable": false, "widgettype": 3}}'
# 2: Longitude (Geodaten Längengrad)
parameters[2] = '{"parameter": {"cloneable": false, "widgettype": 3}}'
# 3: Speed (Fahrgeschwindigkeit in km/h)
parameters[3] = '{"parameter": {"cloneable": false, "widgettype": 3}}'
# 4: Satellites (Anzahl gefundener Satelliten)
parameters[4] = '{"parameter": {"cloneable": false, "widgettype": 3}}'
# 5: Altitude (Höhe über dem Meeresspiegel in Metern)
parameters[5] = '{"parameter": {"cloneable": false, "widgettype": 3}}'
# 6: Time UTC (Atomgenaue GPS-Zeit)
parameters[6] = '{"parameter": {"cloneable": false, "widgettype": 2}}'
# 7: Date UTC (Aktuelles GPS-Datum)
parameters[7] = '{"parameter": {"cloneable": false, "widgettype": 2}}'
# 8: Town (Aktuelle Location/Town)
parameters[8] = '{"parameter": {"cloneable": false, "widgettype": 2}}'
# 9: Address (Aktuelle Location/Adresse)
parameters[9] = '{"parameter": {"cloneable": false, "widgettype": 2}}'
# 10: Coordinate (Latitude/Longitude)
parameters[10] = '{"parameter": {"cloneable": false, "widgettype": 2}}'
# 11: Heading - course over ground of the last movement (>= 5 km/h), kept while standing: the vehicle axis
parameters[11] = '{"parameter": {"cloneable": false, "widgettype": 3}}'

def tell(level, msg):
    if args.v >= level:
        if args.l:
            syslog.syslog(syslog.LOG_INFO, msg)
        else:
            print(msg)

def splitText(text, max_len=8):
    if not text or len(str(text)) <= max_len:
        return text

    text_str = str(text)

    # Sucht nach einer sinnvollen Stelle (Vokal + Konsonant oder Trennzeichen) im Bereich von Zeichen 4 bis 10
    match = re.search(r'^(.{4,10}[aeiouäöüÄÖÜ]h?)(?=[bcdfghjklmnpqrstvwxyzBCDFGHJKLMNPQRSTVWXYZ])|^(.{4,10}[- /])', text_str)

    if match:
        trennstelle = len(match.group(0))
        return f"{text_str[:trennstelle]}-\n{text_str[trennstelle:]}"

    # Fallback: Stur nach der maximalen Länge trennen, wenn kein Muster passt
    return f"{text_str[:max_len]}-\n{text_str[max_len:]}"

def isOnline():
    try:
        urllib.request.urlopen("https://openstreetmap.org", timeout=2)
        return True
    except Exception:
        return False

def getLocation(lat, lng):
    if not isOnline():
        return "offline"

    try:
        geolocator = Nominatim(user_agent="homectld")
        loc = geolocator.reverse(f"{lat}, {lng}", timeout=3)

        if loc and 'address' in loc.raw:
            for key, val in loc.raw['address'].items():
                # Direkter Aufruf im exakten Format Ihrer tell-Methode
                # Da es keine Einheit gibt, bleibt der letzte Platzhalter leer
                tell(2, "  {}: {} {}".format(key.capitalize(), val, ""))

        return loc if loc else "No location found"

    except GeocoderServiceError:
        return "offline"


def getTown(lat, lng):
    town = "---"
    loc = getLocation(lat, lng)

    if loc == "offline" or loc == "No location found":
        return loc

    if hasattr(loc, 'raw') and 'address' in loc.raw:
        addr = loc.raw['address']

        # 1. Village oder City
        # 2. Wenn nicht vorhanden: Municipality
        # 3. Ansonsten: County oder State
        town = (
            addr.get('village') or
            addr.get('city') or
            addr.get('municipality') or
            addr.get('county') or
            addr.get('state') or
            "---")

    return town

def getAddress(lat, lng):
    loc = getLocation(lat, lng)

    if loc == "offline" or loc == "No location found":
        return loc

    if hasattr(loc, 'raw') and 'address' in loc.raw:
        addr = loc.raw['address']

        # Line 1: Street and house number
        street = addr.get('road', '')
        house_number = addr.get('house_number', '')
        line1 = f"{street} {house_number}".strip()

        # Line 2: Postal code and town
        postcode = addr.get('postcode', '')
        town = addr.get('village') or addr.get('city') or addr.get('town') or addr.get('municipality') or ''
        line2 = f"{postcode} {town}".strip()

        # Line 3: State and Country in one line
        state = addr.get('state', '')
        country = addr.get('country', '')

        if state and country:
            line3 = f"{state}, {country}"
        else:
            line3 = state or country

        address_lines = [line1, line2, line3]
        formatted_address = "\n".join([line for line in address_lines if line])

        return formatted_address if formatted_address else "Address incomplete"

    return "Unknown"

# ── u-blox receiver settings (UBX protocol) ───────────────────────────────────
# CFG-NAV5: dynamic platform model (0 portable, 4 automotive) and static hold threshold [cm/s]
#  (below this speed the receiver freezes the position - no drift at standstill)

def ubxFrame(cls, mid, payload=b''):
    body = bytes([cls, mid, len(payload) & 0xff, len(payload) >> 8]) + payload
    a = b = 0
    for x in body:
        a = (a + x) & 0xff
        b = (b + a) & 0xff
    return b'\xb5\x62' + body + bytes([a, b])

def ubxRead(ser, cls, mid, wait=1.5):
    # the answer (or the ACK) of a request within the NMEA stream; the pending NMEA line is lost
    buf = b''
    t0 = time.time()
    while time.time() - t0 < wait:
        buf += ser.read(256)
        i = buf.find(b'\xb5\x62' + bytes([cls, mid]))
        if i < 0:
            continue
        if len(buf) < i + 6:
            continue
        n = buf[i+4] | (buf[i+5] << 8)
        if len(buf) >= i + 6 + n + 2:
            return buf[i+6:i+6+n]
    return None

def ubloxApplyConfig(ser):
    global ubloxConfigPending
    ubloxConfigPending = False

    if ubloxConfig is None or ser is None or not ser.is_open:
        return

    dynModel = int(ubloxConfig.get('dynModel', 0))
    staticHold = max(0, min(255, int(ubloxConfig.get('staticHold', 0))))

    payload = bytearray(36)
    payload[0:2] = (0x0041).to_bytes(2, 'little')   # mask: dyn (bit 0) and staticHoldMask (bit 6)
    payload[2] = dynModel
    payload[22] = staticHold

    try:
        ack = None
        for attempt in range(2):
            ser.write(ubxFrame(0x06, 0x24, bytes(payload)))
            ack = ubxRead(ser, 0x05, 0x01)
            if ack is not None and ack[0:2] == b'\x06\x24':
                break
        else:
            tell(0, f"u-blox: CFG-NAV5 not acknowledged (dynModel {dynModel}, staticHold {staticHold} cm/s)")
            return

        # read back for the log
        ser.write(ubxFrame(0x06, 0x24))
        nav5 = ubxRead(ser, 0x06, 0x24)
        if nav5 and len(nav5) >= 23:
            tell(0, f"u-blox: CFG-NAV5 set, dynModel {nav5[2]}, staticHold {nav5[22]} cm/s ({nav5[22] * 0.036:.1f} km/h)")
        else:
            tell(0, f"u-blox: CFG-NAV5 set (dynModel {dynModel}, staticHold {staticHold} cm/s)")

        if ubloxConfig.get('galileo'):
            ubloxEnableGalileo(ser)
    except (serial.SerialException, OSError) as e:
        tell(0, f"u-blox: setting CFG-NAV5 failed: {e}")

# CFG-GNSS (u-blox 8): GPS + Galileo + GLONASS (the M8 tracks three major systems at a time), SBAS and
#  QZSS as augmentation, BeiDou off. The u-blox 7 knows no Galileo and answers with NAK; the receiver
#  restarts its navigation after the change (the fix comes back after some seconds)

GNSS_NAMES = {0: 'GPS', 1: 'SBAS', 2: 'Galileo', 3: 'BeiDou', 4: 'IMES', 5: 'QZSS', 6: 'GLONASS'}

def ubloxEnableGalileo(ser):
    def block(gnssId, resTrkCh, maxTrkCh, flags):
        return bytes([gnssId, resTrkCh, maxTrkCh, 0]) + flags.to_bytes(4, 'little')

    blocks = (block(0, 8, 16, 0x01010001) +    # GPS      L1C/A  on
              block(1, 1, 3, 0x01010001) +     # SBAS     L1C/A  on
              block(2, 4, 8, 0x01010001) +     # Galileo  E1     on
              block(3, 8, 16, 0x01010000) +    # BeiDou   B1I    off
              block(4, 0, 8, 0x03010000) +     # IMES            off
              block(5, 0, 3, 0x05010001) +     # QZSS     L1C/A  on (u-blox: together with GPS)
              block(6, 8, 14, 0x01010001))     # GLONASS  L1OF   on
    payload = bytes([0, 0, 0xff, 7]) + blocks   # msgVer, numTrkChHw (read only), numTrkChUse (max), blocks

    ser.write(ubxFrame(0x06, 0x3e, payload))
    ack = ubxRead(ser, 0x05, 0x01)

    if ack is None or ack[0:2] != b'\x06\x3e':
        tell(0, "u-blox: CFG-GNSS (Galileo) not acknowledged - not supported by this receiver (u-blox 7 has no Galileo)")
        return

    ser.write(ubxFrame(0x06, 0x3e))
    cfg = ubxRead(ser, 0x06, 0x3e)

    if cfg and len(cfg) >= 4:
        enabled = []
        for i in range(cfg[3]):
            b = cfg[4 + i*8: 12 + i*8]
            if len(b) == 8 and int.from_bytes(b[4:8], 'little') & 1:
                enabled.append(GNSS_NAMES.get(b[0], str(b[0])))
        tell(0, "u-blox: CFG-GNSS set, enabled: " + ", ".join(enabled))
    else:
        tell(0, "u-blox: CFG-GNSS set (GPS, Galileo, GLONASS)")

def onMqttMessage(client, userdata, msg):
    global ubloxConfig, ubloxConfigPending
    try:
        data = json.loads(msg.payload.decode('utf-8', errors='replace'))
    except Exception as e:
        tell(0, f"Ignoring invalid JSON on {msg.topic}: {e}")
        return

    if data.get('action') == 'config':
        ubloxConfig = data
        ubloxConfigPending = True    # applied by the main loop (it owns the serial port)
        tell(1, f"Received receiver settings: {data}")

def onMqttConnect(client, userdata, flags, rc):
    # (re)subscribe and register the command topic at homectld, it answers with the receiver settings
    tell(1, f"MQTT connected (rc={rc}), subscribing {args.c.strip()}")
    client.subscribe(args.c.strip())
    client.publish(args.T.strip(), json.dumps({"action": "init", "type": args.t, "topic": args.c.strip()}))

def mqttConnect():
    try:
        tell(0, f"Connecting to MQTT broker at {args.m.strip()}:{args.p}...")
        mqtt_client.connect(args.m.strip(), args.p, 60)
    except Exception as e:
        tell(0, f"MQTT connection routine crash: {e}")

def publishMqtt(sensor):
    global initial
    addr = sensor['address']
    if initial and parameters[addr] is not None:
        tell(2, "appending {}".format(parameters[addr]))
        p = json.loads(parameters[addr])
        sensor.update(p)
    if args.m.strip() != '':
        msg = json.dumps(sensor)
        tell(2, '{}'.format(msg))
        rc = mqtt_client.publish(args.T.strip(), msg)
        if rc.rc != 0:
            tell(0, f"MQTT publish failed (rc={rc.rc}), reconnecting")
            mqttConnect()
            mqtt_client.publish(args.T.strip(), msg)
        mqtt_client.loop(0.1)

    if args.s:
        wert = sensor.get('value') if sensor.get('value') not in (None, "") else (sensor.get('text') or sensor.get('state'))
        if wert is not None and wert != "":
            tell(0, "  {}: {} {}".format(sensor['title'], wert, sensor.get('unit', '')))

def main():
    global args, mqtt_client, initial, SERIAL_PORT, BAUDRATE
    initial_start_time = time.time()
    prog_name = "gpsmqtt"
    parser = argparse.ArgumentParser(prog_name)
    parser.add_argument('-i', type=int, nargs='?', help='Interval [seconds] (default 30)', default=30)
    parser.add_argument('-d',           nargs='?', help='Serial device (default /dev/ttyGps)', default=SERIAL_PORT)
    parser.add_argument('-b', type=int, nargs='?', help='Baud rate (default 9600, ignored by USB receivers)', default=BAUDRATE)
    parser.add_argument('-m',           nargs='?', help='MQTT host', default="localhost")
    parser.add_argument('-p', type=int, nargs='?', help='MQTT port', default=1883)
    parser.add_argument('-v', type=int, nargs='?', help='Verbosity level (0-3) (default 0)', default=0)
    parser.add_argument('-l', action='store_true', help='Log to syslog (default console)')
    parser.add_argument('-T',           nargs='?', help='MQTT topic', default="homectld2mqtt/gps")
    parser.add_argument('-c',           nargs='?', help='MQTT command topic (receiver settings from homectld, default <topic>/in)', default=None)
    parser.add_argument('-t',           nargs='?', help='Sensor type string (default GPS)', default="GPS")
    parser.add_argument('-s', action='store_true', help='Show current status and exit')

    args = parser.parse_args()

    SERIAL_PORT = args.d
    BAUDRATE = args.b

    if not args.c:
        args.c = args.T.strip() + "/in"

    mqtt_client = mqtt.Client()
    mqtt_client.on_connect = onMqttConnect
    mqtt_client.on_message = onMqttMessage
    if not args.s and args.m.strip() != '':
        mqttConnect()
        mqtt_client.loop_start()

    raw_data = {}
    last_publish_time = 0
    ser = None

    if args.s:
        args.i = 1
        args.m = ""

    while True:
        try:
            if ser is None or not ser.is_open:
                try:
                    tell(2, f"Attempting hardware connection to {SERIAL_PORT}...")
                    ser = serial.Serial(SERIAL_PORT, BAUDRATE, timeout=1)
                    tell(2, f"Opened serial interface link {SERIAL_PORT} successfully.")
                    ubloxApplyConfig(ser)
                except (serial.SerialException, OSError) as e:
                    if args.s:
                        tell(0, f"Hardware execution check failed: {e}")
                        sys.exit(1)
                    tell(0, f"Hardware missing or connection failed: {e}. Retrying in 5s...")
                    raw_data['status'] = "No Fix"
                    time.sleep(5)
                    continue

            if ubloxConfigPending:
                ubloxApplyConfig(ser)

            try:
                line = ser.readline().decode('ascii', errors='replace').strip()
            except (serial.SerialException, OSError) as e:
                if args.s:
                    sys.exit(1)
                tell(0, f"Hardware disconnected during read execution: {e}")
                if ser:
                    try: ser.close()
                    except Exception: pass
                ser = None
                continue

            if not line: continue

            tell(3, f"Raw NMEA: {line}")

            # RMC and GGA of any talker: GP (GPS only), GN (combined), GL / GA / GB (single systems)

            if len(line) > 6 and line[0] == '$' and line[3:6] in ('RMC', 'GGA'):
                try:
                    msg = pynmea2.parse(line)

                    if hasattr(msg, 'latitude') and msg.latitude != 0.0:
                        raw_data['latitude'] = round(msg.latitude, 6)
                        raw_data['longitude'] = round(msg.longitude, 6)
                        if hasattr(msg, 'status') and msg.status is not None:
                            raw_data['status'] = "Fix" if msg.status == 'A' else "No Fix"
                        elif 'status' not in raw_data:
                            raw_data['status'] = "Fix"
                    else:
                        if 'latitude' not in raw_data:
                            raw_data['status'] = "No Fix"

                    if hasattr(msg, 'spd_over_grnd') and msg.spd_over_grnd is not None:
                        raw_data['speed'] = round(msg.spd_over_grnd * 1.852, 1)

                        # the course is valid while moving, kept while standing (the vehicle axis for the dish alignment)
                        if raw_data['speed'] >= 5.0 and getattr(msg, 'true_course', None) is not None:
                            raw_data['heading'] = round(msg.true_course, 1)

                    if hasattr(msg, 'num_sats'):
                        raw_data['satellites'] = int(msg.num_sats)
                    if hasattr(msg, 'altitude') and msg.altitude is not None:
                        raw_data['altitude'] = msg.altitude

                    if hasattr(msg, 'timestamp') and msg.timestamp is not None:
                        raw_data['time_utc'] = str(msg.timestamp)
                    if hasattr(msg, 'datestamp') and msg.datestamp is not None:
                        raw_data['date_utc'] = str(msg.datestamp)

                except pynmea2.ParseError:
                    continue

            current_time = time.time()

            if last_publish_time == 0:
                if args.s and current_time - initial_start_time < 2:
                    continue
                if not args.s:
                    last_publish_time = current_time - args.i

            if current_time - last_publish_time >= args.i:
                if not args.s:
                    tell(0, "Updating ...")

                if raw_data:
                    ts = int(current_time)
                    is_fixed = True if raw_data.get('status') == 'Fix' else False
                    satellites = raw_data.get('satellites', 0)
                    town = address = ""

                    if 'latitude' in raw_data:
                        town = getTown(raw_data['latitude'], raw_data['longitude'])
                        address = getAddress(raw_data['latitude'], raw_data['longitude'])
                    coordinate = f"{raw_data.get('latitude', 0.0)}/{raw_data.get('longitude', 0.0)}"

                    sensors_to_publish = [
                        {"type": args.t, "address":  0, "kind": "status", "title": "Sat State",  "state": is_fixed},
                        {"type": args.t, "address":  4, "kind": "value",  "title": "Satellites", "value": satellites},
                        {"type": args.t, "address":  6, "kind": "text",   "title": "Time UTC",    "text": raw_data.get('time_utc', "")},
                        {"type": args.t, "address":  7, "kind": "text",   "title": "Date UTC",    "text": raw_data.get('date_utc', "")}
                    ]

                    # the position only once the receiver delivered one (no 0/0 after the start)

                    if 'latitude' in raw_data:
                        sensors_to_publish += [
                            {"type": args.t, "address":  1, "kind": "value",  "title": "Latitude",   "value": raw_data.get('latitude', 0.0), "unit": "°"},
                            {"type": args.t, "address":  2, "kind": "value",  "title": "Longitude",  "value": raw_data.get('longitude', 0.0), "unit": "°"},
                            {"type": args.t, "address":  3, "kind": "value",  "title": "Speed",      "value": raw_data.get('speed', 0.0), "unit": "km/h"},
                            {"type": args.t, "address":  5, "kind": "value",  "title": "Altitude",   "value": raw_data.get('altitude', 0.0), "unit": "m"},
                            {"type": args.t, "address":  8, "kind": "text",   "title": "Town",        "text": splitText(town)},
                            {"type": args.t, "address":  9, "kind": "text",   "title": "Address",     "text": address},
                            {"type": args.t, "address": 10, "kind": "text",   "title": "Coordinate",  "text": coordinate}
                        ]

                    if 'heading' in raw_data:
                        sensors_to_publish.append({"type": args.t, "address": 11, "kind": "value", "title": "Heading", "value": raw_data['heading'], "unit": "°"})

                    if not args.s:
                        tell(2, f"Executing serialized interval loop publish event for {len(sensors_to_publish)} sensors...")

                    for sensor in sensors_to_publish:
                        publishMqtt(sensor)

                    initial = False
                    last_publish_time = current_time

                    if args.s:
                        if ser and ser.is_open:
                            ser.close()
                        sys.exit(0)

                    tell(0, "... done")

        except KeyboardInterrupt:
            tell(1, "Process execution stopped manually.")
            break
        except Exception as err:
            tell(0, f"Unexpected operational routine error: {err}")
            time.sleep(2)

    if ser and ser.is_open:
        ser.close()
    if not args.s and args.m.strip() != '':
        mqtt_client.loop_stop()
        mqtt_client.disconnect()


if __name__ == "__main__":
    main()
