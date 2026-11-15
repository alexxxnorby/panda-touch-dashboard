# Contributing

Start with a small issue describing the behaviour and your hardware revision. Keep code changes scoped and preserve saved preference layouts and existing icon selection values.

Before proposing a change, validate and compile `firmware/workshop.yaml` with ESPHome 2026.10.2 and your own ignored secrets file. For UI changes, test touch targets, English and German labels, light/dark text, night scheduling and switching between pages on hardware. The component exposes `validate_tile_controls` and `validate_display_fonts` actions for non-actuating checks through the ESPHome integration.

Never commit `secrets.yaml`, compiled firmware, local logs, MAC addresses, printer serials or actual installation mappings. Do not send printer-motion or workshop-power commands as an automated test.
