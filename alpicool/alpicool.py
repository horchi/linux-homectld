#!/usr/bin/env python3

# Alpicool/MANTUM/BrassMonkey/Vevor BLE compressor fridge integration for homectld
# Protocol: Alpicool BLE (Service FFE0, Characteristic FFE1)
# Compatible with: MANTUM IceCube, Alpicool, BrassMonkey, Vevor, Iceco (same OEM platform)
#
# NOTE: Protocol byte offsets are based on community reverse-engineering of the Alpicool
# platform (ref: github.com/klightspeed/BrassMonkeyFridgeMonitor). Verify against your
# specific device with -v 3 output on first run.

import os, sys

venv = os.path.expanduser("~/.venvs/homectld")
if os.path.isdir(venv) and not sys.prefix.startswith(venv):
    python = os.path.join(venv, "bin", "python3")
    if os.path.isfile(python):
        os.execv(python, [python] + sys.argv)

import asyncio
import argparse
import syslog
import signal
import time
import json
import queue
import paho.mqtt.client as paho
from bleak import BleakScanner, BleakClient

syslog.openlog(ident="alpicoolmqtt", logoption=syslog.LOG_PID)

# ── BLE constants ─────────────────────────────────────────────────────────────

BLE_SVC_UUID  = "0000FFE0-0000-1000-8000-00805F9B34FB"
BLE_CHAR_UUID = "0000FFE1-0000-1000-8000-00805F9B34FB"

# Status query frame (sent to FFE1, triggers notification response)
CMD_STATUS = bytes([0xFE, 0xFE, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0xEF, 0xEF])

# ── command builders ──────────────────────────────────────────────────────────

def cmd_power(on: bool) -> bytes:
    return bytes([0xFE, 0xFE, 0x07, 0x06, 0x01 if on else 0x00, 0x00, 0x00, 0x00, 0xEF, 0xEF])

def cmd_temp(celsius: int) -> bytes:
    # two's complement for negative values (freezer range)
    return bytes([0xFE, 0xFE, 0x07, 0x01, celsius & 0xFF, 0x00, 0x00, 0x00, 0xEF, 0xEF])

def cmd_mode(eco: bool) -> bytes:
    return bytes([0xFE, 0xFE, 0x07, 0x02, 0x01 if eco else 0x00, 0x00, 0x00, 0x00, 0xEF, 0xEF])

# ── response parser ───────────────────────────────────────────────────────────

class AlpicoolStatus:
    # Response layout (10 bytes):
    #   [0-1]  FE FE   header
    #   [2]    06      response type / data length
    #   [3]    target temperature (int8, °C)
    #   [4]    actual temperature (int8, °C)
    #   [5]    battery voltage × 10  (e.g. 126 → 12.6 V)
    #   [6]    power state  bit0: 1=on  0=off
    #   [7]    mode flags   bit0: 1=eco  0=max
    #   [8-9]  EF EF   footer
    def __init__(self, data: bytes):
        if len(data) < 8 or data[0] != 0xFE or data[1] != 0xFE:
            raise ValueError(f"Invalid frame: {data.hex()}")
        self.raw          = data
        self.target_temp  = data[3] if data[3] < 128 else data[3] - 256
        self.actual_temp  = data[4] if data[4] < 128 else data[4] - 256
        self.battery_volt = data[5] / 10.0
        self.power        = bool(data[6] & 0x01)
        self.eco_mode     = bool(data[7] & 0x01)

    def __str__(self):
        return (f"Power: {'On' if self.power else 'Off'}  "
                f"Target: {self.target_temp}°C  "
                f"Actual: {self.actual_temp}°C  "
                f"Battery: {self.battery_volt:.1f}V  "
                f"Mode: {'Eco' if self.eco_mode else 'Max'}")

# ── BLE async helpers ─────────────────────────────────────────────────────────

async def ble_query(mac: str, timeout: float = 15.0) -> AlpicoolStatus | None:
    buf = bytearray()
    done = asyncio.Event()

    def on_notify(sender, data):
        buf.extend(data)
        if len(buf) >= 8:
            done.set()

    try:
        async with BleakClient(mac, timeout=timeout) as client:
            await client.start_notify(BLE_CHAR_UUID, on_notify)
            await client.write_gatt_char(BLE_CHAR_UUID, CMD_STATUS, response=False)
            try:
                await asyncio.wait_for(done.wait(), timeout=5.0)
            except asyncio.TimeoutError:
                tell(0, "BLE notify timeout – no response from device")
                return None
            await client.stop_notify(BLE_CHAR_UUID)
            return AlpicoolStatus(bytes(buf))
    except Exception as e:
        tell(0, f"BLE query error: {e}")
        return None


async def ble_send(mac: str, cmd: bytes, timeout: float = 15.0) -> bool:
    try:
        async with BleakClient(mac, timeout=timeout) as client:
            await client.write_gatt_char(BLE_CHAR_UUID, cmd, response=False)
            return True
    except Exception as e:
        tell(0, f"BLE send error: {e}")
        return False


async def ble_discover(timeout: float = 10.0) -> dict:
    found = {}
    results = await BleakScanner.discover(timeout=timeout, return_adv=True)
    for addr, (device, adv) in results.items():
        uuids_upper = [str(u).upper() for u in adv.service_uuids]
        if any("FFE0" in u for u in uuids_upper):
            found[addr] = device.name or "Unknown"
    return found

# ── homectld widget parameters ────────────────────────────────────────────────
# address → parameter JSON appended on initial publish

parameters = [None] * 5
# 0: Power (on/off status)
parameters[0] = '{"parameter": {"cloneable": false, "widgettype": 0, "symbol": "mdi:mdi-power", "symbolOn": "mdi:mdi-power", "colorCondition": "0=red,1=rgb(40 172 45)"}}'
# 1: Target temperature (settable)
parameters[1] = '{"parameter": {"cloneable": false, "widgettype": 6}}'
# 2: Actual temperature (read-only)
parameters[2] = '{"parameter": {"cloneable": false, "widgettype": 6}}'
# 3: Battery voltage
parameters[3] = '{"parameter": {"cloneable": false, "widgettype": 6}}'
# 4: Mode Max/Eco (text with choices)
parameters[4] = '{"parameter": {"cloneable": false, "widgettype": 0, "symbol": "mdi:mdi-snowflake", "symbolOn": "mdi:mdi-leaf", "colorCondition": "Max=rgb(40 172 45),Eco=orange"}}'

initial  = True
cmd_queue = queue.Queue()

# ── argument parsing ──────────────────────────────────────────────────────────

parser = argparse.ArgumentParser('alpicoolmqtt')
parser.add_argument('-i', type=int, nargs='?', help='Interval [seconds] (default 30)', default=30)
parser.add_argument('-m',           nargs='?', help='MQTT host', default="")
parser.add_argument('-p', type=int, nargs='?', help='MQTT port', default=1883)
parser.add_argument('-v', type=int, nargs='?', help='Verbosity level (0-3) (default 0)', default=0)
parser.add_argument('-l', action='store_true', help='Log to syslog (default console)')
parser.add_argument('-T',           nargs='?', help='MQTT topic', default="homectld2mqtt/alpicool")
parser.add_argument('-t',           nargs='?', help='Sensor type string', default="ALPICOOL")
parser.add_argument('-M',           nargs='?', help='Device MAC address', default="")
parser.add_argument('-s', action='store_true', help='Show current status and exit')
parser.add_argument('-D', action='store_true', help='Discover Alpicool/FFE0 devices', default=False)

args = parser.parse_args()

# ── helpers ───────────────────────────────────────────────────────────────────

def tell(level, msg):
    if args.v >= level:
        if args.l:
            syslog.syslog(syslog.LOG_INFO, msg)
        else:
            print(msg)

def mqttConnect():
    try:
        mqtt.reconnect()
    except Exception:
        try:
            mqtt.connect(args.m.strip(), args.p)
        except Exception as e:
            tell(0, f"MQTT connect failed: {e}")

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
        rc = mqtt.publish(args.T.strip(), msg)
        if rc.rc != 0:
            tell(0, f"MQTT publish failed (rc={rc.rc}), reconnecting")
            mqttConnect()
            mqtt.publish(args.T.strip(), msg)
        mqtt.loop(0.1)
    if args.s:
        tell(0, "  {}: {} {}".format(sensor['title'], sensor['value'], sensor.get('unit', '')))

def shutdown(sig, frame):
    if args.m.strip() != '':
        mqtt.disconnect()
    sys.exit(0)

signal.signal(signal.SIGTERM, shutdown)
signal.signal(signal.SIGINT, shutdown)

# ── MQTT callbacks ────────────────────────────────────────────────────────────

inTopic = ''

def onConnect(client, userdata, flags, reason_code, properties=None):
    tell(0, f"Connected MQTT with result code {reason_code}")
    tell(0, 'Subscribe topic {}'.format(inTopic))
    client.subscribe(inTopic)
    init = {
        'type'  : args.t.strip(),
        'action': 'init',
        'topic' : inTopic,
    }
    mqtt.publish(args.T.strip(), json.dumps(init))

def onMessage(client, userdata, msg):
    tell(0, "Received {}".format(msg.payload.decode()))
    try:
        s = json.loads(msg.payload.decode())
        cmd_queue.put(s)
    except Exception as e:
        tell(0, f"onMessage parse error: {e}")

def processCommand(s):
    address = int(s['address'])
    value   = s['value']
    mac     = args.M.strip()

    if address == 0:    # power on/off
        on = bool(int(value))
        tell(0, f"Set power {'On' if on else 'Off'}")
        asyncio.run(ble_send(mac, cmd_power(on)))
    elif address == 1:  # target temperature
        temp = int(value)
        tell(0, f"Set target temp {temp}°C")
        asyncio.run(ble_send(mac, cmd_temp(temp)))
    elif address == 4:  # mode Max/Eco
        eco = str(value).lower() in ('eco', '1', 'true')
        tell(0, f"Set mode {'Eco' if eco else 'Max'}")
        asyncio.run(ble_send(mac, cmd_mode(eco)))
    else:
        tell(0, f"Ignoring unknown address {address}")

# ── discover ──────────────────────────────────────────────────────────────────

if args.D:
    print("Discovering Alpicool / MANTUM / FFE0 devices ...")
    found = asyncio.run(ble_discover(10))
    print(f"\nFinished. Found {len(found)} device(s) with FFE0 service:")
    for addr, name in found.items():
        print(f"  {addr}  {name}")
    sys.exit(0)

# ── show / main loop guard ────────────────────────────────────────────────────

if args.M.strip() == "":
    print("Missing device MAC, add -M option")
    sys.exit(1)

if args.s:
    status = asyncio.run(ble_query(args.M.strip()))
    if status is None:
        print(f'Device "{args.M.strip()}" not found or not responding')
    else:
        print(f"Raw frame:   {status.raw.hex()}")
        print(f"Power:       {'On' if status.power else 'Off'}")
        print(f"Target temp: {status.target_temp} °C")
        print(f"Actual temp: {status.actual_temp} °C")
        print(f"Battery:     {status.battery_volt:.1f} V")
        print(f"Mode:        {'Eco' if status.eco_mode else 'Max'}")
    sys.exit(0)

# ── MQTT connect ──────────────────────────────────────────────────────────────

if args.m.strip() != '':
    tell(0, 'Connecting to "{}:{}", topic "{}"'.format(args.m.strip(), args.p, args.T.strip()))
    mqtt = paho.Client("alpicool", protocol=paho.MQTTv311, clean_session=True)
    inTopic = args.T.strip() + '/in'
    mqtt.on_connect = onConnect
    mqtt.on_message = onMessage
    mqtt.connect(args.m.strip(), args.p, 60)
    mqtt.loop_start()

# ── main loop ─────────────────────────────────────────────────────────────────

temp_choices = ','.join(str(t) for t in range(-20, 11))

while True:
    # drain pending control commands before reading status
    while not cmd_queue.empty():
        try:
            processCommand(cmd_queue.get_nowait())
        except Exception as e:
            tell(0, f"Command error: {e}")

    tell(0, "Update ...")
    status = asyncio.run(ble_query(args.M.strip()))

    if status is None:
        tell(0, f'Device "{args.M.strip()}" not found or not responding')
    else:
        tell(0, str(status))
        tell(3, f"Raw frame: {status.raw.hex()}")

        stype = args.t.strip()
        publishMqtt({'type': stype, 'address': 0, 'value': int(status.power),
                     'kind': 'status', 'title': 'Power'})
        publishMqtt({'type': stype, 'address': 1, 'value': status.target_temp,
                     'kind': 'value', 'unit': '°C', 'title': 'Target Temp',
                     'choices': temp_choices})
        publishMqtt({'type': stype, 'address': 2, 'value': status.actual_temp,
                     'kind': 'value', 'unit': '°C', 'title': 'Actual Temp'})
        publishMqtt({'type': stype, 'address': 3, 'value': status.battery_volt,
                     'kind': 'value', 'unit': 'V', 'title': 'Battery'})
        publishMqtt({'type': stype, 'address': 4, 'value': 'Eco' if status.eco_mode else 'Max',
                     'kind': 'text', 'title': 'Mode', 'choices': 'Max,Eco'})

    tell(0, "... done")
    time.sleep(args.i)
    initial = False
