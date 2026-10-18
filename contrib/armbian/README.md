# Armbian Board Setup Scripts

Helper scripts and patches for Armbian-based single-board computers
(ODROID N2+, ODROID M1).

## Scripts

### `enable-header-dtd.sh`

Enables I2C on the 40-pin header pins 3 (SDA) / 5 (SCL) and patches the
Device Tree for additional peripherals.

Supported boards:

| Board      | Method                                              |
|------------|-----------------------------------------------------|
| ODROID N2+ | DTB patch — enables `i2c@1d000` (GPIOX\_17/18)     |
| ODROID M1  | Armbian overlay (`i2c0`) + GPIO pull-up overlay     |

For the **ODROID N2+** the script also patches the DTB to enable 1-Wire on
header pin 15 (GPIOX\_14) with `linux,open-drain` so that the `w1-gpio`
driver loads correctly.

```bash
sudo bash enable-header-dtd.sh
# reboot afterwards
```

With `--uart1` (ODROID M1 only) the script also enables UART1 of the header, see below.

Requires: `device-tree-compiler` (`apt install device-tree-compiler`)

---

### UART1 on the ODROID M1 (serial module on the header)

For a GPS module or any other serial device on the 40-pin header (e.g. Radiolink SE100 / TS100 with
u-blox M10, NMEA at 38400 baud, 3.3 V logic, see `gps.u-blox/README.md`) UART1 is enabled with the Armbian overlay
`rockchip-rk3568-hk-uart1`: `sudo bash enable-header-dtd.sh --uart1` adds `uart1` to the `overlays=`
line of `/boot/armbianEnv.txt` (the `overlay_prefix` is `rockchip-rk3568-hk`), e.g.

```
overlays=gpio-pullups onewire i2c0 uart1
```

After the reboot the port shows up as `/dev/ttyS1` (`dmesg | grep ttyS`: `fdd50000.serial: ttyS1`),
the serial console stays on `ttyS2`, no getty runs on `ttyS1`. Header pins: 8 TXD, 10 RXD (3.3 V),
5 V on pin 2 / 4, GND on pin 6 - check against the Hardkernel wiki before wiring. The GPS service then
runs with `GPS_DEVICE="-d /dev/ttyS1 -b 38400"` in `/etc/default/gps2mqtt`; the compass of such a module (I2C) goes
on the `i2c0` bus of the header enabled above.

---

### `check-header-dtd.sh`

Checks whether I2C is properly configured on the 40-pin header and reports
the status of i2c-tools, the active DTB node and detected bus numbers.
Useful for verifying the setup before or after running `enable-header-dtd.sh`.

```bash
sudo bash check-header-dtd.sh
```

---

## Subdirectories

### `armbian-n2+w1gpio-fix/`

DKMS patch for the `w1-gpio` kernel module on ODROID N2+ (Amlogic G12B).
Required because the stock `w1-gpio` driver uses non-sleeping GPIO calls
which silently fail on the Amlogic GPIO controller in kernel 6.18+.

See [`armbian-n2+w1gpio-fix/README.md`](armbian-n2+w1gpio-fix/README.md) for details.
