#!/usr/bin/env python3

# Retrieve status from thetford N4000 series refrigerator

import sys
import syslog
import time
import argparse
import json
import paho.mqtt.client as paho

from usblini import USBlini

def tell(level, msg):
	if args.v >= level:
		if args.l:
			syslog.syslog(syslog.LOG_INFO, msg)
		else:
			print(msg)

received = 0

titles = {
	0  : "Supply",
	1  : "Level",
	2  : "D+",
	3  : "Status",
	4  : "ExtSupply",
	5  : "IntSupply",
	6  : "",
	7  : "",
	10 : "Mode"
}

units = {
	0  : "",
	1  : "",
	2  : "",
	3  : "",
	4  : "V",
	5  : "V",
	6  : "",
	7  : "",
	10 : ""
}

def toSensorTitle(byte):
	return titles[byte];

def toSensorUnit(byte):
	return units[byte];

def byte2uint(val):
	if val < 0:
		return -(256-val)
	else:
		return val

def toError(code):
	if code == 0:
		return "Online"
	if code == 3:
		return "Fehler Gas"
	if code == 4:
		return "Fehler 12V Heizstab"
	if code == 6:
		return "Fehler 12V"
	if code == 7:
		return "Fehler D+"
	if code == 8:
		return "Fehler 230V Heizstab"
	if code == 9:
		return "Fehler Steuerung"
	if code == 10:
		return "Fehler 230V"
	if code == 11:
		return "Keine Energiequelle"
	if code == 13:
		return "Fehler Temp Fühler"
	return "Störung"

def isAuto(val):
	if val & 0x08:
		return True

	return False

def toModeString(mode):
	if mode == 2:
		return "Aus"
	elif mode == 3:
		return "Gas"
	elif mode == 5:
		return "Batterie"
	elif mode == 7:
		return "Netz ~230V"
	return "unknown"

def publishMqtt(sensor):
	if args.m != '':
		msg = json.dumps(sensor)
		tell(1, '{}'.format(msg))
		ret = mqtt.publish("n4000", msg)

def frame_listener(frame):

	if frame.frameid != 0x0c:
		return

	global received
	received += 1

	tell(0, 'Updating ...')
	tell(1, '-----------------------')

	for byte in range(8):
		tell(2, 'Byte {0:}: b{1:08b} 0x{1:02x} ({1:})'.format(byte, byte2uint(frame.data[byte])))

		# build JSON for topic n4000:
		#  {"type": "N4000", "address": 3, "value": 0, "title": "Status", "text": "Online"}

		sensor = {
			'type'    : 'N4000',
			'address' : byte,
			'value'   : byte2uint(frame.data[byte]),
			'kind'    : 'value',
			'title'   : toSensorTitle(byte)
		}

		unit = toSensorUnit(byte)
		if unit != "":
			sensor['unit'] = unit

		if byte == 0:
			sensor['kind'] = 'text'
			sensor['value'] = sensor['value'] & 0x07
			sensor['text'] = toModeString(sensor['value'])
			tell(1, '{0:} / {1:}'.format(toModeString(sensor['value']), "Automatik" if isAuto(sensor['value']) else "Manuell"))
		elif byte == 1:
			sensor['value'] += 1
			tell(1, '{}: {} {}'.format(toSensorTitle(byte), sensor['value'], toSensorUnit(byte)))
		elif byte == 2:
			sensor['kind'] = 'status'
			sensor['value'] = sensor['value'] & 0x40 > 0
			tell(1, '{}: {} {}'.format(toSensorTitle(byte), sensor['value'], toSensorUnit(byte)))
		elif byte == 3:
			sensor['kind'] = 'text'
			sensor['text'] = toError(sensor['value'])
			tell(1, '{0:} {1:}'.format("In Betrieb" if sensor['value'] == 0 else "Störung", "" if sensor['value'] == 0 else sensor['value']))
		elif byte == 4:
			tell(1, '{}: {} {}'.format(toSensorTitle(byte), sensor['value'], toSensorUnit(byte)))
		elif byte == 5:
			sensor['value'] /= 10
			tell(1, '{}: {} {}'.format(toSensorTitle(byte), sensor['value'], toSensorUnit(byte)))

		if byte in [0,1,2,3,4,5]:
			publishMqtt(sensor)

		if byte == 0:
			sensor = {
				'type'    : "N4000",
				'address' : 10,
				'value'   : byte2uint(frame.data[byte]) & 0x08,
				'title'   : toSensorTitle(10),
				'kind'    : 'text',
				'text'    : "Automatik" if isAuto(byte2uint(frame.data[byte])) else "Manuell"
			}
			publishMqtt(sensor)

# --------------------------------------------

# arguments

parser = argparse.ArgumentParser('thetford')
parser.add_argument('-i', type=int, nargs='?', help='interval [seconds] (default 5)', default=5)
parser.add_argument('-m', nargs='?', help='MQTT host', default="")
parser.add_argument('-p', type=int, nargs='?', help='MQTT port', default=1883)
parser.add_argument('-v', type=int, nargs='?', help='Verbosity Level (0-3) (default 0)', default=0)
parser.add_argument('-c', type=int, nargs='?', help='Sample Count', default=0)
parser.add_argument('-l', action='store_true', help='Log to Syslog (default console)')
args = parser.parse_args()

# open mqtt connection

if args.m != '':
	tell(0, 'Connecting to "{}" "{}"'.format(args.m.strip(), args.p))
	mqtt = paho.Client("n4000")
	mqtt.connect(args.m.strip(), args.p)

# init usblini

ulini = USBlini()
ulini.open()
ulini.set_baudrate(19200)

# send one frame to wakeup devices

ulini.master_write(0x00, USBlini.CHECKSUM_MODE_NONE, [])
time.sleep(0.2)

# add listener and set master sequence

ulini.frame_listener_add(frame_listener)
ulini.master_set_sequence(args.i * 1000, 200, [0x0c])

while True:
	time.sleep(0.2)
	abort = args.c != 0 and received >= args.c
	if abort:
		break
	pass

ulini.close()

if args.m:
	mqtt.disconnect()
