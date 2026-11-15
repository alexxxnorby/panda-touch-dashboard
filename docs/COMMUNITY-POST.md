# Draft: Panda Touch Dashboard — a Home Assistant touchscreen for 3D printers

I have been repurposing a BIGTREETECH Panda Touch as a Home Assistant dashboard for 3D printers and workshop controls, using ESPHome and LVGL. All printer data and commands go through Home Assistant. This project is **vibe coded with OpenAI Codex**, with iterative testing on my actual panel.

It has workshop switches and sensors, two Bambu printer pages, laser power and shared wall-fan controls, plus a clock and weather screen. English and German are supported, including a QWERTZ keyboard with umlauts. Night mode dims the display and turns the text soft red.

The settings live on the panel: labels, entity mappings, icons, ordering and display options can be changed without rebuilding the firmware.

Repository: https://github.com/alexxxnorby/panda-touch-dashboard

This is an early preview tested on one panel, not a stock-firmware add-on. It uses Home Assistant and expects the Bambu integration's entity conventions. Existing ESPHome OTA is tested; a stock-to-ESPHome installation walkthrough still needs validation. The source is provided with generic mappings so you can use your own installation.

I would welcome feedback on the layout, German keyboard, other Panda Touch revisions and a reproducible first-install process.

[Add current photos of the Home, printer, fan and German keyboard screens before posting.]

Suggested destination: https://community.home-assistant.io/c/projects/9
