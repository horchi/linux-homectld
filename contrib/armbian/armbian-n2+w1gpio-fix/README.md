# w1-gpio-cansleep — DKMS patch for ODROID N2+ (Amlogic G12B)

## Problem

On the ODROID N2+ running Armbian with kernel 6.18+, the 1-Wire `w1-gpio`
driver fails silently. The Amlogic G12B GPIO controller marks its pins as
`can_sleep`, but the `w1-gpio` driver uses `gpiod_set_value()` /
`gpiod_get_value()` — the non-sleeping variants — which issue a kernel `WARN`
and return immediately without toggling the pin. This breaks the 1-Wire timing
completely and no sensors are detected.

## Fix

A patched replacement (`w1_gpio_cansleep.c`) that uses `gpiod_set_value_cansleep()`
and `gpiod_get_value_cansleep()` throughout. It is installed via DKMS so it
survives kernel updates automatically.

## Requirements

- ODROID N2+ with Armbian (kernel 6.18, `meson64`)
- DTB patched with a `/onewire` node — use `armbian-enable-header-i2c.sh`
  from this repository (it includes the 1-Wire DTB patch for header pin 15)

## Installation

```
make install
```

This installs `dkms`, registers the module as a DKMS package and builds +
installs it for the running kernel. On every subsequent kernel update DKMS
rebuilds the module automatically.

## Removal

```
make uninstall
```

## Files

| File                  | Description                              |
|-----------------------|------------------------------------------|
| `w1_gpio_cansleep.c`  | Patched driver source                    |
| `dkms.conf`           | DKMS package configuration               |
| `Makefile`            | Build, install and uninstall targets     |
