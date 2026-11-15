#pragma once
// External 240x320 ILI9341 panel driver for M5Stack Cardputer ADV,
// wired to the EXT header exactly as in the TamaFi Cardputer port /
// skizzophrenic/Cardputer-CSI-Human-Detector:
//
//   ILI9341 pin | Cardputer ADV GPIO
//   ------------|--------------------
//   CS          | GPIO 5
//   RST         | GPIO 3
//   DC          | GPIO 6
//   MOSI        | GPIO 14
//   SCK         | GPIO 40
//   MISO        | (not wired - write-only display)
//
// Carried over from the TamaFi project's working config, EXCEPT rotation
// (see main.cpp - that value was tuned for the wrong ILI9342 substitute
// driver and needed correcting once the real ILI9341 driver went in;
// different chip drivers don't necessarily agree on what a given rotation
// number means in physical terms, even on identical wiring):
//   - bus_shared = false: this claims SPI2_HOST exclusively. That's fine
//     here since this project (like TamaFi) doesn't touch the SD card from
//     firmware - SD card and display share SCK=40/MOSI=14, just different
//     CS, and sharing that bus properly wasn't worth doing for either
//     project. If this ever needs SD access from the firmware itself
//     (rather than via M5Launcher), budget real time for bus arbitration.

#include <M5Unified.h>
#include <M5GFX.h>
#include "Panel_ILI9341.hpp"
// NOTE: M5GFX (as pulled in via m5stack/M5Cardputer's lib_deps) does not
// bundle a Panel_ILI9341 driver class at all - only Panel_ILI9342 (plus
// ST7735/ST7789/GC9A01/SSD1306/etc). An earlier version of this file
// substituted Panel_ILI9342, since it's the closest sibling in the same
// Ilitek command family - but that was wrong in a way that only showed up
// on real hardware: Panel_ILI9342's constructor defaults to a 320x240
// *memory* addressing window (its native landscape orientation), while
// Panel_ILI9341's defaults to 240x320. Our config only overrides
// panel_width/panel_height, not the underlying memory window, so with
// ILI9342 the GRAM addressing window ends up a different shape than what's
// actually being drawn - which is exactly what produces "content squeezed
// into one corner + a blank strip along one edge." Panel_ILI9341.hpp here
// is vendored locally (see that file's header comment) rather than
// substituted, since it's the one that actually matches this panel.

class LGFX_ExtILI9341 : public lgfx::LGFX_Device
{
  lgfx::Panel_ILI9341 _panel_instance;
  lgfx::Bus_SPI _bus_instance;

public:
  LGFX_ExtILI9341(void)
  {
    { // SPI bus config
      auto cfg = _bus_instance.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 40;
      cfg.pin_mosi = 14;
      cfg.pin_miso = -1; // not wired, write-only display
      cfg.pin_dc = 6;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    { // Panel config
      auto cfg = _panel_instance.config();
      cfg.pin_cs = 5;
      cfg.pin_rst = 3;
      cfg.pin_busy = -1;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.memory_width = 240;  // must match panel_width for this chip - see note above
      cfg.memory_height = 320; // must match panel_height for this chip - see note above
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = false;
      cfg.invert = false;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel_instance.config(cfg);
    }

    setPanel(&_panel_instance);
  }
};
