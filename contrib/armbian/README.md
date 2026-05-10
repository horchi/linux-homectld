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

Requires: `device-tree-compiler` (`apt install device-tree-compiler`)

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
