[← Back to the overview](../README.md)

# LUA Interface

homectld embeds a Lua 5.3 scripting engine that lets you compute values and control digital outputs directly inside the daemon — no external process needed.

## Where scripts are used

| Sensor type | Purpose |
|-------------|---------|
| **CV** (Calculated Value) | Compute a numeric, boolean, or text result from one or more other sensors |
| **DO** (Digital Output, Auto mode) | Decide automatically whether an output should be on or off |

Scripts are entered in the sensor's settings in the web interface (Setup → IO Setup → edit the sensor).

## When a script is called

A script is executed when **at least one sensor declared via `watch()` has changed its value** since the
last run of the script, or when the interval requested via `every(seconds)` has elapsed.

The very first run (at startup and after the script was saved) is always executed so that the `watch()`
and `every()` calls can be collected. From then on the daemon only re-runs the script when one of them is
due. A script declaring neither `watch()` nor `every()` is run every 60 seconds. A script with `watch()`
only runs on changes of the watched sensors, so a script that also depends on the passing of time (hold
times, timeouts, runtime counters) has to add an `every()`.

## Available globals

### `sensors` — the sensor map

All known sensors are available as a nested table:

```lua
sensors["TYPE"][address]
```

`TYPE` is the sensor type string (e.g. `"AI"`, `"DI"`, `"DO"`, `"CV"`, `"OW"`, `"WEA"`, …).
`address` is the numeric address of the sensor — you can write it as a **decimal** integer or as a **hex** literal:

```lua
sensors["AI"][1]       -- decimal address 1
sensors["DO"][0x02]    -- hex address 2  (both forms are equivalent)
```

#### Fields of a sensor entry

| Field | Type | Description |
|-------|------|-------------|
| `value` | number | Numeric measurement value |
| `state` | boolean | Boolean state (for status-kind sensors and digital outputs) |
| `text` | string | Text value |
| `kind` | string | `"value"`, `"status"`, `"text"`, or `"trigger"` |
| `last` | integer | Unix timestamp of the last received update |
| `changedAt` | integer | Unix timestamp of the last actual value change |
| `name` | string | Internal name |
| `title` | string | Display title |
| `unit` | string | Unit string (e.g. `"°C"`, `"V"`, `"%"`) |
| `valid` | boolean | `true` if the sensor has delivered at least one valid reading |

All fields are **read-only** — the script cannot modify sensor data directly.

### `watch(type, address)` — run on change

Declares a sensor the script depends on. The script is re-run as soon as that sensor has changed since
the last run (within 0.5 s, GPIO inputs immediately when read).

```lua
watch("GPIO", 0x0c)
```

### `every(seconds)` — run periodically

Requests a run in the given interval, in addition to the `watch()` dependencies and the forced run
once per minute. Useful for scripts that accumulate time or poll something; the resolution is one second,
the actual call happens within 0.5 s after the interval has elapsed.

```lua
every(10)         -- run at least every 10 seconds
```

### `tell(level, message)` — logging

Writes a message to the homectld log. `level` is one of the predefined constants:

| Constant | When visible |
|----------|-------------|
| `eloAlways` | always |
| `eloInfo` | info verbosity |
| `eloLua` | Lua verbosity |
| `eloDebug` | debug verbosity |

Lua handles the formatting itself via `string.format`:

```lua
tell(eloLua, "temperature is " .. sensors["AI"][1].value)
tell(eloAlways, string.format("value out of range: %.2f", sensors["AI"][1].value))
```

### `signal` — boolean

`true` when the script was triggered by an explicit signal/event, `false` during a regular cyclic evaluation.

## Return value

The script must `return` a value. The type of the returned value determines what is written back to the CV sensor:

| Lua return type | Effect on the sensor |
|-----------------|----------------------|
| `number` (integer) | `value` field is updated as integer |
| `number` (float) | `value` field is updated as float |
| `boolean` | `state` field is updated |
| `string` | `text` field is updated |
| no return / `nil` | nothing is written (warning logged) |

## Examples

### Average of two analog inputs

```lua
watch("AI", 1)
watch("AI", 2)

local a = sensors["AI"][1]
local b = sensors["AI"][2]

if not a.valid or not b.valid then
    return 0.0
end

return (a.value + b.value) / 2.0
```

### Digital output: turn on when temperature is above threshold

```lua
watch("AI", 0x01)

local temp = sensors["AI"][0x01]

if not temp.valid then
    return false
end

return temp.value > 25.0   -- true = ON, false = OFF
```

### Combine state and value into a status text

```lua
watch("DO", 1)
watch("AI", 3)

local pump = sensors["DO"][1]
local flow = sensors["AI"][3]

if pump.state then
    return string.format("running, flow %.1f l/min", flow.value)
else
    return "stopped"
end
```

### React to a signal event

```lua
watch("AI", 1)

if signal then
    return sensors["AI"][1].value * 2.0
end

return sensors["AI"][1].value
```

## Tips

- Always call `watch()` for every sensor the script reads — only watched sensors trigger re-evaluation.
- Use `sensor.valid` before accessing `sensor.value` or `sensor.state` to avoid stale data on startup.
- Both decimal (`1`) and hex (`0x01`) address literals work in `watch()` and in `sensors[...]` access.
- The standard Lua libraries (`math`, `string`, `table`, `os`, …) are available.

[← Back to the overview](../README.md)
