# Widget Defaults

When a sensor brings no widget settings of its own, homectld takes the defaults of a new widget (widget
type, scale, peak, colors, symbols) from two JSON files in `/etc/homectld`. They also define the title and
symbol of the sensor groups (value types) shown in the WEBIF (list, IO setup, alerts).

| File | Content |
|------|---------|
| `widgetDefaults.json` | delivered with homectld, **replaced on each install** - don't edit it |
| `widgetDefaults-usr.json` | your own rules, installed (empty) only if missing, **never overwritten** |

The rules of the user file take precedence over the delivered ones. The files are read at the start of
homectld, after a change restart it (`systemctl restart homectld`). Errors (invalid JSON, unknown widget
type) are written to the log.

These are only the defaults: settings made in the WEBIF at the widget and the `PARAMETER` of a sensor
(e.g. by script sensors) take precedence.

## Format

```json
{
  "default": { "widgettype": "Meter", "scalemin": 0, "scalemax": 45, "scalestep": 10, "showpeak": false,
               "color": "rgb(255, 255, 255)", "colorOn": "rgb(235, 197, 5)" },
  "unitWidgetTypes": { "°C": "MeterLevel", "txt": "Text", "*": "Meter" },
  "properties": [
    { "type": "DZS", "unit": "°C", "widgettype": "MeterLevel", "scalemax": 45, "scalestep": 10, "showpeak": true },
    { "type": "HMB", "unit": "*",  "widgettype": "SymbolValue", "color": "rgb(0 99 161)" }
  ],
  "symbols": [
    { "title": ["Pump"], "on": "img/icon/pump-on.gif", "off": "img/icon/pump-off.png" },
    { "type": ["TPMS"], "unit": ["bar"], "symbol": "mdi:mdi-car-tire-alert" }
  ],
  "valueTypes": [
    { "type": "^VA$", "title": "Messwerte",     "symbol": "mdi:mdi-chart-line" },
    { "type": "^DZS", "title": "DECONZ Sensoren", "symbol": "mdi:mdi-zigbee" }
  ]
}
```

### default

Used for a sensor no entry of `properties` matches. `color` / `colorOn` are also the colors of all
entries of `properties` without their own.

### properties

The first entry matching wins:

| Element | Meaning |
|---------|---------|
| `type` | sensor type (exact, e.g. `DZS`, `SC`, `W1`) |
| `unit` | unit of the sensor, `*` = any |
| `address` | optional, only this address of the type, missing = any |
| `widgettype` | `Symbol`, `Chart`, `Text`, `Value`, `Gauge`, `Meter`, `MeterLevel`, `PlainText`, `Choice`, `SymbolValue`, `Spacer`, `Time`, `SymbolText`, `BarChart`, `SpecialSymbol`, `Level`, `Balance`, `Timer`, `HTML` - missing: by the unit (`unitWidgetTypes`) |
| `scalemin` / `scalemax` / `scalestep` | scale of meters / charts, missing = 0 |
| `showpeak` | show the peak, missing = false |
| `color` / `colorOn` | missing = those of `default` |

So order matters: the entries of a type with a certain unit before its `*` entry.

### unitWidgetTypes

The widget type by the unit, for entries without `widgettype`; `*` for all other units.

### symbols

The symbol / image for off (`symbol`, `imgoff`) and on (`symbolOn`, `imgon`). The first rule matching wins:

- `type`, `unit`: list of sensor types / units (exact), `title`: list of parts of the title (case
  insensitive, e.g. `"Pump"` matches `Poolpumpe` too)
- all criteria given must match, a list any of its entries, a rule without criteria matches always
- `on` / `off`: an image (`img/icon/...` of the htdocs) or an MDI symbol (`mdi:mdi-...`), `symbol` for both

### valueTypes

Title and symbol of the sensor groups: the sensors are grouped by their type (e.g. `DZS`, `W1`, `VA`), the
first entry whose `type` (a regular expression) matches the sensor type wins. Several types can share a
title, they are shown as one group then (e.g. `^DO` and `^MCPO` as 'Digitale Ausgänge'). A type no entry
matches is shown with the type as title and without symbol.

## User file

`widgetDefaults-usr.json` has the same format, all elements optional:

- `properties`, `symbols` and `valueTypes` are checked **before** the delivered ones, an entry for the
  same type / unit replaces the delivered one
- the elements of `default` and `unitWidgetTypes` replace those of the delivered ones (the others stay)

Example: room temperatures of deCONZ as value, own symbol for the heating, other default color, own
title of the deCONZ sensor group:

```json
{
  "default": { "colorOn": "rgb(80, 200, 120)" },
  "properties": [
    { "type": "DZS", "unit": "°C", "widgettype": "Value", "showpeak": true }
  ],
  "symbols": [
    { "title": ["Heizung", "Heating"], "symbol": "mdi:mdi-radiator" }
  ],
  "valueTypes": [
    { "type": "^DZS", "title": "Zigbee Sensoren", "symbol": "mdi:mdi-zigbee" }
  ]
}
```

A rule of `symbols` without criteria in the user file matches all sensors, then none of the delivered
symbol rules is used any more.
