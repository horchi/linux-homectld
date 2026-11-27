# Idea: user defined HASP widget templates

Status: idea only, nothing implemented.

HTML widgets can't be shown on the openHASP panel (no browser, only LVGL objects), the panel shows them as
plain value (`haspTypeOfDashboardType()` maps unknown types to `hwtValue`). Rendering them server side to
PNG (headless Chromium) was dropped: too much load (RAM) and no touch.

Instead: user defined widget types as JSON templates. The template describes the openHASP objects with
coordinates relative to the card, homectld fills in position, ids and values and sends them as JSONL.

## Template

```json
{
  "name": "Tankbalken",
  "sensors": [ "Füllstand" ],
  "objects": [
    { "lid": 1, "obj": "label", "x": "0", "y": "0", "w": "w", "h": "h*0.4",
      "text": "${title}", "text_font": 16, "align": "center" },
    { "lid": 2, "obj": "bar", "x": "w*0.05", "y": "h*0.5", "w": "w*0.9", "h": "h*0.2",
      "min": "${s1.min}", "max": "${s1.max}", "val": "${s1.value}",
      "bg_color2": "${s1.value} < 20 ? '#ff0000' : '#00ff00'" },
    { "lid": 3, "obj": "label", "parent": 2, "x": "0", "y": "0", "w": "w*0.9", "h": "h*0.2",
      "text": "${s1.value} ${s1.unit}" }
  ]
}
```

- `sensors`: the slots of the template, each slot is assigned to a sensor in the setup (multiple sensors per
  widget)
- `lid`: local object id, mapped to `base + lid`, the same for `parent`
- coordinates relative to the card (`w`, `h`), converted to absolute pixels
- placeholders: `${title}`, `${sN.value}`, `${sN.unit}`, `${sN.min}` / `${sN.max}`, colors of the widget
  options, ...
- expressions evaluated by Lua (already in homectld, `lib/lua.h`), no own parser
- optional `"action": "toggle"` per object, bound to the existing click handler

## homectld (hasp.c)

- templates in a DB table (like `htmlwidgets`), loaded at start and after storing
- new type `hwtTemplate`, the card option `template` selects the template
- on create: evaluate all properties, publish the objects (`haspPublishJsonl()`)
- remember which properties depend on which sensor; register a `HaspObjRef` per used sensor in
  `haspObjects` (already `key -> vector<HaspObjRef>`)
- on update of a sensor: re-evaluate only its dependent properties, send only those

## WEBIF

- *Setup -> HASP Panel*: the templates are offered as additional types; per template slot a sensor select
- template editor: JSON in CodeMirror (already in `htdocs/lib/codemirror`), checked on store (error with line)
- preview on the real panel: send the template with test values to a preview page of the panel, show the
  existing panel screenshot of `hasp.js` (exact, the panel renders; 1-2 s, needs the panel online)

## Effort (rough)

| Part                                               | Lines    |
|----------------------------------------------------|----------|
| core: load, placeholders, Lua expressions, layout  | 300-500  |
| multiple sensors per widget                        | 150-250  |
| selection in Setup -> HASP                         | 50-100   |
| JSON editor with check                             | 50-100   |
| preview on the panel                               | 80-150   |
| **total**                                          | 700-1100 |

## Out of scope (too complex)

- drag & drop designer (WYSIWYG)
- exact preview in the browser (LVGL as WASM / openHASP simulator); an approximated browser preview (divs /
  SVG, ~200-300 lines) maybe later
- logic in templates: loops, objects appearing / disappearing depending on the value, animations - the set of
  objects is fixed, only properties are expressions
- rebuilding the existing types (Balance, Timer, Choice) as templates, they stay C++
