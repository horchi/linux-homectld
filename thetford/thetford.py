#!/usr/bin/env python3

# Retrieve status from thetford N4000/T2000 series refrigerator

import sys
import syslog
import time
import argparse
import json
import paho.mqtt.client as paho

from collections import namedtuple
from usblini import USBlini

syslog.openlog(ident="thetfordmqtt",logoption=syslog.LOG_PID)

def tell(level, msg):
	if args.v >= level:
		if args.l:
			syslog.syslog(syslog.LOG_INFO, msg)
		else:
			print(msg)

inTopic = ''
received = 0
lastData = []

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


# flt is a colon separated list of models supporting a switch to this mode ('-' for none)

Mode = namedtuple('Mode', ['title', 'mode', 'flt'])

modeMapping = [
	Mode('Aus',           0x00, 'T2090'),
	Mode('Aus',           0x02, 'N4000'),
	Mode('Auto',          0x01, 'N4000'),
	Mode('Tag',           0x01, 'T2090'),
	Mode('Gas',           0x03, 'N4000'),
	Mode('Störung Batt?', 0x04, '-'),
	Mode('Batterie',      0x05, 'N4000'),
	Mode('Störung',       0x06, '-'),
	Mode('Netz ~230V',    0x07, 'N4000'),
	Mode('Aus',           0x08, '-'),
	Mode('Nacht',         0x09, 'T2090'),
	Mode('Gas Nacht',     0x0b, '-'),
	Mode('Batt Nacht',    0x0d, '-'),
	Mode('~230V Nacht',   0x0f, '-')
]

def toModeCode(title):
	for i in range(len(modeMapping)):
		if modeMapping[i].title == title and (modeMapping[i].flt.find(args.M.strip()) != -1 or modeMapping[i].flt == 'all'):
			return modeMapping[i].mode

def toModeString(mode):
	if args.M.strip() != 'T2090':
		mode = mode & ~0x08     # mask additional auto bit, we check it by isAuto
	elif args.M.strip() == 'T2090' and mode == 0x08:
		mode = 0x00

	for i in range(len(modeMapping)):
		if modeMapping[i].mode == mode and (modeMapping[i].flt.find(args.M.strip()) != -1 or modeMapping[i].flt == 'all'):
			return modeMapping[i].title

def openLini():
	while True:
		try:
			ulini.open()
			ulini.set_baudrate(19200)
			print("Open usblini succeeded")
			break;     # open succeeded
		except:
			print("Open failed, USBlini device with id '04d8:e870' not found, aborting, check connection and cable")
			print("Retrying in 5 seconds ..")
			time.sleep(5.0)

def writeLini(byte, value):
	tell(0, "write {} to byte {}".format(value, byte))
	tell(2, "lastData was {}".format(lastData))
	lastData[byte] = value
	tell(2, "Writing {}".format(lastData))
	try:
		response = ulini.master_write(0x0b, USBlini.CHECKSUM_MODE_LIN2, lastData)
	except:
		tell(0, "write failed");
	tell(2, "Wrote {}".format(lastData))

def publishMqtt(sensor):
	if args.m.strip() != '':
		msg = json.dumps(sensor)
		tell(2, '{}'.format(msg))
		ret = mqtt.publish(args.T.strip(), msg)
	if args.s:
		tell(0, "  {}: {} '{}'".format(sensor["title"], sensor["value"], "" if sensor['kind'] != 'text' else sensor["text"]))

def onConnect(client, userdata, flags, reason_code, properties=None):
	tell(0, f"Connected MQTT with result code {reason_code}")
	global inTopic
	tell(0, 'Subscribe topic {}'.format(inTopic))
	client.subscribe(inTopic)
	init = {
		'type'    : args.t.strip(),
		'action'  : 'init',
		'topic'   : inTopic,
	}
	msg = json.dumps(init)
	tell(2, '{}'.format(msg))
	ret = mqtt.publish(args.T.strip(), msg)

def onMessage(client, userdata, msg):
	tell(0, "Received {}".format(msg.payload.decode()))
	s = json.loads(msg.payload.decode())
	byte = s['address']
	value = s['value']
	if byte == 11:    # mode
		byte = 0
		value = toModeCode(s['value'])
		tell(0, "write {} ({}) to byte {}".format(toModeString(value), value, byte))
	elif byte == 1 and args.M.strip() == "N4000":   # cooling level
		value = int(value) - 1
	writeLini(int(byte), int(value))

def frame_listener(frame):

	if frame.frameid != 0x0c:
		return

	global lastData
	global received

	tell(0, 'Updating ...')
	tell(2, '-----------------------')

	received += 1
	lastData = frame.data

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
			sensor['choices'] = '1,2,3,4,5'
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
		if byte == 0:
			choices = ''
			for i in range(len(modeMapping)):
				if modeMapping[i].flt == 'all' or modeMapping[i].flt.find(args.M.strip()) != -1:
					choices += modeMapping[i].title + ','
			sensor = {
				'type'    : args.t.strip(),
				'address' : 11,
				'title'   : 'Mode Setting',
				'kind'    : 'text',
				'text'    : toModeString(byte2uint(frame.data[byte])),
				'value'   : frame.data[byte],
				'choices' : choices
			}
			publishMqtt(sensor)

	tell(0, "... done")

# --------------------------------------------

# arguments

parser = argparse.ArgumentParser('thetford')
parser.add_argument('-i', type=int, nargs='?', help='interval [seconds] (default 5)', default=5)
parser.add_argument('-m',           nargs='?', help='MQTT host', default="")
parser.add_argument('-p', type=int, nargs='?', help='MQTT port', default=1883)
parser.add_argument('-v', type=int, nargs='?', help='Verbosity Level (0-3) (default 1)', default=0)
parser.add_argument('-c', type=int, nargs='?', help='Sample Count', default=0)
parser.add_argument('-l', action='store_true', help='Log to Syslog (default console)')
parser.add_argument('-T',           nargs='?', help='MQTT topic', default="n4000")
parser.add_argument('-t',           nargs='?', help='Sensor type', default="N4000")
parser.add_argument('-M',           nargs='?', help='Fridge model', default="N4000")
parser.add_argument('-s', action='store_true', help='Show values and exit')
parser.add_argument('-w',           nargs='?', help='Write value to byte, format <byte>:<value>')
parser.add_argument('-V', type=int, nargs='?', help='Value for Mode Setting')

args = parser.parse_args()

# open mqtt connection

if args.m.strip() != '':
	tell(0, 'Connecting to "{}:{}", write to topic "{}"'.format(args.m.strip(), args.p, args.T.strip()))
	mqtt = paho.Client("thetford", protocol=paho.MQTTv311, clean_session=True)
	inTopic = args.T.strip() + '/in'
	mqtt.on_connect = onConnect
	mqtt.on_message = onMessage
	mqtt.connect(args.m.strip(), args.p, 60)

if args.s:
	args.c = 1

# init usblini

ulini = USBlini()
openLini()

# send one frame to wakeup devices

ulini.master_write(0x00, USBlini.CHECKSUM_MODE_NONE, [])
time.sleep(0.2)

# add listener and set master sequence

ulini.frame_listener_add(frame_listener)
ulini.master_set_sequence(args.i * 1000, 200, [0x0c])

if args.m.strip() != '':
	mqtt.loop_start()

if args.w:
	time.sleep(4)
	t = args.w.split(':')
	writeLini(int(t[0]), int(t[1]))
	time.sleep(4)
else:
	while True:
		time.sleep(1.0)
		abort = args.c != 0 and received >= args.c
		if abort:
			break
		pass

if args.m.strip() != '':
	mqtt.loop_stop()

tell(0, "closing device")
ulini.close()

if args.m.strip() != '':
	mqtt.disconnect()
