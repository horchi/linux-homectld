#!/usr/bin/env python3

import argparse
import sys
import os
import time
import json
import logging
import logging.handlers
import serial
import pynmea2
import paho.mqtt.client as mqtt

# Hardware Config
SERIAL_PORT = "/dev/ttyGps"
BAUDRATE = 9600

# Global State to match your framework patterns
initial = True
parameters = {}
args = None
logger = None

# ── homectld widget parameters ────────────────────────────────────────────────
# address → parameter JSON appended on initial publish

parameters = [None] * 8

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

def tell(level, message):
    """Verbose console and syslog router mirroring your framework logger."""
    if level == 0:
        logger.error(message)
    elif level == 1:
        logger.warning(message)
    elif level == 2:
        logger.info(message)
    elif level == 3:
        logger.debug(message)

def setup_logging(to_syslog, verbosity, prog_name):
    global logger
    logger = logging.getLogger(prog_name)

    if verbosity >= 3:
        logger.setLevel(logging.DEBUG)
    elif verbosity == 2:
        logger.setLevel(logging.INFO)
    elif verbosity == 1:
        logger.setLevel(logging.WARNING)
    else:
        logger.setLevel(logging.ERROR)

    if to_syslog:
        handler = logging.handlers.SysLogHandler(address='/dev/log', facility=logging.handlers.SysLogHandler.LOG_DAEMON)
        formatter = logging.Formatter(f'{prog_name}: [%(levelname)s] %(message)s')
    else:
        handler = logging.StreamHandler(sys.stdout)
        formatter = logging.Formatter('%(message)s')

    handler.setFormatter(formatter)
    logger.addHandler(handler)

def mqttConnect():
    try:
        tell(2, f"Connecting to MQTT broker at {args.m.strip()}:{args.p}...")
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
        tell(0, "  {}: {} {}".format(sensor['title'], sensor['value'], sensor.get('unit', '')))

def main():
    global args, mqtt_client, initial
    prog_name = "gpsmqtt"
    parser = argparse.ArgumentParser(prog_name)
    parser.add_argument('-i', type=int, nargs='?', help='Interval [seconds] (default 30)', default=30)
    parser.add_argument('-m',           nargs='?', help='MQTT host', default="localhost")
    parser.add_argument('-p', type=int, nargs='?', help='MQTT port', default=1883)
    parser.add_argument('-v', type=int, nargs='?', help='Verbosity level (0-3) (default 0)', default=0)
    parser.add_argument('-l', action='store_true', help='Log to syslog (default console)')
    parser.add_argument('-T',           nargs='?', help='MQTT topic', default="homectld2mqtt/gps")
    parser.add_argument('-t',           nargs='?', help='Sensor type string', default="GPS")
    parser.add_argument('-M',           nargs='?', help='Device base address offset (default 0)', default="0")
    parser.add_argument('-s', action='store_true', help='Show current status and exit')

    args = parser.parse_args()
    setup_logging(args.l, args.v, prog_name)

    # Base address calculation offset derived from -M flag
    try:
        base_addr = int(args.M)
    except ValueError:
        base_addr = 90

    # Local MQTT Client registration setup
    mqtt_client = mqtt.Client()
    if args.m.strip() != '':
        mqttConnect()
        mqtt_client.loop_start()

    # Open USB GPS Hardware handle
    try:
        ser = serial.Serial(SERIAL_PORT, BAUDRATE, timeout=1)
        tell(2, f"Opened serial interface link {SERIAL_PORT} successfully.")
    except Exception as e:
        tell(0, f"Failed to initialize serial hardware interface {SERIAL_PORT}: {e}")
        sys.exit(1)

    # State accumulation dictionary
    raw_data = {}
    last_publish_time = 0

    while True:
        try:
            line = ser.readline().decode('ascii', errors='replace').strip()
            if not line:
                continue

            tell(3, f"Raw NMEA: {line}")

            if line.startswith(('$GPRMC', '$GNGRM', '$GPGGA', '$GNGGA')):
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

            # Interval scheduler execution check
            current_time = time.time()
            if current_time - last_publish_time >= args.i:
                if raw_data:
                    # Construct individual standalone sensor datasets to fire sequentially
                    ts = int(current_time)

                    # Calculate true/false based on the raw_data status
                    is_fixed = True if raw_data.get('status') == 'Fix' else False

                    sensors_to_publish = [
                        {"type": args.t, "address": 0, "kind": "status", "title": "Sat State",  "state": is_fixed},
                        {"type": args.t, "address": 1, "kind": "value",  "title": "Latitude",   "value": raw_data.get('latitude', 0.0), "unit": "°"},
                        {"type": args.t, "address": 2, "kind": "value",  "title": "Longitude",  "value": raw_data.get('longitude', 0.0), "unit": "°"},
                        {"type": args.t, "address": 3, "kind": "value",  "title": "Speed",      "value": raw_data.get('speed', 0.0), "unit": "km/h"},
                        {"type": args.t, "address": 4, "kind": "value",  "title": "Satellites", "value": raw_data.get('satellites', 0)},
                        {"type": args.t, "address": 5, "kind": "value",  "title": "Altitude",   "value": raw_data.get('altitude', 0.0), "unit": "m"},
                        {"type": args.t, "address": 6, "kind": "text",   "title": "Time UTC",    "text": raw_data.get('time_utc', "")},
                        {"type": args.t, "address": 7, "kind": "text",   "title": "Date UTC",    "text": raw_data.get('date_utc', "")}
                    ]

                    tell(2, f"Executing serialized interval loop publish event for {len(sensors_to_publish)} sensors...")

                    for sensor in sensors_to_publish:
                        publishMqtt(sensor)

                    # Toggle initial block execution off after first full sequence completion
                    initial = False
                    last_publish_time = current_time

                    if args.s:
                        sys.exit(0)

        except KeyboardInterrupt:
            tell(1, "Process execution stopped manually.")
            break
        except Exception as err:
            tell(0, f"Unexpected operational routine error: {err}")
            time.sleep(2)

    ser.close()
    if args.m.strip() != '':
        mqtt_client.loop_stop()
        mqtt_client.disconnect()

if __name__ == "__main__":
    main()
