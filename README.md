# Panda Touch Dashboard

**Vibe coded with OpenAI Codex**, developed iteratively on a real workshop panel.

**A Home Assistant dashboard for 3D printers and workshop controls, running on the BIGTREETECH Panda Touch.** It connects to HA through ESPHome: Home Assistant provides the printer data, switch and sensor states, weather and control actions. Configure your devices and integrations in HA, then use the panel to see their status and control them.

The panel handles the touchscreen, layout, clock, night mode and saved display settings locally. Printer, laser, fan and other device commands go through Home Assistant. It requires HA to provide these functions; it does not connect directly to printers or replace Home Assistant.

**Early preview, tested on one physical panel.** This replaces the stock Panda Touch application. It communicates through Home Assistant; it is not a standalone Bambu client. It is an independent community project, unaffiliated with BIGTREETECH, Bambu Lab or Home Assistant.

## What it does

- Six configurable switch slots and three sensor slots, with icons, thresholds and saved ordering.
- Two printer pages with progress, temperatures, AMS information and fault notices.
- Laser power controls and a shared wall-fan switch showing only its name, icon and state.
- Idle clock, weather, optional wallpaper and adjustable card transparency.
- English and German UI, including a German QWERTZ keyboard with umlauts and ß.
- Scheduled dimming and soft red night text; normal colours return in the morning.
- On-panel entity mapping, Wi-Fi setup and display preferences saved across restarts.

## Hardware and software

The supplied wiring targets the tested ESP32-S3 Panda Touch with 16 MB flash, octal PSRAM, an 800 × 480 RGB display and GT911 touch. Check your board revision before using it. Other displays and Panda products have not been tested.

Use ESPHome **2026.10.2**, a Home Assistant instance and the ESPHome integration. Printer pages expect the entity/attribute conventions of the Bambu Lab Home Assistant integration. Manual motion additionally depends on that integration exposing the required actions and the printer reporting LAN Only + Developer Mode. Camera images must be accessible to the panel without an interactive login.

## Get started

1. Install Python and the pinned ESPHome version in a virtual environment:

   ```sh
   python -m venv .venv
   # Activate .venv using the command for your shell.
   python -m pip install -r requirements.txt
   ```

2. Copy `firmware/secrets.example.yaml` to `firmware/secrets.yaml`. Generate a unique API encryption key and set unique OTA and setup-network passwords. Never publish that file or a binary containing it.
3. Set `timezone` at the top of `firmware/workshop.yaml`. The default is Europe/Berlin. The font downloads from Google Fonts at build time; set `font_file` to a local TrueType font with German glyphs to build without that download.
4. Validate and compile:

   ```sh
   python -m esphome config firmware/workshop.yaml
   python -m esphome compile firmware/workshop.yaml
   ```

5. For an already ESPHome-enabled panel, upload to its IP address:

   ```sh
   python -m esphome upload firmware/workshop.yaml --device PANEL_IP
   ```

   First installation from stock firmware requires a confirmed recovery/flashing procedure for your hardware revision. This preview does not yet provide a verified stock-to-ESPHome walkthrough. See the [manufacturer's project](https://github.com/bigtreetech/PandaTouch) and preserve a way back to stock firmware before proceeding. The existing ESPHome OTA procedure has been tested; stock first installation is a release-documentation gap.

6. Connect Wi-Fi using the panel's Wi-Fi settings or its `Workshop Setup` fallback network. Add the panel in Home Assistant's ESPHome integration using your API encryption key. Enable the integration option that allows this device to perform Home Assistant actions if you want its controls to work.
7. See [the entity mapping guide](docs/ENTITY-MAPPING.md), then open **Settings → Entities** on the panel. Replace the generic examples with your own entity IDs, printer/AMS prefixes and device IDs. Entity prefixes are the part after `sensor.` and before the attribute suffix; inspect your actual entities rather than assuming the example names match.

## Customising the panel

**Display** contains language, size, visibility, opacity, background, night hours and icon options. Hold a switch or sensor row, then drag to reorder it. Extra enabled controls appear on an additional page. Custom labels stay as you entered them.

Choose **Fan / Ventilator** in the icon picker for wall-fan switch tiles. On the laser page the shared fan already has a fan icon. These tiles control a Home Assistant switch; selecting an icon does not add fan speed controls.

Scripts can use a separate read-only state entity so the tile follows the real device state. Sensor thresholds use native units and turn amber only when strictly exceeded. Offline/unknown values are not treated as successful commands.

The optional `homeassistant/entity-search.yaml` helper enables on-panel searches; exact entity IDs can always be entered manually. Check the callback action name against the action registered by your own ESPHome device before enabling the helper.

## Verification and limits

The personalised build has been compiled, installed by encrypted OTA and checked on one physical panel. Its 21 icon/state logic cases and 412 font-glyph checks passed; Wi-Fi and Home Assistant reconnection were confirmed. The distributable uses generic mappings and an original neutral background. It must be configured for each installation. New keyboard touch behaviour and appearance still need feedback across real panels.

Known limitations: fixed two-printer/two-laser layout, Bambu integration naming assumptions, no fan speed control, no verified stock-firmware installation guide, and no compatibility claim for other hardware revisions. No universal firmware binary is distributed because builds contain installation-specific credentials.

Printer motion requires confirmed homing and local control, and extrusion requires a warm nozzle. Stay at the printer for motion operations. Laser tiles only switch power through Home Assistant; they do not replace machine interlocks or extraction controls.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Please include hardware revision, ESPHome version, language and reproduction steps when reporting a problem. Remove credentials and device identifiers from logs and screenshots.

## License

The project source and original neutral background are available under the [MIT license](LICENSE). Third-party dependencies keep their own licenses. The personal wallpaper, credentials and compiled device firmware are excluded.
