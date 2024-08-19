#!/usr/bin/env python3

from mopeka_pro_check.service import MopekaService, MopekaSensor, GetServiceInstance
from time import sleep
import argparse
import sys
import syslog
import time
import json
import paho.mqtt.client as paho

syslog.openlog(ident="mopekamqtt",logoption=syslog.LOG_PID)

initial = True

# default widget settings

parameters = []
parameters.append('{"parameter": {"cloneable": false, "widgettype": 9, "symbol": "mdi:mdi-battery-high", "symbolOn": "mdi:mdi-battery-high", "colorCondition": "20>red,50>orange,51<rgb(40 172 45)"}}')
parameters.append('{"parameter": {"cloneable": false, "widgettype": 14, "scalemax": 11, "color": "rgb(40, 172, 45)", "colorOn": "rgb(235, 197, 5)", "barwidth": 120, "showvalue": true }}')
parameters.append('{"parameter": {"cloneable": false, "widgettype": 6}}')
parameters.append('{"parameter": {"cloneable": false, "widgettype": 14, "scalemax": 11, "color": "rgb(40, 172, 45)", "colorOn": "rgb(235, 197, 5)", "barwidth": 120, "showvalue": true }}')
parameters.append('{"parameter": {"cloneable": false, "widgettype": 0, "symbol": "mdi:mdi-bluetooth", "symbolOn": "mdi:mdi-bluetooth", "colorCondition": "1=red,2=orange,3=rgb(40 172 45)"}}')

# functions

def tell(level, msg):
	if args.v >= level:
		if args.l:
			syslog.syslog(syslog.LOG_INFO, msg)
		else:
			print(msg)

def publishMqtt(sensor):
	if initial and parameters[sensor['address']] != '':
		tell(0, "appending {}".format(parameters[sensor['address']]))
		p = json.loads(parameters[sensor['address']]);
		sensor.update(p)
	if args.m.strip() != '':
		msg = json.dumps(sensor)
		tell(2, '{}'.format(msg))
		ret = mqtt.publish(args.T.strip(), msg)
		mqtt.loop(0.1)
		# print(ret)

# arguments

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
parser.add_argument('-K', type=int, nargs='?', help='Tank amount [kg]', default=0)

args = parser.parse_args()

# service

service = GetServiceInstance()
service.SetHostControllerIndex(0)

if args.m.strip() != '':
	tell(0, 'Connecting to "{}:{}", topic "{}"'.format(args.m.strip(), args.p, args.T.strip()))
	mqtt = paho.Client("mopeka")
	mqtt.connect(args.m.strip(), args.p)

def show(adv):
	print("RSSI %s dBm" % adv.rssi)
	print("Button Pressed %s" % adv.SyncButtonPressed)
	print("Battery %s V" % adv.BatteryVoltage)
	print("Battery %s %%" % round(adv.BatteryPercent))
	print("ReadingQualityStars %s " % round(adv.ReadingQualityStars))
	print("Temperature %s °C" % adv.TemperatureInCelsius)
	print("TankLevel %s mm" % adv.TankLevelInMM)
	# print("TankLevelRaw %s" % adv.TankLevelRaw)
	if args.F != 0:
		percent = round(adv.TankLevelInMM / (args.F / 100))
		print("TankLevel %s %%" % percent)
		if args.K != 0:
			kg = percent * (args.K / 100)
			print("TankAmount %s kg" % kg)
	adv.Dump()

if args.D:
	print("Discover ...")
	service.DoSensorDiscovery()
	service.Start()
	sleep(5)
	service.Stop()

	print(f"\nFinished Discovery. Found {len(service.SensorDiscoveredList)} sensors")
	print("Stats %s" % str(service.ServiceStats))

	for s in service.SensorDiscoveredList.values():
		s.Dump()

	sys.exit()

if args.M.strip() == "":
	print("Missing MAC add -M option")
	sys.exit()

service.AddSensorToMonitor(MopekaSensor(args.M.strip()))

if args.s:
	service.Start()
	sleep(5)
	service.Stop()
	for s in service.SensorMonitoredList.values():
		if s._last_packet is not None:
			show(s._last_packet)
		else:
			tell(0, 'Sensor "{}" not found'.format(args.M.strip()))
	sys.exit()

while True:
	service.Start()
	sleep(5)
	service.Stop()

	tell(0, "Update ...")

	for s in service.SensorMonitoredList.values():
		sensorType = "MOPEKA0"  # the number (0) shoud be set dynamicly on the position of the MAC in the list once we allow mor than one Sensor/MAC
		adv = s._last_packet
		percent = -1

		if adv is None:
			tell(0, 'Sensor "{}" not found'.format(args.M.strip()))
			continue

		if args.F != 0:
			percent = round(adv.TankLevelInMM / (args.F / 100))
		if args.K != 0:
			kg = percent * (args.K / 100)
		publishMqtt({
		  'type'    : sensorType,
		  'address' : 0,
		  'value'   : percent,
		  'kind'    : 'value',
		  'unit'    : '%',
		  'battery' : round(adv.BatteryPercent),
		  'title'   : 'Battery' })
		publishMqtt({
		  'type'    : sensorType,
		  'address' : 1,
		  'value'   : adv.TankLevelInMM,
		  'kind'    : 'value',
		  'unit'    : 'mm',
		  'title'   : 'Tank Level' })
		publishMqtt({
		  'type'    : sensorType,
		  'address' : 2,
		  'value'   : adv.TemperatureInCelsius,
		  'kind'    : 'value',
		  'unit'    : 'C',
		  'title'   : 'Tank Temperature' })
		publishMqtt({
		  'type'    : sensorType,
		  'address' : 3,
		  'value'   : kg,
		  'kind'    : 'value',
		  'unit'    : 'kg',
		  'title'   : 'Tank Amount' })
		publishMqtt({
		  'type'    : sensorType,
		  'address' : 4,
		  'value'   : round(adv.ReadingQualityStars),
		  'kind'    : 'value',
		  'unit'    : '',
		  'title'   : 'Signal Quality' })
	tell(0, "... done")
	time.sleep(args.l)
	initial = False

if args.m.strip() != '':
	mqtt.disconnect()
