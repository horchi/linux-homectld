[← Back to the overview](../README.md)

<a id="tpms"></a>
# Tire Pressure (TPMS)

The pressure, temperature and battery of Bluetooth (BLE) valve caps are received by an ESP32 (sketch `tpms/`)
and sent to homectld via MQTT (sensor type `TPMS`, two sensors per cap: pressure and temperature, addressed by
the ID printed on the cap). The ESP32 only listens passively (no connection, no pairing), the caps keep working
with their app at the same time.

Supported are caps compatible to the apps like **LYTPMS / ITPMS (K)** (BLE manufacturer data with company id 172), e.g.

- WODHMIEY BLE valve caps, battery replaceable - https://www.amazon.de/dp/B0GWH88YSV (tested)
- valve caps for the same app, 0 - 6.8 bar, CR1632, sets of 2 / 3 / 4 - https://www.amazon.de/dp/B0DPMT9VKS (not tested yet)

Take care when buying: many sets use 433 MHz instead of Bluetooth, the listing has to state Bluetooth / the app itself.

Build, flashing (USB or OTA), the configuration (`wheels`, `learn`, ...) and the data format are described in
[tpms/README.md](../tpms/README.md).

[← Back to the overview](../README.md)
