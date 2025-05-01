#!/usr/bin/env python3

# Retrieve status from thetford N4000/T2000 series refrigerator

import sys
import syslog
import time
import argparse
import json
import paho.mqtt.client as paho

from usblini import USBlini

syslog.openlog(ident="thetfordmqtt",logoption=syslog.LOG_PID)

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

unitsN = {
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

unitsT = {
	0  : "",
	1  : "",
	2  : "",
	3  : "",
	4  : "",
	5  : "V",
	6  : "",
	7  : "",
	10 : ""
}

def toSensorTitle(byte):
	return titles[byte];

def toSensorUnit(byte):
	if args.M.strip() == "N4000":
		return unitsN[byte];
	return unitsT[byte];

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
	if code == 23:
		return "Störung 23"

	tell(0, '{0:} {1:}'.format("Unexpected status code", code))

	return "Störung?"

def isAuto(val):
	if val & 0x08:
		return True

	return False

def toModeString(mode):
	mode = mode & ~0x08     # mask additional auto bit, we check it by isAuto
	if mode == 0:
		return "Aus"
	elif mode == 1:
		return "An"
	elif mode == 2:
		return "Aus"
	elif mode == 3:
		return "Gas"
	elif mode == 4:
		return "4 Störung Batt?"
	elif mode == 5:
		return "Batterie"
	elif mode == 6:
		return "6 Störung?"
	elif mode == 7:
		return "Netz ~230V"
	elif mode == 9:
		return "Auto Nacht"
	elif mode == 11:
		return "Gas Nacht"
	elif mode == 13:
		return "Batt Nacht"
	elif mode == 15:
		return "Netz Nacht"
	return str(mode)

def open():
	while True:
		try:
			ulini.open()
			ulini.set_baudrate(19200)
			print("Open succeeded")
			break;     # open succeeded
		except:
			print("Open failed, USBlini device with id '04d8:e870' not found, aborting, check connection and cable")
			print("Retrying in 5 seconds ..")
			time.sleep(5.0)

def publishMqtt(sensor):
	if args.m.strip() != '':
		msg = json.dumps(sensor)
		tell(2, '{}'.format(msg))
		ret = mqtt.publish(args.T.strip(), msg)

def frame_listener(frame):

	if frame.frameid != 0x0c:
		return

	global received
	received += 1

	tell(0, 'Updating ...')
	tell(2, '-----------------------')

	for byte in range(8):
		tell(1, 'Byte {0:}: 0b{1:08b} 0x{1:02x} ({1:})'.format(byte, byte2uint(frame.data[byte])))

		# build JSON for topic n4000:
		#  {"type": "N4000", "address": 3, "value": 0, "title": "Status", "text": "Online"}

		sensor = {
			'type'    : args.t.strip(),
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
			sensor['text'] = toModeString(sensor['value'])
			tell(3, '{0:} / {1:}'.format(toModeString(sensor['value']), "Automatik" if isAuto(sensor['value']) else "Manuell"))
		elif byte == 1:
			if args.M.strip() == "N4000":
				sensor['value'] += 1
			tell(3, '{}: {} {}'.format(toSensorTitle(byte), sensor['value'], toSensorUnit(byte)))
		elif byte == 2:
			sensor['kind'] = 'status'
			sensor['value'] = sensor['value'] & 0x40 > 0
			tell(3, '{}: {} {}'.format(toSensorTitle(byte), sensor['value'], toSensorUnit(byte)))
		elif byte == 3:
			sensor['kind'] = 'text'
			sensor['text'] = toError(sensor['value'])
			tell(3, '{0:} {1:}'.format("Status", sensor['value']))
		elif byte == 4:
			tell(3, '{}: {} {}'.format(toSensorTitle(byte), sensor['value'], toSensorUnit(byte)))
		elif byte == 5:
			sensor['value'] /= 10
			tell(3, '{}: {} {}'.format(toSensorTitle(byte), sensor['value'], toSensorUnit(byte)))

		if byte in [0,1,2,3,4,5]:
			publishMqtt(sensor)

		if args.M.strip() == "N4000":
			if byte == 0:
				sensor = {
					'type'    : args.t.strip(),
					'address' : 10,
					'value'   : byte2uint(frame.data[byte]) & 0x08,
					'title'   : toSensorTitle(10),
					'kind'    : 'text',
					'text'    : "Automatik" if isAuto(byte2uint(frame.data[byte])) else "Manuell"
				}
				publishMqtt(sensor)

	tell(0, "... done")

# --------------------------------------------

# arguments

parser = argparse.ArgumentParser('thetford')
parser.add_argument('-i', type=int, nargs='?', help='interval [seconds] (default 5)', default=5)
parser.add_argument('-m',           nargs='?', help='MQTT host', default="")
parser.add_argument('-p', type=int, nargs='?', help='MQTT port', default=1883)
parser.add_argument('-v', type=int, nargs='?', help='Verbosity Level (0-3) (default 1)', default=1)
parser.add_argument('-c', type=int, nargs='?', help='Sample Count', default=0)
parser.add_argument('-l', action='store_true', help='Log to Syslog (default console)')
parser.add_argument('-T',           nargs='?', help='MQTT topic', default="n4000")
parser.add_argument('-t',           nargs='?', help='Sensor type', default="N4000")
parser.add_argument('-M',           nargs='?', help='Fridge model', default="N4000")

args = parser.parse_args()

# open mqtt connection

if args.m.strip() != '':
	tell(0, 'Connecting to "{}:{}", topic "{}"'.format(args.m.strip(), args.p, args.T.strip()))
	mqtt = paho.Client("thetford")
	mqtt.connect(args.m.strip(), args.p)

# init usblini

ulini = USBlini()
open()

# send one frame to wakeup devices

ulini.master_write(0x00, USBlini.CHECKSUM_MODE_NONE, [])
time.sleep(0.2)

# add listener and set master sequence

ulini.frame_listener_add(frame_listener)
ulini.master_set_sequence(args.i * 1000, 200, [0x0c])

while True:
	time.sleep(1.0)
	abort = args.c != 0 and received >= args.c
	if abort:
		break
	pass

ulini.close()

if args.m.strip() != '':
	mqtt.disconnect()
