# Decision Wheel — Cardputer ADV Port

A "pick a random option from a list" spinner (pickerwheel.com-style) for the
same M5Stack Cardputer ADV + external 240x320 ILI9341 setup used in the
TamaFi Cardputer port.

**Status: written but NOT build-verified.** This sandbox doesn't have
network access to the Espressif toolchain (`platform = espressif32` pulls
from Espressif's package servers, which aren't in this environment's
allowed domain list), so I couldn't run `pio run` here. Treat this as a
first pass to compile and flash yourself — see "Things worth double
checking" below for the spots most likely to need a tweak.

## Hardware (identical to the TamaFi port)

| ILI9341 pin | Cardputer ADV GPIO |
|---|---|
| CS   | GPIO 5  |
| RST  | GPIO 3  |
| DC   | GPIO 6  |
| MOSI | GPIO 14 |
| SCK  | GPIO 40 |
| VCC / GND / LED | 3.3V / GND / 3.3V |

Same wiring and exclusive-SPI2_HOST (`bus_shared =
false`) as TamaFi — `include/ext_panel.h` here is a fresh reimplementation
of that same config from the handoff doc's documented pin table and
gotchas (I didn't have the original TamaFi source files, only the
handoff's description of them).

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

## Known simplification (read before "fixing" the wheel art)

Item names are **not** drawn rotating on the wheel itself. Rotating text at
an arbitrary angle on an M5GFX sprite is the one part of this I wasn't
confident rendering correctly without testing on real hardware, so instead:
the wheel is colored slices only, a **fixed legend** on the right maps
color → item name, and the winner's full name is shown as a banner once the
wheel stops.

If you want real rotating labels on the wheel later: bake each label into
the wheel canvas *before* calling `pushRotateZoom` by drawing it with
per-character trig placement (rotate each glyph's origin around the wheel
center by the slice's mid-angle), since `drawString` itself has no
arbitrary-angle mode in M5GFX/LovyanGFX — only whole-sprite `setRotation()`
in 90° steps.

## Build & deploy

Build:
```
pio run
```

The TamaFi handoff notes this board's USB-serial never enumerated on the
dev machine used for that project, so deployment there went through SD
card + M5Launcher instead of `pio run -t upload`. If that's still the case
on your setup, same path applies here:

```
~/.platformio/penv/bin/python ~/.platformio/packages/tool-esptoolpy/esptool.py \
  --chip esp32s3 merge_bin -o decision-wheel-merged.bin \
  0x0     .pio/build/cardputer-wheel/bootloader.bin \
  0x8000  .pio/build/cardputer-wheel/partitions.bin \
  0x10000 .pio/build/cardputer-wheel/firmware.bin
```

Copy the merged `.bin` to the SD card, boot into M5Launcher, install/run.
If USB does work for you (or you've since resolved it / tried a different
machine per the TamaFi handoff's suggestion), `pio run -t upload` and
`pio device monitor` should work normally too.

## Fixed: display was off-center with a blank strip on one side

First build attempt substituted `Panel_ILI9342` for the driver, since M5GFX
doesn't bundle `Panel_ILI9341` at all. That compiled but was wrong on real
hardware: `Panel_ILI9342`'s constructor defaults to a 320x240 *memory*
addressing window (its native landscape shape), while this panel needs
240x320. Our config only overrode `panel_width`/`panel_height`, not that
underlying memory window, so the GRAM addressing window ended up a
different shape than what was being drawn - producing content squeezed
into one corner with a blank strip along one edge.

Fixed by vendoring the actual `Panel_ILI9341.hpp` from LovyanGFX into
`include/` (verified byte-identical dependency on M5GFX's own
`Panel_LCD.hpp` base class, so it's safe to compile against M5GFX without
pulling in the full, namespace-conflicting LovyanGFX library). `ext_panel.h`
now also sets `memory_width`/`memory_height` explicitly rather than relying
on constructor defaults, so this class of bug is visible in the config
rather than hidden in a base class you'd have to go looking for.

## Fixed: display was mirrored, and Up/Down navigation didn't work

Two unrelated bugs from the previous round:

- **Mirrored display:** `tft.setRotation(7)` was carried over from the
  TamaFi handoff, but that value was tuned for the wrong `Panel_ILI9342`
  substitute driver (see previous section). LovyanGFX rotations 4-7 are
  0-3 plus a horizontal-mirror bit, and that bit doesn't necessarily mean
  the same physical thing across different chip drivers, even on identical
  wiring. Changed to `setRotation(3)` (same base orientation, mirror bit
  dropped). If it's still mirrored or rotated wrong, try `1` next, then `5`.
- **Up/Down did nothing in the Home menu:** genuine bug, not a hardware
  issue. `handleHomeInput()` updated `homeSel` in memory on `;`/`.` but
  never called `drawHome()` afterward, so the highlighted menu item never
  actually redrew. Enter worked because each of its branches redraws
  explicitly. Fixed by redrawing whenever navigation changes the selection.

## Fixed: black creeping outward during spin, eating into the legend

The wheel canvas is a square (needed so `pushRotateZoom` has room to rotate
the circle inside it), and its corners were filled solid black. A rotated
square's corners sweep out to about 1.4x the circle's radius at diagonal
angles (45°, 135°, etc.) - for this wheel that's enough to reach past the
small "clear before redraw" rect and directly overwrite the legend text
with black, which then never got redrawn. The legend only *looked* eaten
away because the corners kept sweeping past it every frame as the wheel
spun through those angles.

Fixed by filling the canvas corners with a distinct key color
(`WHEEL_CANVAS_KEY`) and passing it as `pushRotateZoom`'s transparent-color
argument, so those corner pixels are skipped rather than drawn. The
visible circle itself is rotationally symmetric, so its on-screen
footprint is now fixed regardless of angle and can't sweep into the
legend at all.

## Things worth double checking on first flash

- **`pushRotateZoom` transparency:** the wheel canvas background is filled
  black and the screen background is also black, so corners of the rotated
  square should blend in without needing an explicit transparent-color
  argument. If you see black square corners rotating visibly against a
  non-black background (e.g. if you change the screen bg color later),
  pass a transparent color as the 6th arg to `pushRotateZoom`.
- **Winner-under-pointer math** (`startSpin()`): the pointer is fixed at
  screen "up," and slice 0 is drawn starting at the wheel canvas's 0°
  (3 o'clock, going clockwise). The angle math converts between those two
  coordinate systems — worth spinning it a dozen times on hardware and
  confirming the highlighted-in-legend logic (there isn't any yet, just
  the banner) matches which slice visually stops under the pointer. If
  it's consistently off by a fixed amount, that's a sign the coordinate
  offset needs adjusting rather than a random bug.
- **`isprint()` on the raw keyboard chars** in `handleEditInput` — this
  should filter out stray non-printable codes from `status.word`, but if
  any keys type garbage into item names, check what codes they're
  actually sending on this keyboard revision.

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

## Possible next steps

1. Verify the build against real hardware and fix whatever the toolchain
   flags (untested in this environment, see status note above).
2. If rotating on-wheel labels are wanted, see "Known simplification"
   above for the approach.
3. Add a "highlight winning legend entry" flash once the wheel stops, to
   make the color→name mapping easier to follow at a glance.
4. Consider an on-screen diagnostic readout like TamaFi's `drawStatusBar()`
   if USB serial still isn't available for debugging on this board.
