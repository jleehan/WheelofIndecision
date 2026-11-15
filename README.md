# Decision Wheel — Cardputer ADV Port

A "pick a random option from a list" spinner (pickerwheel.com-style) for the
same M5Stack Cardputer ADV + external 240x320 ILI9341 (as seen in [skizzophrenic's M5PORKCHOP_DualScreen repository](https://github.com/skizzophrenic/M5PORKCHOP_DualScreen)). Firmware is designed to be compatible with [M5Launcher firmware](https://bmorcelli.github.io/Launcher/catalog.html)

## Hardware

| ILI9341 pin | Cardputer ADV GPIO |
|---|---|
| CS   | GPIO 5  |
| RST  | GPIO 3  |
| DC   | GPIO 6  |
| MOSI | GPIO 14 |
| SCK  | GPIO 40 |
| VCC / GND / LED | 3.3V / GND / 3.3V |

## What it does

- **Home screen:** Spin Wheel / Edit List / Clear List menu.
- **Edit List:** type an option, hit Enter to add it (up to 16 items, 28
  chars each). List is saved to flash (NVS via `Preferences.h`) so it
  survives power-off.
- **Spin Wheel:** a color-coded wheel spins with an ease-out deceleration,
  ticking as it crosses each slice, then lands on a random item (true
  random via `esp_random()`) with a little fanfare and the winner's name
  shown large at the bottom.

## Controls

| Key | Action |
|---|---|
| `;` / `.` | Up / down in menus |
| Enter | Select / add item / spin |
| Del (backspace) | Delete last typed char, or last list item if buffer empty |
| Opt (⌥) | Back to Home from Edit List or Wheel screen |

## Build & deploy

Build:
```
pio run
```

```
~/.platformio/penv/bin/python ~/.platformio/packages/tool-esptoolpy/esptool.py \
  --chip esp32s3 merge_bin -o decision-wheel-merged.bin \
  0x0     .pio/build/cardputer-wheel/bootloader.bin \
  0x8000  .pio/build/cardputer-wheel/partitions.bin \
  0x10000 .pio/build/cardputer-wheel/firmware.bin
```

Copy the merged `.bin` to the SD card, boot into M5Launcher, install/run.

## File structure

```
wheel-cardputer/
├── platformio.ini
├── README.md
├── include/
│   ├── ext_panel.h      external ILI9341 driver config (pins, bus, rotation)
│   └── Panel_ILI9341.hpp vendored LovyanGFX panel driver class (see ext_panel.h)
└── src/
    └── main.cpp          menu, list editor, wheel physics/rendering
```
