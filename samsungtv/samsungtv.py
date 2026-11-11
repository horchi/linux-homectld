#!/usr/bin/env python3
#
# samsungtv.py
#
# Samsung Tizen TV (2016+) <-> homectld via MQTT
#
#  - power state via REST (https://<tv>:8002/api/v2/ -> device.PowerState)
#  - power on via Wake-on-LAN, power off / keys / apps via the websocket remote API (samsungtvws)
#  - volume / mute via UPnP RenderingControl (port 9197)
#
# The first websocket connection shows an 'Allow' popup on the TV, the token is stored
# in the token file (-k), pair once with: samsungtv.py -H <ip> --pair
#

import sys
import os
import re
import time
import json
import queue
import threading
import socket
import signal
import syslog
import argparse
import html
import urllib3
import urllib.request

import websocket
import paho.mqtt.client as paho
from samsungtvws import SamsungTVWS

syslog.openlog(ident="samsungtv", logoption=syslog.LOG_PID)
urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)   # self signed certificate of the TV

# ── arguments ─────────────────────────────────────────────────────────────────

parser = argparse.ArgumentParser('samsungtv')
parser.add_argument('-H',           nargs='?', help='TV IP / hostname', default="")
parser.add_argument('-M',           nargs='?', help='TV MAC address (Wake-on-LAN)', default="")
parser.add_argument('-B',           nargs='?', help='Broadcast address for Wake-on-LAN (default 255.255.255.255)', default="255.255.255.255")
parser.add_argument('-P', type=int, nargs='?', help='Websocket port (default 8002)', default=8002)
parser.add_argument('-k',           nargs='?', help='Token file (default /etc/homectld/samsungtv.token)', default="/etc/homectld/samsungtv.token")
parser.add_argument('-i', type=int, nargs='?', help='Interval [seconds] (default 10)', default=10)
parser.add_argument('-m',           nargs='?', help='MQTT host', default="")
parser.add_argument('-p', type=int, nargs='?', help='MQTT port', default=1883)
parser.add_argument('-T',           nargs='?', help='MQTT topic', default="homectld2mqtt/samsungtv")
parser.add_argument('-t',           nargs='?', help='Sensor type string (default SAMTV)', default="SAMTV")
parser.add_argument('-K',           nargs='?', help='Keys, one sensor each (default HOME,BACK,...)',
                    default="HOME,BACK,MENU,SOURCE,GUIDE,INFO,UP,DOWN,LEFT,RIGHT,ENTER,PLAY,PAUSE,STOP,FF,REWIND,CHUP,CHDOWN,VOLUP,VOLDOWN")
parser.add_argument('-A',           nargs='?', help='Apps "Title=AppId,..."',
                    default="Netflix=3201907018807,YouTube=111299001912,Prime Video=3201910019365,Disney+=3201901017640")
parser.add_argument('-v', type=int, nargs='?', help='Verbosity level (0-3) (default 0)', default=0)
parser.add_argument('-l', action='store_true', help='Log to syslog (default console)')
parser.add_argument('-s', action='store_true', help='Show current status and exit')
parser.add_argument('--pair', action='store_true', help='Connect once to get the token (confirm the popup on the TV) and exit')
parser.add_argument('--key',        nargs='?', help='Send a key (e.g. KEY_VOLUP) and exit', default="")

args = parser.parse_args()

# ── homectld sensors ──────────────────────────────────────────────────────────

ADDR_POWER  = 0
ADDR_VOLUME = 1
ADDR_MUTE   = 2
ADDR_SOURCE = 3      # disabled, see 'sources' below
ADDR_APP    = 4
ADDR_KEY    = 10     # first key sensor, one for each key of -K (5 was the former key text sensor)

# TV source (address 3) - disabled:
#   the Q8F (2018) ignores KEY_HDMI1..4 via the websocket remote (KEY_HOME works) and the
#   current source can't be read. Options: KEY_HDMI (next HDMI input), the key sequence
#   KEY_SOURCE + n * KEY_RIGHT + KEY_ENTER or the SmartThings API (like Home Assistant,
#   needs the cloud and an OAuth app). Enable the 3 blocks marked with 'ADDR_SOURCE' to test it.

sources = { 'TV': 'KEY_TV', 'HDMI1': 'KEY_HDMI1', 'HDMI2': 'KEY_HDMI2', 'HDMI3': 'KEY_HDMI3', 'HDMI4': 'KEY_HDMI4' }

# key sensors (push buttons): name -> title, symbol

keyInfo = {
    'HOME':    ('Home',      'mdi:mdi-home'),
    'BACK':    ('Zurück',    'mdi:mdi-arrow-u-left-top'),
    'MENU':    ('Menü',      'mdi:mdi-menu'),
    'SOURCE':  ('Quelle',    'mdi:mdi-video-input-hdmi'),
    'GUIDE':   ('Guide',     'mdi:mdi-television-guide'),
    'INFO':    ('Info',      'mdi:mdi-information-outline'),
    'UP':      ('Hoch',      'mdi:mdi-chevron-up'),
    'DOWN':    ('Runter',    'mdi:mdi-chevron-down'),
    'LEFT':    ('Links',     'mdi:mdi-chevron-left'),
    'RIGHT':   ('Rechts',    'mdi:mdi-chevron-right'),
    'ENTER':   ('OK',        'mdi:mdi-checkbox-blank-circle-outline'),
    'PLAY':    ('Play',      'mdi:mdi-play'),
    'PAUSE':   ('Pause',     'mdi:mdi-pause'),
    'STOP':    ('Stop',      'mdi:mdi-stop'),
    'FF':      ('Vorspulen', 'mdi:mdi-fast-forward'),
    'REWIND':  ('Zurückspulen', 'mdi:mdi-rewind'),
    'CHUP':    ('Kanal +',   'mdi:mdi-chevron-double-up'),
    'CHDOWN':  ('Kanal -',   'mdi:mdi-chevron-double-down'),
    'VOLUP':   ('Lauter',    'mdi:mdi-volume-plus'),
    'VOLDOWN': ('Leiser',    'mdi:mdi-volume-minus'),
}

keys = [k.strip().upper().removeprefix('KEY_') for k in args.K.split(',') if k.strip()]

apps = {}
for item in args.A.split(','):
    if '=' in item:
        title, appId = item.split('=', 1)
        apps[title.strip()] = appId.strip()

volume_choices = ','.join(str(v) for v in range(0, 51))

parameters = {}
parameters[ADDR_POWER]  = {"cloneable": False, "widgettype": 0, "symbol": "mdi:mdi-television-off", "symbolOn": "mdi:mdi-television", "color": "gray", "colorOn": "rgb(3 169 244)"}
parameters[ADDR_VOLUME] = {"cloneable": False, "widgettype": 8}
parameters[ADDR_MUTE]   = {"cloneable": False, "widgettype": 0, "symbol": "mdi:mdi-volume-high", "symbolOn": "mdi:mdi-volume-off", "color": "gray", "colorOn": "rgb(244 67 54)"}
parameters[ADDR_SOURCE] = {"cloneable": False, "widgettype": 8}
parameters[ADDR_APP]    = {"cloneable": False, "widgettype": 8}
for n, key in enumerate(keys):
    symbol = keyInfo.get(key, (key, 'mdi:mdi-remote'))[1]
    parameters[ADDR_KEY + n] = {"cloneable": False, "widgettype": 0, "symbol": symbol, "symbolOn": symbol, "color": "rgb(3 169 244)", "colorOn": "rgb(3 169 244)"}

initial   = True
paramSent = set()     # addresses which got their widget parameters
cmd_queue = queue.Queue()
lastSource = ''

# ── helpers ───────────────────────────────────────────────────────────────────

def tell(level, msg):
    if args.v >= level:
        if args.l:
            syslog.syslog(syslog.LOG_INFO, msg)
        else:
            print(msg, flush=True)

# ── TV access ─────────────────────────────────────────────────────────────────

tv = None

def tvRemote():
    global tv
    if tv is None:
        tv = SamsungTVWS(host=args.H.strip(), port=args.P, token_file=args.k, timeout=5,
                         key_press_delay=0.2, name='homectld')
    return tv

def tvClose():
    global tv, tvReader
    if tv is not None:
        try:
            tv.close()
        except Exception:
            pass
    tv = None
    if tvReader is not None and tvReader is not threading.current_thread():
        tvReader.join(1)
    tvReader = None

# The TV pings the websocket client every few seconds and closes the connection after about 20
# seconds without a pong. samsungtvws answers only while reading, so a connection used for sending
# only is dead after a pause (a key sent into it is lost, or fails after the timeout and the new
# connection takes 1.5 - 3.5 seconds). So a reader thread reads the websocket all the time (answers
# the pings, like the Home Assistant integration does), the connection is opened in advance as long
# as the TV is on, and a new one is opened if the TV has closed it.

tvReader = None
tvNextConnect = 0.0

def tvReaderLoop(conn):
    while True:
        try:
            if not conn.recv():          # close frame
                break
        except websocket.WebSocketTimeoutException:
            continue                     # nothing to read, wait further
        except Exception as e:
            tell(2, f"Websocket reader: {type(e).__name__}: {e}")
            break
    tell(1, "Websocket to the TV closed")

def tvConnected():
    return tv is not None and tv.connection is not None and tvReader is not None and tvReader.is_alive()

def tvConnection():
    global tvReader
    if tv is not None and tv.connection is not None and not tvConnected():
        tvClose()                        # closed by the TV
    r = tvRemote()
    if r.connection is None:
        t0 = time.time()
        r.open()
        tvReader = threading.Thread(target=tvReaderLoop, args=(r.connection,), daemon=True)
        tvReader.start()
        tell(1, f"Websocket to the TV opened ({time.time() - t0:.1f}s)")
    return r

def sendKey(key):
    if not key.startswith('KEY_'):
        key = 'KEY_' + key
    tell(1, f"Send key {key}")
    for retry in range(2):
        try:
            tvConnection().send_key(key)
            return True
        except Exception as e:
            tell(0, f"Send key {key} failed: {e}")
            tvClose()
    return False

def runApp(appId):
    # REST (POST /api/v2/applications/<id>) works reliable, the websocket 'ed.apps.launch'
    # is ignored by some models (e.g. Q8F 2018) - it's the fallback only

    tell(1, f"Run app {appId}")
    try:
        tvRemote().rest_app_run(appId)
        return True
    except Exception as e:
        tell(0, f"Run app {appId} via REST failed: {e}, trying websocket")
    for retry in range(2):
        try:
            tvConnection().run_app(appId)
            return True
        except Exception as e:
            tell(0, f"Run app {appId} failed: {e}")
            tvClose()
    return False

def visibleApp():
    # title of the app in the foreground (of the configured apps), '' for none (TV, HDMI)
    for title, appId in apps.items():
        try:
            if tvRemote().rest_app_status(appId).get('visible'):
                return title
        except Exception as e:
            tell(2, f"App status of {appId} failed: {e}")
    return ''

def powerState():
    # 'on', 'standby' or 'off' (not reachable)
    try:
        info = tvRemote().rest_device_info()
        state = info.get('device', {}).get('PowerState', 'on')   # older models don't report it
        return state, info
    except Exception as e:
        tell(2, f"REST request failed: {e}")
        return 'off', None

def wakeOnLan():
    mac = re.sub('[^0-9a-fA-F]', '', args.M)
    if len(mac) != 12:
        tell(0, "Can't power on, missing / invalid MAC address (-M)")
        return
    packet = bytes.fromhex('FF' * 6 + mac * 16)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        for i in range(3):
            s.sendto(packet, (args.B.strip(), 9))
            time.sleep(0.1)
    tell(1, f"Wake-on-LAN sent to {args.M}")

def powerOn():
    state, _ = powerState()
    if state == 'on':
        return
    wakeOnLan()
    # TVs in network standby partially ignore the magic packet, they are switched by KEY_POWER
    for i in range(10):
        time.sleep(1)
        state, _ = powerState()
        if state == 'on':
            return
    if state == 'standby':
        sendKey('KEY_POWER')

def powerOff():
    state, _ = powerState()
    if state == 'on':      # KEY_POWER toggles, send it only if the TV is really on
        sendKey('KEY_POWER')

# ── UPnP RenderingControl (volume / mute) ─────────────────────────────────────

UPNP_URL     = 'http://{}:9197/upnp/control/RenderingControl1'
UPNP_SERVICE = 'urn:schemas-upnp-org:service:RenderingControl:1'

def upnpCall(action, arguments=''):
    body = ('<?xml version="1.0" encoding="utf-8"?>'
            '<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">'
            f'<s:Body><u:{action} xmlns:u="{UPNP_SERVICE}"><InstanceID>0</InstanceID>{arguments}</u:{action}></s:Body></s:Envelope>')
    req = urllib.request.Request(UPNP_URL.format(args.H.strip()), data=body.encode(), method='POST',
                                 headers={'Content-Type': 'text/xml; charset="utf-8"',
                                          'SOAPACTION': f'"{UPNP_SERVICE}#{action}"'})
    with urllib.request.urlopen(req, timeout=3) as r:
        return r.read().decode(errors='replace')

def upnpValue(xmlText, tag):
    m = re.search(f'<{tag}>([^<]*)</{tag}>', xmlText)
    return m.group(1) if m else None

def getVolume():
    try:
        v = upnpValue(upnpCall('GetVolume', '<Channel>Master</Channel>'), 'CurrentVolume')
        return int(v) if v is not None else None
    except Exception as e:
        tell(2, f"UPnP GetVolume failed: {e}")
        return None

def getMute():
    try:
        v = upnpValue(upnpCall('GetMute', '<Channel>Master</Channel>'), 'CurrentMute')
        return v in ('1', 'true') if v is not None else None
    except Exception as e:
        tell(2, f"UPnP GetMute failed: {e}")
        return None

def setVolume(volume):
    volume = max(0, min(100, int(volume)))
    tell(1, f"Set volume {volume}")
    try:
        upnpCall('SetVolume', f'<Channel>Master</Channel><DesiredVolume>{volume}</DesiredVolume>')
    except Exception as e:
        tell(0, f"UPnP SetVolume failed: {e}")

def setMute(mute):
    tell(1, f"Set mute {mute}")
    try:
        upnpCall('SetMute', f'<Channel>Master</Channel><DesiredMute>{1 if mute else 0}</DesiredMute>')
    except Exception as e:
        tell(0, f"UPnP SetMute failed: {e}")

# ── pair / key / show ─────────────────────────────────────────────────────────

if args.H.strip() == '':
    print("Missing TV address, add -H option")
    sys.exit(1)

if args.pair:
    print("Connecting, confirm the popup on the TV ...")
    try:
        r = SamsungTVWS(host=args.H.strip(), port=args.P, token_file=args.k, timeout=30, name='homectld')
        r.open()
        r.close()
    except Exception as e:
        print(f"Pairing failed: {e}")
        sys.exit(1)
    if os.path.isfile(args.k):
        print(f"Token stored in '{args.k}'")
        sys.exit(0)
    print("Connected, but no token received (the TV may not need one)")
    sys.exit(0)

if args.key.strip() != '':
    rc = sendKey(args.key.strip())
    tvClose()
    sys.exit(0 if rc else 1)

if args.s:
    state, info = powerState()
    print(f"Power:  {state}")
    if info:
        dev = info.get('device', {})
        print(f"Model:  {dev.get('modelName', '')} ({html.unescape(dev.get('name', ''))})")
        print(f"OS:     {dev.get('OS', '')}")
        print(f"MAC:    {dev.get('wifiMac', '')}")
        print(f"Token:  {'supported' if dev.get('TokenAuthSupport') == 'true' else 'not required'}")
    if state == 'on':
        print(f"Volume: {getVolume()}")
        print(f"Mute:   {getMute()}")
    tvClose()
    sys.exit(0)

# ── MQTT ──────────────────────────────────────────────────────────────────────

inTopic = args.T.strip() + '/in'

def publishInit():
    init = { 'type': args.t.strip(), 'action': 'init', 'topic': inTopic, 'restore': False }   # don't switch the TV on restart of homectld
    mqtt.publish(args.T.strip(), json.dumps(init))

def publishMqtt(sensor):
    # the parameters with the first message of each sensor (the sensors except the power one are
    # published first when the TV is switched on, without the parameters they would become a meter)
    if sensor['address'] not in paramSent and sensor['address'] in parameters:
        sensor['parameter'] = parameters[sensor['address']]
        paramSent.add(sensor['address'])
    msg = json.dumps(sensor)
    tell(3, msg)
    mqtt.publish(args.T.strip(), msg)

def onConnect(client, userdata, flags, reason_code, properties=None):
    global initial
    tell(0, f"Connected MQTT with result code {reason_code}, subscribe '{inTopic}'")
    client.subscribe(inTopic)
    publishInit()
    initial = True
    paramSent.clear()

def onMessage(client, userdata, msg):
    tell(1, f"Received {msg.payload.decode(errors='replace')}")
    try:
        cmd_queue.put(json.loads(msg.payload.decode()))
    except Exception as e:
        tell(0, f"onMessage parse error: {e}")

def processCommand(s):
    global initial, lastSource

    action = s.get('action', '')

    if action == 'requestinit':
        publishInit()
        initial = True
        paramSent.clear()
        return

    if action == 'init':        # config packet of homectld, nothing to configure here
        return

    # free key / app for scripts and actions: {"key": "KEY_HOME"} / {"app": "Netflix"}

    if 'key' in s:
        sendKey(str(s['key']))
        return

    if 'app' in s:
        appId = apps.get(str(s['app']), str(s['app']))
        runApp(appId)
        return

    address = int(s.get('address', -1))
    value   = s.get('value', '')

    if address == ADDR_POWER:
        if int(value):
            powerOn()
        else:
            powerOff()

    elif address == ADDR_VOLUME:
        setVolume(int(value))

    elif address == ADDR_MUTE:
        setMute(bool(int(value)))

    # ADDR_SOURCE - disabled, see 'sources'
    # elif address == ADDR_SOURCE:
    #     if str(value) in sources:
    #         lastSource = str(value)
    #         sendKey(sources[lastSource])

    elif address == ADDR_APP:
        if str(value) in apps:
            runApp(apps[str(value)])

    elif ADDR_KEY <= address < ADDR_KEY + len(keys):     # push button, any value sends the key
        sendKey(keys[address - ADDR_KEY])
        publishKey(address - ADDR_KEY)

    else:
        tell(0, f"Ignoring unknown address {address}")

unreachable = 0   # count of failed REST requests in series
tvOn = False      # the TV is on (last update)

def publishKey(n):
    # the key sensors are always 'off', a click of the widget sends the key
    key = keys[n]
    publishMqtt({'type': args.t.strip(), 'address': ADDR_KEY + n, 'kind': 'status', 'state': False,
                 'title': 'TV ' + keyInfo.get(key, (key, ''))[0], 'rights': 2})

def update():
    global initial, unreachable, tvOn
    stype = args.t.strip()
    state, info = powerState()

    # the TV doesn't answer sometimes, report 'off' only after the 2nd failed request in series

    unreachable = unreachable + 1 if info is None else 0

    if unreachable == 1:
        tell(1, "TV not reachable, retry next interval")
        return

    on = state == 'on'
    tvOn = on

    if not on:
        tvClose()

    tell(1, f"Power: {state}")
    publishMqtt({'type': stype, 'address': ADDR_POWER, 'kind': 'status', 'state': on,
                 'title': 'TV', 'rights': 2})

    volume = getVolume() if on else None
    mute   = getMute() if on else None

    if volume is not None or initial:
        publishMqtt({'type': stype, 'address': ADDR_VOLUME, 'kind': 'value', 'value': volume or 0,
                     'unit': '', 'title': 'TV Lautstärke', 'choices': volume_choices, 'rights': 2})
    if mute is not None or initial:
        publishMqtt({'type': stype, 'address': ADDR_MUTE, 'kind': 'status', 'state': bool(mute),
                     'title': 'TV Stumm', 'rights': 2})
    if initial or on:
        # ADDR_SOURCE - disabled, see 'sources'
        # publishMqtt({'type': stype, 'address': ADDR_SOURCE, 'kind': 'text', 'text': lastSource,
        #              'title': 'TV Quelle', 'choices': ','.join(sources.keys()), 'rights': 2})
        publishMqtt({'type': stype, 'address': ADDR_APP, 'kind': 'text', 'text': visibleApp() if on else '',
                     'title': 'TV App', 'choices': ','.join(apps.keys()), 'rights': 2})
        # the keys only while the TV is on, homectld invalidates them 2 minutes after
        # power off (like volume, mute, app) - they are not usable then
        for n in range(len(keys)):
            publishKey(n)

    initial = False

def shutdown(sig, frame):
    raise KeyboardInterrupt

signal.signal(signal.SIGTERM, shutdown)
signal.signal(signal.SIGINT, shutdown)

if args.m.strip() == '':
    print("Missing MQTT host, add -m option")
    sys.exit(1)

try:
    mqtt = paho.Client(paho.CallbackAPIVersion.VERSION2, "samsungtv", protocol=paho.MQTTv311, clean_session=True)
except AttributeError:      # paho-mqtt < 2.0
    mqtt = paho.Client("samsungtv", protocol=paho.MQTTv311, clean_session=True)

mqtt.on_connect = onConnect
mqtt.on_message = onMessage

tell(0, f"Connecting to '{args.m.strip()}:{args.p}', topic '{args.T.strip()}', TV '{args.H.strip()}'")

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
            nextUpdate = time.time() + 1      # update soon, the TV needs a moment
        except queue.Empty:
            pass
        except Exception as e:
            tell(0, f"Command error: {e}")

        if tvOn and not tvConnected() and time.time() >= tvNextConnect:
            tvNextConnect = time.time() + 5       # at most every 5 seconds
            try:
                tvConnection()
            except Exception as e:
                tell(1, f"Connect to the TV failed: {e}")
                tvClose()

        if time.time() >= nextUpdate:
            try:
                update()
            except Exception as e:
                tell(0, f"Update error: {e}")
            nextUpdate = time.time() + args.i

except KeyboardInterrupt:
    tvClose()
    mqtt.loop_stop()
    mqtt.disconnect()
    tell(0, "exit")
