#!/usr/bin/env python3
# p4sim.py - simulator of the serial interface (COM1) of the Froeling S 3200
#
# Creates a pseudo terminal and answers the requests of p4mqttd / p4 like the control does,
# for tests without a heating. Only the commands needed for reading are implemented:
# check, state, version, value, value list, menu list, digital in/out, analog out and
# the error list, parameters (get and set), time programs (get and set), set date/time (the clock of the simulated control runs with the offset of --drift
# until it is set).
#
#   p4sim.py [--link /tmp/ttyP4SIM] [--delay 0.02] [--drift 120] [--log]
#   p4mqttd -n -t -d /tmp/ttyP4SIM ...
#
# The framing is the one of p4io.c: 02 FD <size:2> <command> <payload> <crc>, everything behind
# the id is masked (02 -> 02 00, 2B -> 2B 00, FE -> FE 00, 11 -> FE 12, 13 -> FE 14).

import argparse
import os
import select
import sys
import termios
import time
import tty

CHECK, GETVALUE, VALUES_FIRST, VALUES_NEXT = 0x22, 0x30, 0x31, 0x32
MENU_FIRST, MENU_NEXT, GETVERSION = 0x37, 0x38, 0x41
GETDIGOUT, GETANLOUT, GETDIGIN, GETSTATE = 0x44, 0x45, 0x46, 0x51
SETDATETIME, GETPARAMETER = 0x54, 0x55
SETPARAMETER, GETTIMESFIRST, GETTIMESNEXT, SETTIMES = 0x39, 0x42, 0x43, 0x50
GETERRORFIRST, GETERRORNEXT = 0x47, 0x48

# value specs: address, factor, type (1 = unsigned), unit, description
VALUES = [
    (0x0000, 10, 0, '°', 'Kesseltemperatur'),
    (0x0001, 10, 0, '°', 'Abgastemperatur'),
    (0x0002, 10, 1, '°', 'Puffer oben'),
    (0x0003, 10, 0, '°', 'Außentemperatur'),
    (0x0004, 1,  1, '%', 'Restsauerstoff [%]'),
    (0x0005, 100, 0, 'bar', 'Anlagendruck'),
]

# menu: type, parent, child, address, unit, description (only some entries are of interest)
MENU = [
    (0x2a, 1, 5, 0x0000, '', 'Kessel'),
    (0x16, 1, 0, 0x0200, '', 'Firmware'),
    (0x03, 1, 0, 0x0000, '', 'Kesseltemperatur:'),
    (0x2a, 1, 6, 0x0000, '', 'Ausgaenge'),
    (0x07, 5, 0, 0x01e0, '°', 'Kessel-Solltemperatur'),
    (0x08, 5, 0, 0x01e1, '', 'Zuendung freigegeben'),
    (0x2a, 5, 230, 0x0000, '', 'Zeiten'),
    (0x11, 6, 0, 0x0001, '', 'Saugzug'),
    (0x11, 6, 0, 0x0002, '', 'Zuendung'),
    (0x13, 6, 0, 0x0003, '', 'Tuer Kontakt'),
    (0x12, 6, 0, 0x0004, '', 'Saugzug Leistung'),
]

# parameters: address -> unit, digits, factor, value, min, max, default
PARAMS = {
    0x01e0: ['°', 0, 1, 78, 65, 85, 78],
    0x01e1: ['', 0, 1, 1, 0, 1, 1],
}

# error list: number, info, state (1 came, 4 gone, 2 acknowledged), time (y, m, d, h, min, s), text
ERRORS = [
    (16, 163, 1, (2025, 10, 28, 0, 15, 2), 'Raumaustragung kontrollieren'),
    (16, 162, 4, (2025, 10, 28, 0, 15, 5), 'Raumaustragung kontrollieren'),
    (16, 34,  2, (2025, 10, 29, 7, 55, 1), 'Raumaustragung kontrollieren'),
    (169, 201, 1, (2025, 11, 4, 17, 2, 49), 'Aschebox voll, bitte entleeren'),
    (169, 73,  2, (2025, 11, 4, 17, 16, 41), 'Aschebox voll, bitte entleeren'),
    (169, 72,  4, (2025, 11, 4, 17, 16, 42), 'Aschebox voll, bitte entleeren'),
]

# time programs: address -> [(from, to) x 4], times in 1/10 hour (60 -> 06:00), 0xff -> not set
TIMES = {a: [(0xff, 0xff)] * 4 for a in range(0xe0)}
for a in range(0x00, 0x07):
    TIMES[a] = [(60, 220), (0xff, 0xff), (0xff, 0xff), (0xff, 0xff)]

IO_STATE = {0x0001: (ord('A'), 1), 0x0002: (ord('A'), 0), 0x0003: (ord('A'), 1), 0x0004: (0xff, 62)}


def crc(data):
    c = 0
    for b in data:
        c ^= (b ^ ((b * 2) & 0xff))
    return c


def mask(data):
    out = bytearray()
    for b in data:
        if b in (0x02, 0x2b, 0xfe):
            out += bytes([b, 0])
        elif b == 0x11:
            out += bytes([0xfe, 0x12])
        elif b == 0x13:
            out += bytes([0xfe, 0x14])
        else:
            out.append(b)
    return bytes(out)


def unmask(data):
    out = bytearray()
    i = 0
    while i < len(data):
        b = data[i]
        if b in (0x02, 0x2b) and i + 1 < len(data) and data[i + 1] == 0:
            out.append(b)
            i += 2
        elif b == 0xfe and i + 1 < len(data):
            n = data[i + 1]
            out.append({0x12: 0x11, 0x14: 0x13, 0x00: 0xfe}.get(n, 0xfe))
            i += 2
        else:
            out.append(b)
            i += 1
    return bytes(out)


def frame(command, payload):
    head = bytes([0x02, 0xfd]) + (len(payload) + 1).to_bytes(2, 'big') + bytes([command])
    body = head + payload
    body += bytes([crc(body)])
    return body[:2] + mask(body[2:])


def text(s, size=None):
    b = s.encode('latin-1')
    return b if size is None else b[:size].ljust(size, b'\0')


class Sim:
    def __init__(self, delay, log, drift=0):
        self.drift = drift
        self.delay = delay
        self.log = log
        self.value_pos = 0
        self.menu_pos = 0
        self.times_pos = 0
        self.error_pos = 0

    def answer(self, cmd, payload):
        if cmd == CHECK:
            return frame(CHECK, text('Tescht ;-)'))

        if cmd == GETSTATE:
            return frame(GETSTATE, bytes([1, 3]) + text('Automatik;Heizen'))

        if cmd == GETVERSION:
            t = time.localtime(time.time() + self.drift)
            return frame(GETVERSION, bytes([0x50, 0x04, 0x00, 0x01, t.tm_sec, t.tm_min, t.tm_hour,
                                            t.tm_mday, t.tm_mon, (t.tm_wday + 1) % 7, t.tm_year - 2000]))

        if cmd == GETPARAMETER:
            addr = int.from_bytes(payload[:2], 'big')
            if addr not in PARAMS:
                return None
            unit, digits, factor, value, mn, mx, df = PARAMS[addr]
            return frame(cmd, bytes([0]) + addr.to_bytes(2, 'big') + text(unit, 1) + bytes([digits, 0, factor])
                         + value.to_bytes(2, 'big') + mn.to_bytes(2, 'big') + mx.to_bytes(2, 'big')
                         + df.to_bytes(2, 'big') + (0).to_bytes(2, 'big') + bytes([0]))

        if cmd in (GETERRORFIRST, GETERRORNEXT):
            if cmd == GETERRORFIRST:
                self.error_pos = 0
            if self.error_pos >= len(ERRORS):
                return frame(cmd, bytes([0]))
            number, info, state, (y, mo, d, h, mi, sec), txt = ERRORS[self.error_pos]
            self.error_pos += 1
            return frame(cmd, bytes([1]) + number.to_bytes(2, 'big') + bytes([info, state, sec, mi, h, d, mo, y - 2000]) + text(txt))

        if cmd == SETPARAMETER:
            addr = int.from_bytes(payload[:2], 'big')
            value = int.from_bytes(payload[2:4], 'big')
            if addr not in PARAMS:
                return None
            PARAMS[addr][3] = value
            one = frame(cmd, addr.to_bytes(2, 'big') + value.to_bytes(2, 'big'))
            return one + one                      # the control answers twice

        if cmd in (GETTIMESFIRST, GETTIMESNEXT):
            if cmd == GETTIMESFIRST:
                self.times_pos = 0
            addr = self.times_pos
            self.times_pos += 1
            body = bytes([1, 0, addr])
            for f, t in TIMES.get(addr, [(0xff, 0xff)] * 4):
                body += bytes([f, t])
            return frame(cmd, body)

        if cmd == SETTIMES:
            addr = int.from_bytes(payload[:2], 'big')
            if addr not in TIMES:
                return None
            ranges = []
            for n in range(4):
                w = int.from_bytes(payload[2 + 2 * n:4 + 2 * n], 'big')
                ranges.append((w >> 8, w & 0xff))
            TIMES[addr] = ranges
            body = bytes([0]) + addr.to_bytes(2, 'big')
            for f, t in ranges:
                body += bytes([f, t])
            return frame(cmd, body)

        if cmd == SETDATETIME:
            self.drift = 0
            return frame(SETDATETIME, bytes([0]))

        if cmd == GETVALUE:
            addr = int.from_bytes(payload[:2], 'big')
            for a, factor, typ, unit, desc in VALUES:
                if a == addr:
                    base = {0: 800, 1: 1500, 2: 600, 3: 85, 4: 90, 5: 150}[a]
                    raw = base + int(time.time()) % 7
                    if a == 3:
                        raw = -35 if typ == 0 else raw       # -3.5 degree (signed)
                    return frame(GETVALUE, (raw & 0xffff).to_bytes(2, 'big'))
            return None

        if cmd in (VALUES_FIRST, VALUES_NEXT):
            if cmd == VALUES_FIRST:
                self.value_pos = 0
            if self.value_pos >= len(VALUES):
                return frame(cmd, bytes([0]))
            a, factor, typ, unit, desc = VALUES[self.value_pos]
            self.value_pos += 1
            return frame(cmd, bytes([1]) + factor.to_bytes(2, 'big') + typ.to_bytes(2, 'big')
                         + text(unit, 2) + a.to_bytes(2, 'big') + text(desc) + b'\0')

        if cmd in (MENU_FIRST, MENU_NEXT):
            if cmd == MENU_FIRST:
                self.menu_pos = 0
            if self.menu_pos >= len(MENU):
                return frame(cmd, bytes([0]))
            typ, parent, child, a, unit, desc = MENU[self.menu_pos]
            self.menu_pos += 1
            return frame(cmd, bytes([1, typ, 0]) + parent.to_bytes(2, 'big') + child.to_bytes(2, 'big')
                         + bytes(18) + a.to_bytes(2, 'big') + (0).to_bytes(2, 'big') + text(desc) + b'\0')

        if cmd in (GETDIGOUT, GETDIGIN, GETANLOUT):
            addr = int.from_bytes(payload[:2], 'big')
            if addr not in IO_STATE:
                return None
            mode, state = IO_STATE[addr]
            return frame(cmd, bytes([mode, state]))

        return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--link', default='/tmp/ttyP4SIM')
    ap.add_argument('--delay', type=float, default=0.02)
    ap.add_argument('--drift', type=int, default=0, help='clock of the control is off by this many seconds')
    ap.add_argument('--log', action='store_true')
    args = ap.parse_args()

    master, slave = os.openpty()
    tty.setraw(master)
    tty.setraw(slave)
    name = os.ttyname(slave)

    if os.path.islink(args.link):
        os.unlink(args.link)
    os.symlink(name, args.link)
    print('simulating the S 3200 at %s (%s)' % (args.link, name), flush=True)

    sim = Sim(args.delay, args.log, args.drift)
    buf = bytearray()

    while True:
        r, _, _ = select.select([master], [], [], 1.0)
        if not r:
            continue
        try:
            data = os.read(master, 4096)
        except OSError:
            time.sleep(0.2)
            continue
        buf += data

        while True:
            start = buf.find(b'\x02\xfd')
            if start < 0:
                buf.clear()
                break
            del buf[:start]
            # the size is masked, so read stepwise: unmask what we have
            raw = bytes(buf[2:])
            plain = unmask(raw)
            if len(plain) < 3:
                break
            size = int.from_bytes(plain[:2], 'big')
            if len(plain) < 3 + size:
                break
            cmd = plain[2]
            payload = plain[3:3 + size - 1]
            got_crc = plain[3 + size - 1]
            want = crc(bytes([0x02, 0xfd]) + plain[:3 + size - 1])

            # consume exactly the bytes of this request from the raw buffer
            consumed = 0
            count = 0
            while count < 3 + size and consumed < len(raw):
                b = raw[consumed]
                consumed += 2 if (b in (0x02, 0x2b, 0xfe) and consumed + 1 < len(raw)) else 1
                count += 1
            del buf[:2 + consumed]

            if got_crc != want:
                print('bad crc for command 0x%02x' % cmd, flush=True)
                continue

            answer = sim.answer(cmd, payload)
            if args.log:
                print('cmd 0x%02x %s -> %s' % (cmd, payload.hex(), 'answer' if answer else 'none'), flush=True)
            if answer:
                time.sleep(args.delay)
                os.write(master, answer)


if __name__ == '__main__':
    main()
