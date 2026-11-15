# Entity mapping

Open **Settings → Entities** and configure each row. Names are free text; entity IDs must be actual Home Assistant IDs. Examples in the source are placeholders.

| Slots | Expected input |
| --- | --- |
| Switch 1–6 | `switch`, `light`, `input_boolean` or a script action; script tiles can link a separate read-only state entity |
| Sensor 1–3 | A numeric sensor; units are read from Home Assistant |
| Weather | A `weather` entity with a temperature attribute |
| Laser 1 / Laser 2 power | A Home Assistant switch controlling each power source |
| Shared extractor / wall fan | The Home Assistant switch that powers the fan |
| P1S / X2D camera | Camera entity with an accessible `entity_picture` proxy path |
| Printer 1 / Printer 2 entity prefix | Entity object prefix, without a domain or field suffix |
| Printer 1 / Printer 2 AMS prefix | AMS object prefix, without a domain or tray suffix |
| Home Assistant URL | Base URL reachable from the panel, such as `http://homeassistant.local:8123` |
| Printer 1 / Printer 2 device ID | Home Assistant device-registry ID, needed for printer action calls |

The printer titles currently remain P1S and X2D in the UI. The printer integration conventions are fixed in this preview. For example, `printer_1` expects `sensor.printer_1_print_status`, `sensor.printer_1_nozzle_temperature` and `binary_sensor.printer_1_online`. Confirm the exact names and attributes produced by your integration before expecting the printer page to populate.

The optional entity-search helper is supplied as a script fragment. Merge it into your existing scripts configuration; do not replace your whole scripts file. Its callback defaults to `esphome.panda_workshop_entity_search_results`, matching the supplied node name `panda-workshop`. If you rename that node, find the new callback action in Home Assistant and update the helper. The helper has not been exercised against another installation.

Custom labels are retained exactly as entered. The German keyboard permits umlauts in names; entity IDs still use Home Assistant's allowed ID characters.
