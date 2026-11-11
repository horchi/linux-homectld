#!/usr/bin/env python3
#
# denon.py
#
# Denon / Marantz AV receiver (AVR-X series, tested with the Denon AVR-X3000) <-> homectld via MQTT
#
#  - status by HTTP (/goform/formMainZone_MainZoneXmlStatus.xml, /goform/formZone2_Zone2XmlStatus.xml)
#  - commands by HTTP (/goform/formiPhoneAppDirect.xml?<command>), the same ASCII commands as the
#    telnet port 23 (PWON, MV45, MUON, SINET, Z2ON, ...)
#  - main zone and zone 2: power, volume (display in dB, push buttons louder / quieter), mute, source (names
#    as renamed at the receiver), surround mode
#
# Free commands (scripts, actions) to the command topic: {"cmd": "MVUP"}
#

import sys
import time
import json
import queue
import signal
import syslog
import argparse
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET

import paho.mqtt.client as paho

syslog.openlog(ident="denon", logoption=syslog.LOG_PID)

# ── arguments ─────────────────────────────────────────────────────────────────

parser = argparse.ArgumentParser('denon')
parser.add_argument('-H',           nargs='?', help='Receiver IP / hostname', default="")
parser.add_argument('-i', type=int, nargs='?', help='Interval [seconds] (default 5)', default=5)
parser.add_argument('-V', type=int, nargs='?', help='Maximum volume, 0 - 98, 80 = 0 dB (default 75)', default=75)
parser.add_argument('-S', type=float, nargs='?', help='Step of the volume buttons [dB] (default 1)', default=1.0)
parser.add_argument('-m',           nargs='?', help='MQTT host', default="")
parser.add_argument('-p', type=int, nargs='?', help='MQTT port', default=1883)
parser.add_argument('-T',           nargs='?', help='MQTT topic', default="homectld2mqtt/denon")
parser.add_argument('-t',           nargs='?', help='Sensor type string (default DENON)', default="DENON")
parser.add_argument('-v', type=int, nargs='?', help='Verbosity level (0-3) (default 0)', default=0)
parser.add_argument('-l', action='store_true', help='Log to syslog (default console)')
parser.add_argument('-s', action='store_true', help='Show current status and exit')
parser.add_argument('--cmd',        nargs='?', help='Send a command (e.g. MVUP) and exit', default="")

args = parser.parse_args()

# ── homectld sensors ──────────────────────────────────────────────────────────

# main zone                     zone 2

ADDR_POWER    = 0;              ADDR_Z2_POWER  = 10
ADDR_VOLUME   = 1;              ADDR_Z2_VOLUME = 11
ADDR_MUTE     = 2;              ADDR_Z2_MUTE   = 12
ADDR_SOURCE   = 3;              ADDR_Z2_SOURCE = 13
ADDR_SURROUND = 4
ADDR_VOLUP    = 5;              ADDR_Z2_VOLUP   = 14
ADDR_VOLDOWN  = 6;              ADDR_Z2_VOLDOWN = 15

# the function names of the receiver (InputFuncList) -> command code (SI<code>, Z2<code>)

functionCodes = {
    'TUNER': 'TUNER', 'CD': 'CD', 'NETWORK': 'NET', 'DVD': 'DVD', 'Blu-ray': 'BD', 'TV AUDIO': 'TV',
    'CBL/SAT': 'SAT/CBL', 'GAME': 'GAME', 'AUX': 'AUX1', 'Media Player': 'MPLAY', 'iPod/USB': 'USB/IPOD',
    'PHONO': 'PHONO', 'Phono': 'PHONO', 'HD Radio': 'HDRADIO', 'Internet Radio': 'IRADIO',
    'Favorites': 'FAVORITES', 'Server': 'SERVER', 'USB': 'USB', 'AUX2': 'AUX2',
}

# surround modes to select (MS<mode>), the receiver reports its own names (e.g. 'PLII Cinema')

surroundModes = [ 'MOVIE', 'MUSIC', 'GAME', 'DIRECT', 'PURE DIRECT', 'STEREO', 'AUTO', 'DOLBY DIGITAL', 'DTS SURROUND',
                  'MCH STEREO', 'ROCK ARENA', 'JAZZ CLUB', 'MONO MOVIE', 'MATRIX', 'VIDEO GAME', 'VIRTUAL' ]

parameters = {
    ADDR_POWER:     {"cloneable": False, "widgettype": 0, "symbol": "mdi:mdi-audio-video-off", "symbolOn": "mdi:mdi-audio-video", "color": "gray", "colorOn": "rgb(3 169 244)"},
    ADDR_VOLUME:    {"cloneable": False, "widgettype": 3},
    ADDR_VOLUP:     {"cloneable": False, "widgettype": 0, "symbol": "mdi:mdi-volume-plus", "symbolOn": "mdi:mdi-volume-plus", "color": "rgb(3 169 244)", "colorOn": "rgb(3 169 244)"},
    ADDR_VOLDOWN:   {"cloneable": False, "widgettype": 0, "symbol": "mdi:mdi-volume-minus", "symbolOn": "mdi:mdi-volume-minus", "color": "rgb(3 169 244)", "colorOn": "rgb(3 169 244)"},
    ADDR_MUTE:      {"cloneable": False, "widgettype": 0, "symbol": "mdi:mdi-volume-high", "symbolOn": "mdi:mdi-volume-off", "color": "gray", "colorOn": "rgb(244 67 54)"},
    ADDR_SOURCE:    {"cloneable": False, "widgettype": 8},
    ADDR_SURROUND:  {"cloneable": False, "widgettype": 8},
    ADDR_Z2_POWER:  {"cloneable": False, "widgettype": 0, "symbol": "mdi:mdi-speaker-off", "symbolOn": "mdi:mdi-speaker", "color": "gray", "colorOn": "rgb(3 169 244)"},
    ADDR_Z2_VOLUME: {"cloneable": False, "widgettype": 3},
    ADDR_Z2_VOLUP:  {"cloneable": False, "widgettype": 0, "symbol": "mdi:mdi-volume-plus", "symbolOn": "mdi:mdi-volume-plus", "color": "rgb(3 169 244)", "colorOn": "rgb(3 169 244)"},
    ADDR_Z2_VOLDOWN:{"cloneable": False, "widgettype": 0, "symbol": "mdi:mdi-volume-minus", "symbolOn": "mdi:mdi-volume-minus", "color": "rgb(3 169 244)", "colorOn": "rgb(3 169 244)"},
    ADDR_Z2_MUTE:   {"cloneable": False, "widgettype": 0, "symbol": "mdi:mdi-volume-high", "symbolOn": "mdi:mdi-volume-off", "color": "gray", "colorOn": "rgb(244 67 54)"},
    ADDR_Z2_SOURCE: {"cloneable": False, "widgettype": 8},
}

paramSent   = set()           # addresses which got their widget parameters (homectld takes them when it adds the sensor)
cmd_queue   = queue.Queue()
unreachable = 0                 # failed requests in series
zones       = { 'main': None, 'zone2': None }      # the last status of the zones

# ── helpers ───────────────────────────────────────────────────────────────────

def tell(level, msg):
    if args.v >= level:
        if args.l:
            syslog.syslog(syslog.LOG_INFO, msg)
        else:
            print(msg, flush=True)

# ── receiver access ───────────────────────────────────────────────────────────

def httpGet(path, timeout=3):
    with urllib.request.urlopen(f'http://{args.H.strip()}{path}', timeout=timeout) as r:
        return r.read()

def sendCommand(cmd):
    tell(1, f"Send command '{cmd}'")
    try:
        httpGet('/goform/formiPhoneAppDirect.xml?' + urllib.parse.quote(cmd, safe='/'))
        return True
    except Exception as e:
        tell(0, f"Command '{cmd}' failed: {e}")
        return False

def fetchZone(path):
    # the status of a zone:
    #   { power, volume (0 - 98, 80 = 0 dB), mute, source (command code), sources [(name, code)], surround }

    root = ET.fromstring(httpGet(path))

    def text(tag):
        e = root.find(tag + '/value')
        return (e.text or '').strip() if e is not None else ''

    def values(tag):
        return [(e.text or '').strip() for e in root.findall(tag + '/value')]

    # the renamed sources, in the order of InputFuncList; DEL = removed in the menu of the receiver

    functions = values('InputFuncList')
    renamed   = []
    for e in root.findall('RenameSource/value'):
        inner = e.find('value')
        renamed.append(((inner.text if inner is not None else e.text) or '').strip())
    deleted = values('SourceDelete')

    sources = []
    for n, function in enumerate(functions):
        code = functionCodes.get(function)
        if code is None or (n < len(deleted) and deleted[n] == 'DEL'):
            continue
        sources.append((renamed[n] if n < len(renamed) and renamed[n] else function, code))

    # 'volume' is 0 - 98 (80 = 0 dB), the receiver reports dB (VolumeDisplay 'Absolute') or 0 - 98 ('Relative')

    try:
        volume = float(text('MasterVolume'))
        if text('VolumeDisplay') != 'Relative':
            volume += 80
        volume = round(volume, 1)
    except ValueError:
        volume = None

    return { 'power': text('Power').upper() == 'ON', 'volume': volume, 'mute': text('Mute').lower() == 'on',
             'source': text('InputFuncSelect'), 'sources': sources, 'surround': text('SurrMode') }

def sourceName(zone, code):
    for name, c in zone['sources']:
        if c == code:
            return name
    return code

def sourceCode(zone, name):
    if zone:
        for n, code in zone['sources']:
            if n == name:
                return code
    return None

def volumeCommand(prefix, volume):
    volume = max(0, min(args.V, float(volume)))
    return prefix + ('%03d' % round(volume * 10) if volume != int(volume) else '%02d' % int(volume))

def changeVolume(name, direction):
    # the volume buttons: one step up / down from the last known volume (an optimistic update, so
    # several clicks before the next poll add up), never above the maximum volume (-V)

    prefix = 'MV' if name == 'main' else 'Z2'
    zone   = zones[name]

    if zone is None or zone['volume'] is None:
        sendCommand(prefix + ('UP' if direction > 0 else 'DOWN'))
        return

    if direction > 0 and zone['volume'] >= args.V:
        tell(1, f"Volume {zone['volume']} is at the maximum ({args.V}), ignoring")
        return

    zone['volume'] = max(0, min(args.V, zone['volume'] + direction * args.S))
    sendCommand(volumeCommand(prefix, zone['volume']))

def publishButtons(first, second, title):
    # push buttons, always 'off', a click of the widget sends the command
    for address, text in ((first, 'Lauter'), (second, 'Leiser')):
        publishMqtt({'type': args.t.strip(), 'address': address, 'kind': 'status', 'state': False,
                     'title': f'{title} {text}', 'rights': 2})

# ── show / command ────────────────────────────────────────────────────────────

if args.H.strip() == '':
    print("Missing receiver address, add -H option")
    sys.exit(1)

if args.cmd.strip() != '':
    sys.exit(0 if sendCommand(args.cmd.strip()) else 1)

if args.s:
    try:
        info = ET.fromstring(httpGet('/goform/Deviceinfo.xml'))
        print(f"Model:   {info.findtext('ModelName', '').lstrip('*')}  (API {info.findtext('CommApiVers', '')}, {info.findtext('DeviceZones', '')} zones)")
        for title, path in (('Main zone', '/goform/formMainZone_MainZoneXmlStatus.xml'), ('Zone 2', '/goform/formZone2_Zone2XmlStatus.xml')):
            z = fetchZone(path)
            print(f"{title}: {'on' if z['power'] else 'off'}, volume {z['volume']} ({round(z['volume'] - 80, 1) if z['volume'] is not None else '?'} dB), "
                  f"mute {z['mute']}, source '{sourceName(z, z['source'])}' ({z['source']}), surround '{z['surround']}'")
            print(f"   sources: {', '.join(n + ' [' + c + ']' for n, c in z['sources'])}")
    except Exception as e:
        print(f"Receiver not reachable: {e}")
        sys.exit(1)
    sys.exit(0)

# ── MQTT ──────────────────────────────────────────────────────────────────────

inTopic = args.T.strip() + '/in'

def publishInit():
    # restore: false - homectld mustn't switch the receiver by a restart of homectld
    mqtt.publish(args.T.strip(), json.dumps({ 'type': args.t.strip(), 'action': 'init', 'topic': inTopic, 'restore': False }))

def publishMqtt(sensor):
    # the parameters with the first message of each sensor - the sensors except the power ones are
    # published first when their zone is switched on, homectld adds them then (without the parameters
    # they would become the default widget, a meter)
    if sensor['address'] not in paramSent and sensor['address'] in parameters:
        sensor['parameter'] = parameters[sensor['address']]
        paramSent.add(sensor['address'])
    msg = json.dumps(sensor)
    tell(3, msg)
    mqtt.publish(args.T.strip(), msg)

def onConnect(client, userdata, flags, reason_code, properties=None):
    tell(0, f"Connected MQTT with result code {reason_code}, subscribe '{inTopic}'")
    client.subscribe(inTopic)
    publishInit()
    paramSent.clear()

def onMessage(client, userdata, msg):
    tell(1, f"Received {msg.payload.decode(errors='replace')}")
    try:
        cmd_queue.put(json.loads(msg.payload.decode()))
    except Exception as e:
        tell(0, f"onMessage parse error: {e}")

def processCommand(s):
    action = s.get('action', '')

    if action == 'requestinit':
        publishInit()
        paramSent.clear()
        return

    if action == 'init':        # config packet of homectld, nothing to configure here
        return

    if 'cmd' in s:              # {"cmd": "MVUP"}
        sendCommand(str(s['cmd']).strip())
        return

    address = int(s.get('address', -1))
    value   = s.get('value', '')
    on      = str(value).lower() in ('1', 'true', 'on')

    if address == ADDR_POWER:
        sendCommand('PWON' if on else 'PWSTANDBY')
    elif address == ADDR_VOLUME:
        sendCommand(volumeCommand('MV', value))
    elif address in (ADDR_VOLUP, ADDR_VOLDOWN):
        changeVolume('main', 1 if address == ADDR_VOLUP else -1)
        publishButtons(ADDR_VOLUP, ADDR_VOLDOWN, 'AVR')
    elif address in (ADDR_Z2_VOLUP, ADDR_Z2_VOLDOWN):
        changeVolume('zone2', 1 if address == ADDR_Z2_VOLUP else -1)
        publishButtons(ADDR_Z2_VOLUP, ADDR_Z2_VOLDOWN, 'AVR Zone 2')
    elif address == ADDR_MUTE:
        sendCommand('MUON' if on else 'MUOFF')
    elif address == ADDR_SOURCE:
        code = sourceCode(zones['main'], str(value))
        if code:
            sendCommand('SI' + code)
    elif address == ADDR_SURROUND:
        if str(value) in surroundModes:
            sendCommand('MS' + str(value))
    elif address == ADDR_Z2_POWER:
        sendCommand('Z2ON' if on else 'Z2OFF')
    elif address == ADDR_Z2_VOLUME:
        sendCommand(volumeCommand('Z2', value))
    elif address == ADDR_Z2_MUTE:
        sendCommand('Z2MUON' if on else 'Z2MUOFF')
    elif address == ADDR_Z2_SOURCE:
        code = sourceCode(zones['zone2'], str(value))
        if code:
            sendCommand('Z2' + code)
    else:
        tell(0, f"Ignoring unknown address {address}")

def update():
    global unreachable
    stype = args.t.strip()

    try:
        zones['main']  = main  = fetchZone('/goform/formMainZone_MainZoneXmlStatus.xml')
        zones['zone2'] = zone2 = fetchZone('/goform/formZone2_Zone2XmlStatus.xml')
        unreachable = 0
    except Exception as e:
        # off (no network standby) or busy, 'off' only after the 2nd failed request in series
        unreachable += 1
        tell(1, f"Receiver not reachable ({unreachable}): {e}")
        if unreachable < 2:
            return
        main = zone2 = None

    tell(2, f"main: {main}  zone2: {zone2}")

    # the power sensors are always published, the others only while their zone is on
    # (homectld invalidates them 2 minutes after power off)

    publishMqtt({'type': stype, 'address': ADDR_POWER, 'kind': 'status', 'state': bool(main and main['power']),
                 'title': 'AVR', 'rights': 2})
    publishMqtt({'type': stype, 'address': ADDR_Z2_POWER, 'kind': 'status', 'state': bool(zone2 and zone2['power']),
                 'title': 'AVR Zone 2', 'rights': 2})

    if main and main['power']:
        if main['volume'] is not None:
            publishMqtt({'type': stype, 'address': ADDR_VOLUME, 'kind': 'value', 'value': round(main['volume'] - 80, 1), 'unit': 'dB',
                         'title': 'AVR Lautstärke'})
        publishButtons(ADDR_VOLUP, ADDR_VOLDOWN, 'AVR')
        publishMqtt({'type': stype, 'address': ADDR_MUTE, 'kind': 'status', 'state': main['mute'],
                     'title': 'AVR Stumm', 'rights': 2})
        publishMqtt({'type': stype, 'address': ADDR_SOURCE, 'kind': 'text', 'text': sourceName(main, main['source']),
                     'title': 'AVR Quelle', 'choices': ','.join(n for n, c in main['sources']), 'rights': 2})
        publishMqtt({'type': stype, 'address': ADDR_SURROUND, 'kind': 'text', 'text': main['surround'],
                     'title': 'AVR Surround', 'choices': ','.join(surroundModes), 'rights': 2})

    if zone2 and zone2['power']:
        if zone2['volume'] is not None:
            publishMqtt({'type': stype, 'address': ADDR_Z2_VOLUME, 'kind': 'value', 'value': round(zone2['volume'] - 80, 1), 'unit': 'dB',
                         'title': 'AVR Zone 2 Lautstärke'})
        publishButtons(ADDR_Z2_VOLUP, ADDR_Z2_VOLDOWN, 'AVR Zone 2')
        publishMqtt({'type': stype, 'address': ADDR_Z2_MUTE, 'kind': 'status', 'state': zone2['mute'],
                     'title': 'AVR Zone 2 Stumm', 'rights': 2})
        publishMqtt({'type': stype, 'address': ADDR_Z2_SOURCE, 'kind': 'text', 'text': sourceName(zone2, zone2['source']),
                     'title': 'AVR Zone 2 Quelle', 'choices': ','.join(n for n, c in zone2['sources']), 'rights': 2})

def shutdown(sig, frame):
    raise KeyboardInterrupt

signal.signal(signal.SIGTERM, shutdown)
signal.signal(signal.SIGINT, shutdown)

if args.m.strip() == '':
    print("Missing MQTT host, add -m option")
    sys.exit(1)

try:
    mqtt = paho.Client(paho.CallbackAPIVersion.VERSION2, "denon", protocol=paho.MQTTv311, clean_session=True)
except AttributeError:      # paho-mqtt < 2.0
    mqtt = paho.Client("denon", protocol=paho.MQTTv311, clean_session=True)

mqtt.on_connect = onConnect
mqtt.on_message = onMessage

tell(0, f"Connecting to '{args.m.strip()}:{args.p}', topic '{args.T.strip()}', receiver '{args.H.strip()}'")

while True:
    try:
        mqtt.connect(args.m.strip(), args.p, 60)
        break
    except Exception as e:
        tell(0, f"MQTT connect failed: {e}, retrying in 10 seconds")
        time.sleep(10)

mqtt.loop_start()     # reconnects automatically

# ── main loop ─────────────────────────────────────────────────────────────────

try:
    nextUpdate = 0

    while True:
        try:
            cmd = cmd_queue.get(timeout=0.2)
            processCommand(cmd)
            nextUpdate = time.time() + 0.7      # update soon, the receiver needs a moment
        except queue.Empty:
            pass
        except Exception as e:
            tell(0, f"Command error: {e}")

        if time.time() >= nextUpdate:
            try:
                update()
            except Exception as e:
                tell(0, f"Update error: {e}")
            nextUpdate = time.time() + args.i

except KeyboardInterrupt:
    mqtt.loop_stop()
    mqtt.disconnect()
    tell(0, "exit")
