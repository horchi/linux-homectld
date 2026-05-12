
# Bluetooth Audio Setup (BR/EDR Devices)

For exampe: JBL Flip Essential
Tested on:  Odroid M1, Armbian Noble (Ubuntu 24.04), BlueZ 5.72

---

## Overview

BR/EDR audio devices (e.g. JBL Flip Essential) require:

1. **PulseAudio** running with bluetooth modules loaded — provides the A2DP profile to BlueZ
2. **Initial pairing** via a single `bluetoothctl` session (separate invocations do not share the device cache)
3. **Trust** set on the device so BlueZ auto-accepts reconnects

---

## 1. PulseAudio Autostart at Boot

Create a system service that starts PulseAudio as root before bluetooth connections are attempted.
`/run/user/0` must exist before PulseAudio starts — `ExecStartPre` handles that.

```bash
cat > /etc/systemd/system/pulseaudio.service << 'EOF'
[Unit]
Description=PulseAudio Sound System
After=bluetooth.service
Wants=bluetooth.service

[Service]
Type=notify
User=root
ExecStartPre=/bin/mkdir -p /run/user/0
ExecStartPre=/bin/chmod 700 /run/user/0
ExecStart=/usr/bin/pulseaudio --daemonize=no --log-target=journal
Restart=on-failure
RestartSec=5
Environment=HOME=/root
Environment=XDG_RUNTIME_DIR=/run/user/0
Environment=DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable --now pulseaudio
```

Squeezelite and other PA clients connect via `/run/user/0/pulse/native`:

```bash
# /etc/systemd/system/squeezelite.service.d/pulse.conf
[Service]
Environment=PULSE_SERVER=unix:/run/user/0/pulse/native
```

Verify:
```bash
systemctl status pulseaudio
pactl info | head -3
```

---

## 2. Initial Pairing

The JBL must be **in pairing mode** (hold Bluetooth button ~3 seconds until LED blinks fast).

`bluetoothctl scan bredr` only works inside a single interactive session — run everything in one pipe:

```bash
(
    echo "scan bredr"
    sleep 20
    echo "pair 04:CB:88:B9:46:49"
    sleep 5
    echo "quit"
) | bluetoothctl
```

Then trust the device (required for connect to succeed):

```bash
bluetoothctl trust "04:CB:88:B9:46:49"
```

Verify the link key was persisted:

```bash
grep -A3 "LinkKey" /var/lib/bluetooth/$(bluetoothctl show | awk '/Controller/{print $2}')/04:CB:88:B9:46:49/info
```

---

## 3. Connect / Disconnect

```bash
bluetoothctl connect    "04:CB:88:B9:46:49"
bluetoothctl disconnect "04:CB:88:B9:46:49"
```

Status check:

```bash
bluetoothctl info "04:CB:88:B9:46:49" | egrep "(Paired|Trusted|Connected)"
```

---

## 4. Re-Pairing (after JBL power cycle)

Some JBL devices clear their link key when powered off. If `connect` fails with
`br-connection-profile-unavailable` or `Connect Failed (status 0x04)`:

1. Put JBL in pairing mode (hold Bluetooth button ~3 sec)
2. Remove stale entry and re-pair:

```bash
bluetoothctl remove "04:CB:88:B9:46:49"
(
    echo "scan bredr"
    sleep 20
    echo "pair 04:CB:88:B9:46:49"
    sleep 5
    echo "quit"
) | bluetoothctl
bluetoothctl trust "04:CB:88:B9:46:49"
bluetoothctl connect "04:CB:88:B9:46:49"
```

---

## 5. homectld Script (bt-switch.sh)

Located at `configs/bt-switch.sh`. Configure in IO Setup with JSON parameter:

```json
{ "mac": "04:CB:88:B9:46:49", "name": "JBL Wohnmobil" }
```

- `name` — optional display name (fallback: device name from BlueZ, then MAC)
- `toggle` — disconnects if connected, pairs+connects if disconnected
- `status` — publishes current state via MQTT

---

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| `br-connection-profile-unavailable` | PulseAudio not running | `systemctl --user start pulseaudio` |
| `Device not available` | bluetoothd cache empty | Use single-session pipe approach (§2) |
| `Connect Failed (status 0x04)` | JBL not in connectable mode / powered off | Power on JBL, put in pairing mode |
| `Paired: no` after successful pair | Link key not persisted | Check `grep LinkKey` in info file |
| `scan on` doesn't find BR/EDR device | Default scan is LE only | Use `scan bredr` |
