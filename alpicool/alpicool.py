#!/usr/bin/env python3

# Alpicool/MANTUM/BrassMonkey/Vevor BLE compressor fridge integration for homectld
# Protocol: Alpicool BLE (Service 1234, Command 1235, Notify 1236)
# Compatible with: MANTUM IceCube, Alpicool, BrassMonkey, Vevor, Iceco (same OEM platform)

import os, sys

#venv = os.path.expanduser("~/.venvs/homectld")
#if os.path.isdir(venv) and not sys.prefix.startswith(venv):
#    python = os.path.join(venv, "bin", "python3")
#    if os.path.isfile(python):
#        os.execv(python, [python] + sys.argv)

import struct
import asyncio
import argparse
import syslog
import signal
import time
import copy
import json
import queue
import paho.mqtt.client as paho
import random
import os
from asyncio import Future
from enum import Enum
from typing import Optional, Union, Callable, Any
from dataclasses import dataclass

from bleak import BleakScanner, BleakClient
from bleak.exc import BleakError

from bleak.backends.device import BLEDevice
from bleak.backends.characteristic import BleakGATTCharacteristic

syslog.openlog(ident="alpicoolmqtt", logoption=syslog.LOG_PID)

temp_choices = ','.join(str(t) for t in range(-1, 12))

# Globale Variable für die dauerhafte Instanz
global_fridge = None
global_fridge_status = None

# ── BLE constants ─────────────────────────────────────────────────────────────

SERVICE_UID  = '1234'
SERVICE_UUID = '00001234-0000-1000-8000-00805f9b34fb'
TX_UUID      = '00001235-0000-1000-8000-00805f9b34fb'   # command / write
RX_UUID      = '00001236-0000-1000-8000-00805f9b34fb'   # notify  / read

# ── error codes  ──────────────────────────────────────────────────────────────

ERROR_MESSAGES = {
    0: "OK",
    1: "F1 Unterspannungsschutz",
    2: "F2 Lüfter zieht zu viel Strom",
    3: "F3 Kompressor zu viele Startversuche",
    4: "F4 Kompressor Drehzahlfehler",
    5: "F5 Elektronik / Kompressor heiß",
    6: "F6 Controller Fehler",
    7: "F7 Temperatursensor defekt"
}

def get_error_text(code: int) -> str:
    return ERROR_MESSAGES.get(code, f"Unbekannter Fehler ({code})")

# ── protocol enums ────────────────────────────────────────────────────────────

class FridgeCommand(int, Enum):
    Bind           = 0
    Query          = 1
    Set            = 2
    Reset          = 4
    SetUnit1Target = 5
    SetUnit2Target = 6

class FridgeRunMode(int, Enum):
    Max = 0
    Eco = 1

class FridgeBatterySaver(int, Enum):
    Low  = 0
    Mid  = 1
    High = 2

class FridgeTemperatureUnit(int, Enum):
    Celsius    = 0
    Fahrenheit = 1

# ── data structures ───────────────────────────────────────────────────────────

@dataclass
class FridgeUnitData:
    target_temperature:          int
    hysteresis:                  int
    temperature_correction_hot:  int
    temperature_correction_mid:  int
    temperature_correction_cold: int
    temperature_correction_halt: int
    current_temperature:         int

@dataclass
class FridgeData:
    controls_locked:            bool
    powered_on:                 bool
    compressor_running:         bool
    run_mode:                   FridgeRunMode
    battery_saver:              FridgeBatterySaver
    max_selectable_temperature: int
    min_selectable_temperature: int
    start_delay:                int
    temperature_unit:           FridgeTemperatureUnit
    battery_charge_percent:     int
    battery_voltage:            float
    error_code:                 int
    unit1:                      FridgeUnitData
    unit2:                      Optional[FridgeUnitData]

# ── packet codec ──────────────────────────────────────────────────────────────

def create_packet(data: Union[bytes, bytearray]) -> bytes:
    pkt  = b'\xFE\xFE' + struct.pack('B', len(data) + 2) + data
    pkt += struct.pack('>H', sum(int(v) for v in pkt))
    return pkt

def decode_unit1_data(data: Union[bytes, bytearray]) -> FridgeUnitData:
    target_temperature, hysteresis, \
    temperature_correction_hot, temperature_correction_mid, \
    temperature_correction_cold, temperature_correction_halt, \
    current_temperature = struct.unpack_from('>bxxbxxbbbbb', data, 4)
    return FridgeUnitData(
        target_temperature          = target_temperature,
        hysteresis                  = hysteresis,
        temperature_correction_hot  = temperature_correction_hot,
        temperature_correction_mid  = temperature_correction_mid,
        temperature_correction_cold = temperature_correction_cold,
        temperature_correction_halt = temperature_correction_halt,
        current_temperature         = current_temperature
    )

def decode_unit2_data(data: Union[bytes, bytearray]) -> Optional[FridgeUnitData]:
    if len(data) < 28:
        return None
    target_temperature, hysteresis, \
    temperature_correction_hot, temperature_correction_mid, \
    temperature_correction_cold, temperature_correction_halt, \
    current_temperature = struct.unpack_from('>bxxbbbbbb', data, 18)
    return FridgeUnitData(
        target_temperature          = target_temperature,
        hysteresis                  = hysteresis,
        temperature_correction_hot  = temperature_correction_hot,
        temperature_correction_mid  = temperature_correction_mid,
        temperature_correction_cold = temperature_correction_cold,
        temperature_correction_halt = temperature_correction_halt,
        current_temperature         = current_temperature
    )

def decode_fridge_data(data: Union[bytes, bytearray]) -> FridgeData:
    if len(data) < 18:
        raise ValueError('Packet too short')

    controls_locked, powered_on, run_mode, battery_saver, \
    target_temperature, \
    max_selectable_temperature, min_selectable_temperature, \
    byte8, \
    start_delay, temperature_unit, \
    byte11, byte12, byte13, byte14, byte15, \
    battery_charge_percent, battery_voltage_int, battery_voltage_frac = \
        struct.unpack_from('>??BBBbbBBBBBBBBBBB', data, 0)

    print(f"{byte8} | {byte11} | {byte12} | {byte13} | {byte14} | {byte15}")

    error_code = 0
    compressor_running = False

    # optional für bestimte modelle
    running_status = None
    if len(data) >= 28:
        running_status = struct.unpack_from('B', data, 28)

    return FridgeData(
        controls_locked            = controls_locked,
        powered_on                 = powered_on,
        run_mode                   = FridgeRunMode(run_mode),
        battery_saver              = FridgeBatterySaver(battery_saver),
        max_selectable_temperature = max_selectable_temperature,
        min_selectable_temperature = min_selectable_temperature,
        start_delay                = start_delay,
        temperature_unit           = FridgeTemperatureUnit(temperature_unit),
        battery_charge_percent     = battery_charge_percent,
        battery_voltage            = battery_voltage_int + battery_voltage_frac / 10,
        error_code                 = error_code,
        compressor_running         = compressor_running,
        unit1                      = decode_unit1_data(data),
        unit2                      = decode_unit2_data(data)
    )

def encode_query_command() -> bytes:
    return create_packet(struct.pack('B', FridgeCommand.Query))

def encode_set_command(data: FridgeData) -> bytes:
    if data.unit2 is None:
        return create_packet(struct.pack(
            '>B??BBbbbbBBbbbb',
            FridgeCommand.Set,
            data.controls_locked, data.powered_on, data.run_mode, data.battery_saver,
            data.unit1.target_temperature, data.max_selectable_temperature,
            data.min_selectable_temperature, data.unit1.hysteresis,
            data.start_delay, data.temperature_unit,
            data.unit1.temperature_correction_hot, data.unit1.temperature_correction_mid,
            data.unit1.temperature_correction_cold, data.unit1.temperature_correction_halt
        ))
    else:
        return create_packet(struct.pack(
            '>B??BBbbbbBBbbbbbxxbbbbbxxx',
            FridgeCommand.Set,
            data.controls_locked, data.powered_on, data.run_mode, data.battery_saver,
            data.unit1.target_temperature, data.max_selectable_temperature,
            data.min_selectable_temperature, data.unit1.hysteresis,
            data.start_delay, data.temperature_unit,
            data.unit1.temperature_correction_hot, data.unit1.temperature_correction_mid,
            data.unit1.temperature_correction_cold, data.unit1.temperature_correction_halt,
            data.unit2.target_temperature, data.unit2.hysteresis,
            data.unit2.temperature_correction_hot, data.unit2.temperature_correction_mid,
            data.unit2.temperature_correction_cold, data.unit2.temperature_correction_halt
        ))

def encode_set_unit1_target_command(temp: int) -> bytes:
    return create_packet(struct.pack('Bb', FridgeCommand.SetUnit1Target, temp))

def encode_set_unit2_target_command(temp: int) -> bytes:
    return create_packet(struct.pack('Bb', FridgeCommand.SetUnit2Target, temp))

# ── Fridge BLE class ──────────────────────────────────────────────────────────

class Fridge:
    on_query_response:        Optional[Callable[[FridgeData], Any]] = None
    command_characteristic:   Optional[BleakGATTCharacteristic] = None
    notify_characteristic:    Optional[BleakGATTCharacteristic] = None
    client:                   BleakClient = None
    verbose:                  bool = False
    _query_result_future:     Optional[Future] = None
    _set_result_future:       Optional[Future] = None
    _set_unit1_result_future: Optional[Future] = None
    _set_unit2_result_future: Optional[Future] = None
    _last_packet:             Optional[Union[bytes, bytearray]] = None

    def __init__(self, client: Union[BleakClient, BLEDevice, str], verbose: bool):
        if isinstance(client, str):
            # Wir zwingen BlueZ über ein direktes D-Bus-Argument dazu,
            # den internen Geräte-Cache zu ignorieren und direkt zu koppeln
            self.client = BleakClient(
                client,
                bluez_options={"connection_mode": "direct"}
            )
        else:
            self.client = client if isinstance(client, BleakClient) else BleakClient(client)

        self.verbose = verbose

    async def connect(self):
        import random  # zeitlicher Versatz

        for attempt in range(0, 5):
            try:
                await self.client.connect()
                break  # Erfolgreich verbunden -> Schleife beenden

            except Exception as e:
                # Fehlermeldung als Text
                error_msg = str(e)

                # Wenn BlueZ blockiert, greift dieser Block ohne NameError
                if "InProgress" in error_msg or "already in progress" in error_msg.lower():
                    if self.verbose:
                        tell(0, f"Bluetooth belegt (Versuch {attempt+1}/5). Warte kurz...")
                    # 2 bis 3 Sekunden warten, damit der hängengebliebene Scan im Kernel ausläuft
                    await asyncio.sleep(2.0 + random.uniform(0.0, 1.0))
                    continue  # Schleife springt zum nächsten Versuch

                # bekannten Verbindungsabbrüche abfangen
                if 'failed to discover services' in error_msg or 'Unreachable' in error_msg:
                    await asyncio.sleep(0.5)
                    continue

                # Ein völlig anderer Fehler? Dann normal hochwerfen
                raise

        else:
            # Letzter, ungeschützter Versuch, falls alle 5 Versuche fehlschlagen
            await self.client.connect()

        # Code für die GATT-Characteristics
        for _, c in self.client.services.characteristics.items():
            if c.service_uuid in (SERVICE_UID, SERVICE_UUID):
                if c.uuid in ('1235', TX_UUID):
                    self.command_characteristic = c
                elif c.uuid in ('1236', RX_UUID):
                    self.notify_characteristic = c

        if self.command_characteristic is None or self.notify_characteristic is None:
            await self.client.disconnect()
            raise ValueError('Required GATT characteristics not found')

        await self.client.start_notify(self.notify_characteristic, self._notify_callback)

    async def disconnect(self):
        if self.client and self.client.is_connected:
            try:
                await self.client.disconnect()
            except Exception as e:
                if self.verbose:
                    print(f"Fehler beim Trennen: {e}")

    async def __aenter__(self):
        try:
            await self.connect()
            return self
        except Exception:
            # Falls beim Connecten (z.B. beim Finden der Characteristics) etwas schiefgeht,
            # stellen wir sicher, dass die Verbindung sofort getrennt wird.
            await self.disconnect()
            raise

    async def __aexit__(self, exc_type, exc_value, traceback):
        # Nur trennen, wenn der Client existiert und verbunden ist
        if self.client:
            await self.disconnect()

    def _get_packet_data(self, data: Union[bytes, bytearray]) -> Optional[bytes]:
        self._last_packet = None
        if len(data) <= 2:
            return None
        if data[:2] != b'\xFE\xFE':
            return None
        pktlen = struct.unpack_from('B', data, 2)[0]
        if pktlen > len(data) - 3:
            self._last_packet = data
            return None
        if pktlen != len(data) - 3:
            return None
        csum    = struct.unpack_from('>H', data[-2:])[0]
        calcsum = sum(int(v) for v in data[:-2])
        if csum not in (calcsum, calcsum * 2):
            return None
        return data[3:-2]

    def _notify_callback(self, sender: BleakGATTCharacteristic, pkt: bytearray):
        if self.verbose:
            sys.stderr.write(f'Recv: {sender}: {pkt.hex()}\n')
        data = None
        if self._last_packet is not None:
            data = self._get_packet_data(self._last_packet + pkt)
        if data is None and self._last_packet is None:
            data = self._get_packet_data(pkt)
        if data is None or len(data) < 2:
            return
        cmd = struct.unpack_from('B', data, 0)[0]
        if cmd == FridgeCommand.Query:
            self._notify_query(decode_fridge_data(data[1:]))
        elif cmd == FridgeCommand.Set:
            self._notify_set(decode_fridge_data(data[1:]))
        elif cmd == FridgeCommand.SetUnit1Target:
            self._notify_set_unit1(struct.unpack_from('b', data, 1)[0])
        elif cmd == FridgeCommand.SetUnit2Target:
            self._notify_set_unit2(struct.unpack_from('b', data, 1)[0])

    def _notify_query(self, data: FridgeData):
        if self.on_query_response is not None:
            self.on_query_response(data)
        if isinstance(self._query_result_future, Future):
            self._query_result_future.set_result(data)

    def _notify_set(self, data: FridgeData):
        if isinstance(self._set_result_future, Future):
            self._set_result_future.set_result(data)

    def _notify_set_unit1(self, data: int):
        if isinstance(self._set_unit1_result_future, Future):
            self._set_unit1_result_future.set_result(data)

    def _notify_set_unit2(self, data: int):
        if isinstance(self._set_unit2_result_future, Future):
            self._set_unit2_result_future.set_result(data)

    async def _send(self, pkt: bytes):
        if self.verbose:
            sys.stderr.write(f'Send: {pkt.hex()}\n')
        await self.client.write_gatt_char(self.command_characteristic, pkt, response=True)

    async def query(self) -> FridgeData:
        loop = asyncio.get_event_loop()
        self._query_result_future = loop.create_future()
        await self._send(encode_query_command())
        return await self._query_result_future

    async def set(self, data: FridgeData) -> FridgeData:
        loop = asyncio.get_event_loop()
        self._set_result_future = loop.create_future()
        await self._send(encode_set_command(data))
        return await self._set_result_future

    async def set_unit1_target_temperature(self, temp: int) -> int:
        loop = asyncio.get_event_loop()
        self._set_unit1_result_future = loop.create_future()
        await self._send(encode_set_unit1_target_command(temp))
        return await self._set_unit1_result_future

    async def set_unit2_target_temperature(self, temp: int) -> int:
        loop = asyncio.get_event_loop()
        self._set_unit2_result_future = loop.create_future()
        await self._send(encode_set_unit2_target_command(temp))
        return await self._set_unit2_result_future

# ── BLE async helpers ─────────────────────────────────────────────────────────

async def get_fridge(mac: str) -> Fridge:
    """
    Aggressiver Verbindungsaufbau: Optimiert für das 3-Sekunden-Fenster der Alpicool
    und vollautomatisches Recovery im laufenden Betrieb.
    """
    global global_fridge

    # 1. Verbindung steht -> Direkt nutzen
    if global_fridge and global_fridge.client and global_fridge.client.is_connected:
        return global_fridge

    # 2. Verbindung wurde unterbrochen -> Recovery
    if global_fridge:
        if args.v >= 1:
            tell(0, "[INFO] Verbindung verloren. Starte automatisches Recovery...")
        try:
            await global_fridge.disconnect()
        except Exception:
            pass
        global_fridge = None

    # 3. Verbindungsversuch ohne Pausen
    #   3 Mal direkt hintereinander versuchen
    for attempt in range(1, 4):
        try:
            # Wir nutzen Ihre Fridge-Klasse mit dem erzwungenen "direct"-Modus
            global_fridge = Fridge(mac, args.v >= 3)
            await global_fridge.connect()

            tell(0, "Erfolgreich mit Kühlbox verbunden!")
            return global_fridge

        except Exception as e:
            error_msg = str(e).lower()

            # Falls BlueZ ganz kurz meldet, dass eine Operation läuft,
            # machen wir NUR eine minimale Pause
            if "inprogress" in error_msg or "already in progress" in error_msg:
                await asyncio.sleep(0.2)
                continue

            # Bei jedem anderen Fehler (z. B. Box noch nicht bereit / "not found")
            # warten wir ebenfalls nicht, sondern probieren es direkt in der Schleife noch mal.
            await asyncio.sleep(0.1)
            continue

    # Wenn alle schnellen Versuche fehlschlagen (Box ist aktuell aus oder außer Reichweite):
    global_fridge = None
    # kontrollierte Exception. Der Main-Loop fängt diese ab,
    # gibt "not found" aus und versucht es im nächsten Takt (args.i) vollautomatisch neu.
    raise Exception("Kühlbox aktuell nicht erreichbar.")

async def ble_query_persistent(mac: str) -> Optional[FridgeData]:
    global global_fridge_status
    try:
        fridge = await get_fridge(mac)
        status = await asyncio.wait_for(fridge.query(), 5.0)
        if status:
            global_fridge_status = status
        return status
    except Exception as e:
        error_msg = str(e)
        tell(0, f"BLE query error: {error_msg}")

        # reagieren auf hardware-absturz:
        if "no bluetooth adapters found" in error_msg.lower():
            tell(0, "[WARNUNG] Bluetooth-Stick abgestürzt! Erzwinge Hardware-Reset...")

            # Bluetooth-Treiber auf Kernel-Ebene neu starten
            os.system("hciconfig hci0 down && hciconfig hci0 up")

            # Falls das nicht reicht, den ganzen Dienst
            os.system("systemctl restart bluetooth")

            await asyncio.sleep(3.0)

            # Die alte, Instanz löschen, damit sie beim nächsten Durchlauf neu gebaut wird
            global global_fridge
            global_fridge = None

        return None

async def ble_set_field_persistent(mac: str, field: str, value) -> bool:
    """Sendet ein Command in Echtzeit über die offene Verbindung."""
    global global_fridge_status
    if global_fridge_status is None:
        tell(0, "Fehler: Kann Command nicht senden, noch kein Status-Update vorhanden.")
        return False

    try:
        fridge = await get_fridge(mac)

        # Lokale Kopie des Status modifizieren
        data = copy.deepcopy(global_fridge_status)
        setattr(data, field, value)

        # Direkt über die offene Verbindung rausballern (unter 0.1 Sekunden!)
        try:
            await asyncio.wait_for(fridge.set(data), 0.5)
        except asyncio.TimeoutError:
            pass # Timeout beim Senden ignorieren

        # Globalen Cache direkt aktualisieren
        setattr(global_fridge_status, field, value)
        return True
    except Exception as e:
        tell(0, f"BLE set_field error: {e}")
        return False

async def processCommand(s):
    address = int(s['address'])
    value   = s['value']
    mac     = args.M.strip()

    if address == 0:    # power on/off
        on = bool(int(value))
        tell(0, f"Set power {'On' if on else 'Off'}")
        await ble_set_field_persistent(mac, 'powered_on', on)
    elif address == 1:  # target temperature unit 1
        temp = int(value)
        tell(0, f"Set target temp {temp}°C")
        await ble_set_temp_persistent(mac, temp, unit=1)
    elif address == 4:  # mode Max/Eco
        eco = str(value).lower() in ('eco', '1', 'true')
        tell(0, f"Set mode {'Eco' if eco else 'Max'}")
        await ble_set_field_persistent(mac, 'run_mode', FridgeRunMode.Eco if eco else FridgeRunMode.Max)
    else:
        tell(0, f"Ignoring unknown address {address}")

async def ble_set_temp_persistent(mac: str, temp: int, unit: int = 1) -> bool:
    """Set the target temperature for unit 1 or 2 using the persistent connection."""
    try:
        # 1. Die bereits offene Verbindung holen (oder automatisch neu verbinden)
        fridge = await get_fridge(mac)

        # 2. Befehl absenden (Timeout 0.5s, da Alpicool oft nicht quittiert)
        try:
            if unit == 2:
                await asyncio.wait_for(fridge.set_unit2_target_temperature(temp), 0.5)
            else:
                await asyncio.wait_for(fridge.set_unit1_target_temperature(temp), 0.5)
        except asyncio.TimeoutError:
            # Timeout beim Schreiben ignorieren wir (normal bei Alpicool)
            pass

        # 3. Den lokalen globalen Cache aktualisieren, falls vorhanden,
        # damit das sofortige Update danach direkt den neuen Zielwert anzeigt.
        global global_fridge_status
        if global_fridge_status:
            if unit == 2 and hasattr(global_fridge_status, 'unit2'):
                global_fridge_status.unit2.target_temperature = temp
            elif hasattr(global_fridge_status, 'unit1'):
                global_fridge_status.unit1.target_temperature = temp

        return True

    except Exception as e:
        tell(0, f"BLE set_temp error: {e}")
        return False

async def ble_discover(timeout: float = 10.0) -> dict:
    found = {}
    try:
        results = await BleakScanner.discover(timeout=timeout, return_adv=True)
    except Exception as e:
        error_msg = str(e)
        if "InProgress" in error_msg or "already in progress" in error_msg.lower():
            print("\n[Fehler] Bluetooth-Adapter ist blockiert. Bitte führen Sie aus:")
            print("         systemctl restart bluetooth\n")
            sys.exit(1)
        raise e

    known_prefixes = ["WT-", "ALPI", "MANT", "FRIDG", "CAR-", "ICECO"]
    for addr, (device, adv) in results.items():
        name        = device.name or adv.local_name or "Unknown"
        name_upper  = name.upper()
        uuids_upper = [str(u).upper() for u in adv.service_uuids]
        if (any("FFE0" in u for u in uuids_upper) or
                any("1234" in u for u in uuids_upper) or
                any(p in name_upper for p in known_prefixes)):
            found[addr] = name
    return found

# ── homectld widget parameters ────────────────────────────────────────────────
# address → parameter JSON appended on initial publish

parameters = [None] * 7
# 0: Power (on/off status)
parameters[0] = '{"parameter": {"cloneable": false, "widgettype": 0, "symbol": "mdi:mdi-power", "symbolOn": "mdi:mdi-snowflake", "color": "gray", "colorOn": "rgb(3 169 244)"}}'
# 1: Target temperature (settable)
parameters[1] = '{"parameter": {"cloneable": false, "widgettype": 8}}'
# 2: Actual temperature (read-only)
parameters[2] = '{"parameter": {"cloneable": false, "widgettype": 6}}'
# 3: Battery voltage
parameters[3] = '{"parameter": {"cloneable": false, "widgettype": 3}}'
# 4: Mode Max/Eco
parameters[4] = '{"parameter": {"cloneable": false, "widgettype": 8}}'
# 5: Status/Error
parameters[5] = '{"parameter": {"cloneable": false, "widgettype": 2, "colorCondition": "0=green,>0=red"}}'
# 6: Compressor running?
parameters[6] = '{"parameter": {"cloneable": false, "widgettype": 0, "symbol": "mdi:mdi-engine-outline", "symbolOn": "mdi:mdi-engine-outline", "color": "gray", "colorOn": "rgb(3 169 244)"}}'

initial   = True
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
    tell(0, "Beende Skript sauber...")
    # den MQTT-Hintergrund-Thread stoppen, damit keine neuen Nachrichten kommen
    if args.m.strip() != '':
        mqtt.loop_stop()
    # KeyboardInterrupt um im Haupt-Thread asyncio.run zu beenden
    raise KeyboardInterrupt

signal.signal(signal.SIGTERM, shutdown)
signal.signal(signal.SIGINT, shutdown)

# ── MQTT callbacks ────────────────────────────────────────────────────────────

inTopic = ''

# --- MQTT Callbacks ---

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
        cmd_queue.put(s)  # Schiebt den Befehl sicher in die Queue für den Main-Loop
    except Exception as e:
        tell(0, f"onMessage parse error: {e}")

async def processCommand(s):  # Jetzt 'async def'
    address = int(s['address'])
    value   = s['value']
    mac     = args.M.strip()

    if address == 0:    # power on/off
        on = bool(int(value))
        tell(0, f"Set power {'On' if on else 'Off'}")
        await ble_set_field_persistent(mac, 'powered_on', on)

    elif address == 1:  # target temperature unit 1
        temp = int(value)
        tell(0, f"Set target temp {temp}°C")
        await ble_set_temp_persistent(mac, temp, unit=1)

    elif address == 4:  # mode Max/Eco
        eco = str(value).lower() in ('eco', '1', 'true')
        tell(0, f"Set mode {'Eco' if eco else 'Max'}")
        await ble_set_field_persistent(mac, 'run_mode', FridgeRunMode.Eco if eco else FridgeRunMode.Max)

    else:
        tell(0, f"Ignoring unknown address {address}")


# ── discover ──────────────────────────────────────────────────────────────────

if args.D:
    print("Discovering BT devices ..")
    found = asyncio.run(ble_discover(10))
    print(f"\n.. done, devices:")
    for addr, name in found.items():
        print(f"  {addr}  {name}")
    sys.exit(0)

# ── show / main loop guard ────────────────────────────────────────────────────

if args.M.strip() == "":
    print("Missing device MAC, add -M option")
    sys.exit(1)

if args.s:
    status = asyncio.run(ble_query_persistent(args.M.strip()))

    if status is None:
        print(f'Device "{args.M.strip()}" not found or not responding')
    else:
        print(f"Power:       {'On' if status.powered_on else 'Off'}")
        print(f"Compressor: {'On' if status.compressor_running else 'Off'}  ")
        print(f"Target temp: {status.unit1.target_temperature} °C")
        print(f"Actual temp: {status.unit1.current_temperature} °C")
        print(f"Status:      {status.error_code}  '{get_error_text(status.error_code)}' ")
        print(f"Battery:     {status.battery_voltage:.1f} V ({status.battery_charge_percent}%)")
        print(f"Mode:        {'Eco' if status.run_mode == FridgeRunMode.Eco else 'Max'}")
        if status.unit2 is not None:
            print(f"Unit 2 temp: {status.unit2.current_temperature} °C (target: {status.unit2.target_temperature} °C)")

    # Vor sys.exit(0), trennen der Verbindung damit der Linux-Bluetooth-Stack nicht im "InProgress"-Modus hängen bleibt
    if global_fridge and global_fridge.client:
        try:
            asyncio.run(global_fridge.disconnect())
        except Exception:
            pass

    sys.exit(0)

# ── MQTT connect ──────────────────────────────────────────────────────────────

if args.m.strip() != '':
    tell(0, 'Connecting to "{}:{}", topic "{}"'.format(args.m.strip(), args.p, args.T.strip()))
    mqtt = paho.Client("alpicool", protocol=paho.MQTTv311, clean_session=True)
    inTopic = args.T.strip() + '/in'
    mqtt.on_connect = onConnect
    mqtt.on_message = onMessage
    mqtt.connect(args.m.strip(), args.p, 60)
    mqtt.loop_start()  # Startet mqtt

# ── main loop ─────────────────────────────────────────────────────────────────

async def main_application():
    next_update_time = 0
    initial = True

    while True:
        command_processed = False

        # 1. Commands abarbeiten
        while not cmd_queue.empty():
            try:
                cmd = cmd_queue.get_nowait()
                await processCommand(cmd)
                command_processed = True
            except Exception as e:
                tell(0, f"Command error: {e}")

        # Wenn ein Befehl verarbeitet wurde, Update erzwingen
        if command_processed:
            tell(0, "Befehl erkannt, update...")
            next_update_time = 0
            current_time = time.time()
        else:
            current_time = time.time()

        # 2. Zeitprüfung für das reguläre Update
        if current_time >= next_update_time:
            tell(0, "Update ...")

            status = await ble_query_persistent(args.M.strip())

            if status is None:
                tell(0, f'Device "{args.M.strip()}" not found or not responding')
            else:
                tell(0, f"Power: {'On' if status.powered_on else 'Off'}  "
                     f"Compressor: {'On' if status.compressor_running else 'Off'}  "
                     f"Target: {status.unit1.target_temperature}°C  "
                     f"Actual: {status.unit1.current_temperature}°C  "
                     f"Battery: {status.battery_voltage:.1f}V  "
                     f"Status: {status.error_code}  '{get_error_text(status.error_code)}' "
                     f"Mode: {'Eco' if status.run_mode == FridgeRunMode.Eco else 'Max'}")

                stype = args.t.strip()
                publishMqtt({'type': stype, 'address': 0, 'state': status.powered_on,
                             'kind': 'status', 'title': 'Power', 'rights': 2})
                publishMqtt({'type': stype, 'address': 1, 'value': status.unit1.target_temperature,
                             'kind': 'value', 'unit': '°C', 'title': 'Target Temp',
                             'choices': temp_choices, 'rights': 2})
                publishMqtt({'type': stype, 'address': 2, 'value': status.unit1.current_temperature,
                             'kind': 'value', 'unit': '°C', 'title': 'Actual Temp'})
                publishMqtt({'type': stype, 'address': 3, 'value': status.battery_voltage,
                             'kind': 'value', 'unit': 'V', 'title': 'Battery'})
                publishMqtt({'type': stype, 'address': 4, 'text': 'Eco' if status.run_mode == FridgeRunMode.Eco else 'Max',
                             'kind': 'text', 'title': 'Mode', 'choices': 'Max,Eco', 'rights': 2})
                publishMqtt({'type': stype, 'address': 5, 'value': int(status.error_code), 'text': get_error_text(status.error_code),
                             'kind': 'text', 'title': 'Status'})
                publishMqtt({'type': stype, 'address': 6, 'state': status.compressor_running,
                             'kind': 'status', 'title': 'Compressor'})

            tell(0, "... done")
            initial = False

            # Nächstes Intervall
            next_update_time = time.time() + args.i
            continue

        # 3. Asynchrones Schlafen (gibt dem Bluetooth-Treiber Raum zum Atmen)
        await asyncio.sleep(0.1)

if __name__ == "__main__":
    try:
        # Startet den persistenten Loop
        asyncio.run(main_application())
    except KeyboardInterrupt:
        # trennen der Bluetooth-Verbindung im Kernel
        if global_fridge and global_fridge.client:
            try:
                asyncio.run(global_fridge.disconnect())
                tell(0, "Bluetooth-Verbindung erfolgreich geschlossen.")
            except Exception as e:
                tell(0, f"Fehler beim Bluetooth-Disconnect: {e}")

        if args.m.strip() != '':
            try:
                mqtt.disconnect()
                tell(0, "MQTT sauber getrennt.")
            except Exception:
                pass

        tell(0, "Skript exit")
