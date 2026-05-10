#!/usr/bin/env python3

import sys
import asyncio
import argparse
import syslog
import signal
import time
import json
import paho.mqtt.client as paho
from bleak import BleakScanner

syslog.openlog(ident="mopekamqtt", logoption=syslog.LOG_PID)

MOPEKA_MANUFACTURER_ID  = 0x0059
MOPEKA_HARDWARE_PROPANE = 0x03
MOPEKA_TANK_LEVEL_COEFF = (0.573045, -0.002822, -0.00000535)

initial = True

parameters = []
parameters.append('{"parameter": {"cloneable": false, "widgettype": 9, "symbol": "mdi:mdi-battery-high", "symbolOn": "mdi:mdi-battery-high", "colorCondition": "20>red,50>orange,51<rgb(40 172 45)"}}')
parameters.append('{"parameter": {"cloneable": false, "widgettype": 14, "scalemax": 11, "color": "rgb(40, 172, 45)", "colorOn": "rgb(235, 197, 5)", "barwidth": 120, "showvalue": true }}')
parameters.append('{"parameter": {"cloneable": false, "widgettype": 6}}')
parameters.append('{"parameter": {"cloneable": false, "widgettype": 14, "scalemax": 11, "color": "rgb(40, 172, 45)", "colorOn": "rgb(235, 197, 5)", "barwidth": 120, "showvalue": true }}')
parameters.append('{"parameter": {"cloneable": false, "widgettype": 0, "symbol": "mdi:mdi-bluetooth", "symbolOn": "mdi:mdi-bluetooth", "colorCondition": "1=red,2=orange,3=rgb(40 172 45)"}}')


class MopekaReading:
	def __init__(self, mfg: bytes, rssi: int):
		self.rssi = rssi
		if mfg[0] != MOPEKA_HARDWARE_PROPANE:
			raise ValueError(f"Unsupported hardware ID: 0x{mfg[0]:02X}")
		self._raw_battery        = mfg[1] & 0x7F
		self.SyncButtonPressed   = bool(mfg[2] & 0x80)
		self._raw_temp           = mfg[2] & 0x7F
		self._raw_level          = ((int(mfg[4]) << 8) + mfg[3]) & 0x3FFF
		self.ReadingQualityStars = mfg[4] >> 6

	@property
	def BatteryVoltage(self):
		return self._raw_battery / 32.0

	@property
	def BatteryPercent(self):
		pct = ((self.BatteryVoltage - 2.2) / 0.65) * 100
		return round(max(0.0, min(100.0, pct)), 1)

	@property
	def TemperatureInCelsius(self):
		return self._raw_temp - 40

	@property
	def TankLevelInMM(self):
		c = MOPEKA_TANK_LEVEL_COEFF
		return int(self._raw_level * (c[0] + c[1] * self._raw_temp + c[2] * self._raw_temp * self._raw_temp))

	def __str__(self):
		return (f"RSSI: {self.rssi}dBm  "
		        f"Battery: {self.BatteryVoltage:.2f}V {self.BatteryPercent}%  "
		        f"Button: {self.SyncButtonPressed}  "
		        f"Temp: {self.TemperatureInCelsius}°C  "
		        f"Quality: {self.ReadingQualityStars}★  "
		        f"Level: {self.TankLevelInMM}mm")


def _parse(mfg: bytes, rssi: int):
	try:
		return MopekaReading(mfg, rssi)
	except (ValueError, IndexError):
		return None


async def scan_one(mac: str, timeout: float = 10.0):
	mac_upper = mac.upper()
	result    = None

	def cb(device, adv):
		nonlocal result
		if device.address.upper() != mac_upper:
			return
		mfg = adv.manufacturer_data.get(MOPEKA_MANUFACTURER_ID)
		if mfg:
			result = _parse(mfg, getattr(adv, 'rssi', None) or 0)

	async with BleakScanner(cb):
		await asyncio.sleep(timeout)
	return result


async def scan_discover(timeout: float = 10.0):
	found = {}

	def cb(device, adv):
		mfg = adv.manufacturer_data.get(MOPEKA_MANUFACTURER_ID)
		if mfg:
			r = _parse(mfg, adv.rssi or 0)
			if r:
				found[device.address] = r

	async with BleakScanner(cb):
		await asyncio.sleep(timeout)
	return found


# ── arguments ────────────────────────────────────────────────────────────────

parser = argparse.ArgumentParser('mopekamqtt')
parser.add_argument('-i', type=int, nargs='?', help='interval [seconds] (default 5)', default=5)
parser.add_argument('-m',           nargs='?', help='MQTT host', default="")
parser.add_argument('-p', type=int, nargs='?', help='MQTT port', default=1883)
parser.add_argument('-v', type=int, nargs='?', help='Verbosity level (0-3) (default 0)', default=0)
parser.add_argument('-l', action='store_true', help='Log to syslog (default console)')
parser.add_argument('-T',           nargs='?', help='MQTT topic', default=" homectld2mqtt/mopeka")
parser.add_argument('-s', action='store_true', help='Show', default=False)
parser.add_argument('-M',           nargs='?', help='Sensor MAC', default="")
parser.add_argument('-D', action='store_true', help='Discover sensors', default=False)
parser.add_argument('-F', type=int, nargs='?', help='Tank level full [mm]', default=365)
parser.add_argument('-K', type=int, nargs='?', help='Tank amount [kg]', default=11)

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
	if initial and parameters[sensor['address']] != '':
		tell(0, "appending {}".format(parameters[sensor['address']]))
		p = json.loads(parameters[sensor['address']])
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

def shutdown(sig, frame):
	if args.m.strip() != '':
		mqtt.disconnect()
	sys.exit(0)

signal.signal(signal.SIGTERM, shutdown)
signal.signal(signal.SIGINT, shutdown)

if args.m.strip() != '':
	tell(0, 'Connecting to "{}:{}", topic "{}"'.format(args.m.strip(), args.p, args.T.strip()))
	mqtt = paho.Client("mopeka")
	mqtt.connect(args.m.strip(), args.p)

# ── discover ──────────────────────────────────────────────────────────────────

if args.D:
	print("Discover ...")
	found = asyncio.run(scan_discover(10))
	print(f"\nFinished Discovery. Found {len(found)} sensors")
	for addr, r in found.items():
		print(f"  {addr}: {r}")
	sys.exit()

# ── monitor / show ────────────────────────────────────────────────────────────

if args.M.strip() == "":
	print("Missing MAC add -M option")
	sys.exit()

if args.s:
	adv = asyncio.run(scan_one(args.M.strip(), 10))
	if adv is None:
		tell(0, 'Sensor "{}" not found'.format(args.M.strip()))
	else:
		print(f"RSSI {adv.rssi} dBm")
		print(f"Button Pressed {adv.SyncButtonPressed}")
		print(f"Battery {adv.BatteryVoltage:.2f} V")
		print(f"Battery {round(adv.BatteryPercent)} %")
		print(f"ReadingQualityStars {adv.ReadingQualityStars}")
		print(f"Temperature {adv.TemperatureInCelsius} °C")
		print(f"TankLevel {adv.TankLevelInMM} mm")
		if args.F != 0:
			percent = round(adv.TankLevelInMM / (args.F / 100))
			print(f"TankLevel {percent} %")
			if args.K != 0:
				print(f"TankAmount {percent * (args.K / 100):.1f} kg")
	sys.exit()

while True:
	adv = asyncio.run(scan_one(args.M.strip(), 20))
	if args.m.strip() != '':
		mqtt.loop(0.1)
	tell(0, "Update ...")

	if adv is None:
		tell(0, 'Sensor "{}" not found'.format(args.M.strip()))
	else:
		tell(0, 'Sensor found: {}mm {}% {}°C Q{}'.format(
		    adv.TankLevelInMM, round(adv.BatteryPercent),
		    adv.TemperatureInCelsius, adv.ReadingQualityStars))
		sensorType = "MOPEKA0"
		percent    = round(adv.TankLevelInMM / (args.F / 100)) if args.F else -1
		kg         = percent * (args.K / 100) if (args.K and percent >= 0) else -1

		publishMqtt({'type': sensorType, 'address': 0, 'value': round(adv.BatteryPercent),
		             'kind': 'value', 'unit': '%', 'title': 'Battery'})
		publishMqtt({'type': sensorType, 'address': 1, 'value': adv.TankLevelInMM,
		             'kind': 'value', 'unit': 'mm', 'title': 'Tank Level'})
		publishMqtt({'type': sensorType, 'address': 2, 'value': adv.TemperatureInCelsius,
		             'kind': 'value', 'unit': 'C', 'title': 'Tank Temperature'})
		publishMqtt({'type': sensorType, 'address': 3, 'value': kg,
		             'kind': 'value', 'unit': 'kg', 'title': 'Tank Amount'})
		publishMqtt({'type': sensorType, 'address': 4, 'value': adv.ReadingQualityStars,
		             'kind': 'value', 'unit': '', 'title': 'Signal Quality'})

	tell(0, "... done")
	time.sleep(args.i)
	initial = False
