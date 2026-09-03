#!/usr/bin/env python3

import argparse
import sys
import time
import json
import syslog
import re
import urllib.request
import urllib.error
from bs4 import BeautifulSoup
import paho.mqtt.client as mqtt

syslog.openlog(ident="weewxmqtt", logoption=syslog.LOG_PID)

# Global State
initial = True
args = None

# ── homectld widget parameters ────────────────────────────────────────────────
parameters = [None] * 8

# 0: Wind Speed (Knots)
parameters[0] = '{"parameter": {"cloneable": false, "widgettype": 3, "symbol": "mdi:mdi-weather-windy", "symbolOn": "mdi:mdi-weather-windy"}}'
# 1: Wind Gust Max Today (Knots)
parameters[1] = '{"parameter": {"cloneable": false, "widgettype": 3, "symbol": "mdi:mdi-weather-windy-variant", "symbolOn": "mdi:mdi-weather-windy-variant"}}'
# 2: Wind Direction (Grad)
parameters[2] = '{"parameter": {"cloneable": false, "widgettype": 3, "symbol": "mdi:mdi-compass", "symbolOn": "mdi:mdi-compass"}}'
# 3: Wind Direction (Text)
parameters[3] = '{"parameter": {"cloneable": false, "widgettype": 2, "symbol": "mdi:mdi-compass", "symbolOn": "mdi:mdi-compass"}}'
# 4: Outside Temperature (°C)
parameters[4] = '{"parameter": {"cloneable": false, "widgettype": 6, "symbol": "mdi:mdi-thermometer", "symbolOn": "mdi:mdi-thermometer"}}'
# 5: Humidity (%)
parameters[5] = '{"parameter": {"cloneable": false, "widgettype": 3, "symbol": "mdi:mdi-water-percent", "symbolOn": "mdi:mdi-water-percent"}}'
# 6: Barometer (mbar)
parameters[6] = '{"parameter": {"cloneable": false, "widgettype": 3, "symbol": "mdi:mdi-gauge", "symbolOn": "mdi:mdi-gauge"}}'
# 7: Station Timestamp
parameters[7] = '{"parameter": {"cloneable": false, "widgettype": 2}}'

def tell(level, msg):
    if args.v >= level:
        if args.l:
            syslog.syslog(syslog.LOG_INFO, msg)
        else:
            print(msg)

def mqttConnect():
    try:
        tell(0, f"Connecting to MQTT broker at {args.m.strip()}:{args.p}...")
        mqttClient.connect(args.m.strip(), args.p, 60)
    except Exception as e:
        tell(0, f"MQTT connection routine crash: {e}")

def publishMqtt(sensor):
    global initial
    addr = sensor['address']
    if initial and addr < len(parameters) and parameters[addr] is not None:
        tell(2, "appending {}".format(parameters[addr]))
        p = json.loads(parameters[addr])
        sensor.update(p)

    if args.m.strip() != '':
        msg = json.dumps(sensor)
        tell(2, '{}'.format(msg))
        rc = mqttClient.publish(args.T.strip(), msg)
        if rc.rc != 0:
            tell(0, f"MQTT publish failed (rc={rc.rc}), reconnecting")
            mqttConnect()
            mqttClient.publish(args.T.strip(), msg)
        mqttClient.loop(0.1)

    if args.s:
        wert = sensor.get('value') if sensor.get('value') not in (None, "") else (sensor.get('text') or sensor.get('state'))
        if wert is not None and wert != "":
            tell(0, "  {}: {} {}".format(sensor['title'], wert, sensor.get('unit', '')))

def fetchWeewxData(url):
    data = {}
    try:
        req = urllib.request.Request(url, headers={'User-Agent': 'homectld-weewx/1.0'})
        with urllib.request.urlopen(req, timeout=10) as response:
            html = response.read().decode('utf-8', errors='ignore')

        soup = BeautifulSoup(html, 'html.parser')

        # 1. Station Timestamp
        pTime = soup.find('p')
        if pTime:
            data['timeStr'] = pTime.get_text(strip=True)

        # 2. Tabellenwerte auslesen
        for row in soup.find_all('tr'):
            cols = row.find_all(['td', 'th'])
            if len(cols) >= 2:
                label = cols[0].get_text(strip=True)
                valText = cols[1].get_text(separator=' ', strip=True)

                if label == "Outside Temperature":
                    m = re.search(r'(-?\d+[\.,]\d+|-?\d+)', valText)
                    if m:
                        data['temp'] = float(m.group(1).replace(',', '.'))

                elif label == "Humidity":
                    m = re.search(r'(\d+)', valText)
                    if m:
                        data['humidity'] = int(m.group(1))

                elif label == "Barometer":
                    m = re.search(r'(-?\d+[\.,]\d+|-?\d+)', valText)
                    if m:
                        data['barometer'] = float(m.group(1).replace(',', '.'))

                elif label == "Wind":
                    # Liest km/h aus und rechnet direkt in Knoten um
                    m = re.search(r'(\d+)\s*km/h\s+([A-Z]+)?\s*\((?:N/A|(\d+)°)\)', valText)
                    if m:
                        kmh = float(m.group(1))
                        data['windSpeedKn'] = round(kmh / 1.852, 1)
                        data['windDirText'] = m.group(2) if m.group(2) else ""
                        data['windDirDeg'] = int(m.group(3)) if m.group(3) else 0

                elif label == "Wind Max":
                    m = re.search(r'(\d+)', valText)
                    if m:
                        kmhMax = float(m.group(1))
                        data['windGustKn'] = round(kmhMax / 1.852, 1)

    except Exception as e:
        tell(0, f"Error fetching WeeWX data: {e}")
        return None

    return data

def main():
    global args, mqttClient, initial
    progName = "weewxmqtt"
    parser = argparse.ArgumentParser(progName)
    parser.add_argument('-i', type=int, nargs='?', help='Interval [seconds] (default 60)', default=60)
    parser.add_argument('-m',           nargs='?', help='MQTT host', default="localhost")
    parser.add_argument('-p', type=int, nargs='?', help='MQTT port', default=1883)
    parser.add_argument('-v', type=int, nargs='?', help='Verbosity level (0-3) (default 0)', default=0)
    parser.add_argument('-l', action='store_true', help='Log to syslog (default console)')
    parser.add_argument('-T',           nargs='?', help='MQTT topic', default="homectld2mqtt/weewx")
    parser.add_argument('-t',           nargs='?', help='Sensor type string', default="WEEWX")
    parser.add_argument('-u',           nargs='?', help='WeeWX URL', default="https://wsce.de/Wetter/weewx/index.html")
    parser.add_argument('-s', action='store_true', help='Show current status and exit')

    args = parser.parse_args()

    mqttClient = mqtt.Client()
    if not args.s and args.m.strip() != '':
        mqttConnect()
        mqttClient.loop_start()

    if args.s:
        args.i = 1
        args.m = ""

    while True:
        try:
            if not args.s:
                tell(0, "Fetching WeeWX data ...")

            data = fetchWeewxData(args.u)

            if data:
                sensorsToPublish = [
                    {"type": args.t, "address": 0, "kind": "value", "title": "Wind Speed",     "value": data.get('windSpeedKn', 0.0), "unit": "kn"},
                    {"type": args.t, "address": 1, "kind": "value", "title": "Wind Gust Max",  "value": data.get('windGustKn', 0.0),  "unit": "kn"},
                    {"type": args.t, "address": 2, "kind": "value", "title": "Wind Direction", "value": data.get('windDirDeg', 0),    "unit": "°"},
                    {"type": args.t, "address": 3, "kind": "text",  "title": "Wind Direction", "text":  data.get('windDirText', ""),  "unit": "txt"},
                    {"type": args.t, "address": 4, "kind": "value", "title": "Outside Temp",   "value": data.get('temp', 0.0),        "unit": "°C"},
                    {"type": args.t, "address": 5, "kind": "value", "title": "Humidity",       "value": data.get('humidity', 0),      "unit": "%"},
                    {"type": args.t, "address": 6, "kind": "value", "title": "Barometer",      "value": data.get('barometer', 0.0),   "unit": "mbar"},
                    {"type": args.t, "address": 7, "kind": "text",  "title": "Station Time",   "text":  data.get('timeStr', ""),  "unit": "txt"}
                ]

                if not args.s:
                    tell(2, f"Executing serialized interval loop publish event for {len(sensorsToPublish)} sensors...")

                for sensor in sensorsToPublish:
                    publishMqtt(sensor)

                initial = False

                if args.s:
                    sys.exit(0)

                tell(0, "... done")

            else:
                tell(0, "Failed to retrieve valid data.")
                if args.s:
                    sys.exit(1)

            time.sleep(args.i)

        except KeyboardInterrupt:
            tell(1, "Process execution stopped manually.")
            break
        except Exception as err:
            tell(0, f"Unexpected operational routine error: {err}")
            time.sleep(5)

    if not args.s and args.m.strip() != '':
        mqttClient.loop_stop()
        mqttClient.disconnect()

if __name__ == "__main__":
    main()
