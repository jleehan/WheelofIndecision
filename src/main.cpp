// TamaFi-hardware Decision Wheel
//
// Same M5Stack Cardputer ADV + external 240x320 ILI9341 as the TamaFi port.
// Lets you type in a list of options on the built-in keyboard, then spins
// a color-coded wheel (a la pickerwheel.com) to pick one.
//
// Known simplification vs. a "real" pickerwheel: item names are NOT drawn
// rotating on the wheel itself (arbitrary-angle text rendering on M5GFX
// sprites is finicky and wasn't worth risking un-tested on real hardware).
// Instead each slice gets a color, a fixed non-rotating legend on the right
// maps color -> item name, and the winning name is shown large once the
// wheel stops. See README "Known cosmetic gap" for how to upgrade this
// later if wanted.

#include <M5Unified.h>
#include <M5Cardputer.h>
#include <M5GFX.h>
#include <Preferences.h>
#include <vector>
#include <math.h>

#include "ext_panel.h"

// ---------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------
LGFX_ExtILI9341 tft;
M5Canvas wheelCanvas(&tft); // square canvas, rotated as a whole onto tft

static const int SCREEN_W = 320;
static const int SCREEN_H = 240;

static const int WHEEL_D = 200; // wheel canvas diameter (square canvas WHEEL_D x WHEEL_D)
static const int WHEEL_CX = 120; // center x on screen
static const int WHEEL_CY = 130; // center y on screen

// Corner-fill color for the square wheel canvas, chosen distinct from
// TFT_BLACK/TFT_WHITE and every palette entry below, so it can be used as
// pushRotateZoom's transparent-color key (see renderWheelCanvas/drawWheelScreen).
static const uint16_t WHEEL_CANVAS_KEY = 0x0001;

// ---------------------------------------------------------------------
// Persistent item list
// ---------------------------------------------------------------------
Preferences prefs;
static const int MAX_ITEMS = 16;
static const int MAX_ITEM_LEN = 28;
std::vector<String> items;

void saveItems()
{
  String joined;
  for (size_t i = 0; i < items.size(); i++)
  {
    joined += items[i];
    joined += '\n';
  }
  prefs.begin("wheel", false);
  prefs.putString("list", joined);
  prefs.end();
}

void loadItems()
{
  prefs.begin("wheel", true);
  String joined = prefs.getString("list", "");
  prefs.end();

  items.clear();
  int start = 0;
  while (start < (int)joined.length())
  {
    int nl = joined.indexOf('\n', start);
    if (nl < 0) break;
    String item = joined.substring(start, nl);
    if (item.length() > 0) items.push_back(item);
    start = nl + 1;
  }

  if (items.empty())
  {
    // Friendly defaults on first boot
    items.push_back("Pizza");
    items.push_back("Tacos");
    items.push_back("Sushi");
    items.push_back("Burgers");
  }
}

// ---------------------------------------------------------------------
// App state
// ---------------------------------------------------------------------
enum AppState
{
  STATE_HOME,
  STATE_EDIT_LIST,
  STATE_WHEEL
};
AppState state = STATE_HOME;

// Home menu
const char *homeMenu[] = {"Spin Wheel", "Edit List", "Clear List"};
const int homeMenuCount = 3;
int homeSel = 0;

// Edit list
String editBuffer = "";
int editSel = -1; // index of item "selected" for deletion when buffer is empty

// Wheel
bool spinning = false;
float wheelAngle = 0;        // current rotation, degrees
float spinStartAngle = 0;
float spinTargetAngle = 0;
uint32_t spinStartMs = 0;
uint32_t spinDurationMs = 0;
int lastSliceCrossed = -1;
int winnerIndex = -1;
bool showResult = false;

// Palette for slices (RGB565-friendly distinct colors)
const uint16_t palette[] = {
    0xF800, // red
    0xFD20, // orange
    0xFFE0, // yellow
    0x07E0, // green
    0x07FF, // cyan
    0x001F, // blue
    0x781F, // purple
    0xF81F, // magenta
    0xFBE0, // amber
    0x8410, // gray
    0x07EF, // teal
    0xFC9F, // pink
};
const int paletteCount = sizeof(palette) / sizeof(palette[0]);

// ---------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------
void drawHome()
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(top_center);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.drawString("Decision Wheel", SCREEN_W / 2, 16);

  tft.setTextDatum(middle_left);
  for (int i = 0; i < homeMenuCount; i++)
  {
    int y = 90 + i * 34;
    bool sel = (i == homeSel);
    tft.fillRoundRect(40, y - 14, 240, 28, 6, sel ? TFT_DARKGREY : TFT_BLACK);
    tft.setTextColor(sel ? TFT_YELLOW : TFT_WHITE);
    tft.setTextSize(2);
    String label = homeMenu[i];
    if (i == 1) label += " (" + String(items.size()) + ")";
    tft.drawString(label, 56, y);
  }

  tft.setTextDatum(bottom_center);
  tft.setTextColor(TFT_DARKGREY);
  tft.setTextSize(1);
  tft.drawString(";=up  .=down  Enter=select", SCREEN_W / 2, SCREEN_H - 8);
}

void drawEditList()
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(top_left);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.drawString("Edit List", 10, 8);

  tft.setTextSize(1);
  int y = 36;
  for (size_t i = 0; i < items.size() && y < SCREEN_H - 40; i++)
  {
    bool sel = ((int)i == editSel);
    tft.setTextColor(sel ? TFT_YELLOW : TFT_WHITE);
    String line = String(i + 1) + ". " + items[i];
    tft.drawString(line, 14, y);
    y += 14;
  }

  // input line
  tft.fillRect(0, SCREEN_H - 34, SCREEN_W, 34, 0x2104);
  tft.setTextColor(TFT_GREEN);
  tft.setTextSize(2);
  tft.drawString("> " + editBuffer + "_", 10, SCREEN_H - 26);

  tft.setTextDatum(bottom_center);
  tft.setTextColor(TFT_DARKGREY);
  tft.setTextSize(1);
  tft.drawString("Type+Enter=add  Del=backspace/remove  Opt=Home", SCREEN_W / 2, SCREEN_H - 2);
}

// Build the static wheel image (colored slices only, no rotating text)
void renderWheelCanvas()
{
  wheelCanvas.fillSprite(WHEEL_CANVAS_KEY);
  int n = items.size();
  if (n <= 0) return;

  int r = WHEEL_D / 2;
  int cx = r, cy = r;
  float sliceDeg = 360.0f / n;

  for (int i = 0; i < n; i++)
  {
    float a0 = i * sliceDeg;
    float a1 = a0 + sliceDeg;
    uint16_t color = palette[i % paletteCount];
    // approximate the slice with a fan of triangles for a smooth edge
    int steps = max(2, (int)(sliceDeg / 4));
    for (int s = 0; s < steps; s++)
    {
      float ta0 = a0 + (a1 - a0) * s / steps;
      float ta1 = a0 + (a1 - a0) * (s + 1) / steps;
      float r0x = cx + r * cosf(radians(ta0));
      float r0y = cy + r * sinf(radians(ta0));
      float r1x = cx + r * cosf(radians(ta1));
      float r1y = cy + r * sinf(radians(ta1));
      wheelCanvas.fillTriangle(cx, cy, r0x, r0y, r1x, r1y, color);
    }
  }
  wheelCanvas.drawCircle(cx, cy, r - 1, TFT_WHITE);
  wheelCanvas.fillCircle(cx, cy, 6, TFT_WHITE);
}

void drawLegend()
{
  int n = items.size();
  int x = 250;
  int y = 40;
  tft.setTextDatum(top_left);
  tft.setTextSize(1);
  for (int i = 0; i < n && y < SCREEN_H - 40; i++)
  {
    uint16_t color = palette[i % paletteCount];
    tft.fillRect(x, y, 10, 10, color);
    tft.setTextColor(TFT_WHITE);
    String label = items[i];
    if (label.length() > 8) label = label.substring(0, 7) + ".";
    tft.drawString(label, x + 14, y);
    y += 14;
  }
}

void drawWheelScreen(bool fullRedraw)
{
  if (fullRedraw)
  {
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(top_center);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.drawString("Spin!", SCREEN_W / 2, 6);
    drawLegend();
  }
  else
  {
    // clear just the wheel area before redrawing the rotated sprite
    tft.fillRect(WHEEL_CX - WHEEL_D / 2 - 4, WHEEL_CY - WHEEL_D / 2 - 4,
                 WHEEL_D + 8, WHEEL_D + 8, TFT_BLACK);
  }

  // Push only the inscribed circle - corners of the square canvas are
  // skipped via the transparent-color key, since a rotated square's
  // corners sweep out to ~1.4x the wheel radius (well past the legend at
  // extreme angles) if drawn opaque. The circle itself is rotationally
  // symmetric, so its visible footprint never grows regardless of angle.
  wheelCanvas.pushRotateZoom(WHEEL_CX, WHEEL_CY, wheelAngle, 1.0f, 1.0f, WHEEL_CANVAS_KEY);

  // fixed pointer at top of wheel
  int px = WHEEL_CX;
  int py = WHEEL_CY - WHEEL_D / 2 - 2;
  tft.fillTriangle(px - 8, py - 14, px + 8, py - 14, px, py, TFT_WHITE);

  if (showResult && winnerIndex >= 0)
  {
    tft.fillRoundRect(20, SCREEN_H - 40, SCREEN_W - 40, 34, 8, TFT_DARKGREEN);
    tft.setTextDatum(middle_center);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.drawString(items[winnerIndex], SCREEN_W / 2, SCREEN_H - 23);
  }
  else
  {
    tft.setTextDatum(bottom_center);
    tft.setTextColor(TFT_DARKGREY);
    tft.setTextSize(1);
    tft.drawString("Enter=spin  Opt=Home", SCREEN_W / 2, SCREEN_H - 4);
  }
}

// ---------------------------------------------------------------------
// Spin logic
// ---------------------------------------------------------------------
float easeOutCubic(float t)
{
  float f = t - 1.0f;
  return f * f * f + 1.0f;
}

void startSpin()
{
  int n = items.size();
  if (n <= 0) return;

  winnerIndex = esp_random() % n;
  showResult = false;

  float sliceDeg = 360.0f / n;
  // Pointer is fixed at top (screen "up" = -90 deg in the wheel's own
  // 0-deg-at-3-o'clock coordinate system used when drawing slices).
  // We want: after rotating the wheel by `finalAngle`, the winner slice's
  // center lands under the pointer.
  float winnerCenterDeg = winnerIndex * sliceDeg + sliceDeg / 2.0f;
  float jitter = (float)(esp_random() % 1000) / 1000.0f * (sliceDeg * 0.6f) - (sliceDeg * 0.3f);

  int extraSpins = 5 + (esp_random() % 4); // 5-8 full turns
  // pushRotateZoom's angle rotates clockwise; pointer sits at -90 deg
  // (screen "up") relative to the wheel's own drawing origin.
  float target = extraSpins * 360.0f + (270.0f - winnerCenterDeg + jitter);

  spinStartAngle = wheelAngle;
  spinTargetAngle = spinStartAngle + target;
  spinStartMs = millis();
  spinDurationMs = 3800 + (esp_random() % 800); // ~3.8-4.6s
  lastSliceCrossed = -1;
  spinning = true;

  M5Cardputer.Speaker.tone(1200);
  M5Cardputer.Speaker.stop();
}

void updateSpin()
{
  if (!spinning) return;

  uint32_t elapsed = millis() - spinStartMs;
  float t = (float)elapsed / (float)spinDurationMs;
  if (t >= 1.0f)
  {
    t = 1.0f;
    wheelAngle = fmodf(spinTargetAngle, 360.0f);
    spinning = false;
    showResult = true;
    // little fanfare
    M5Cardputer.Speaker.tone(880);
    delay(90);
    M5Cardputer.Speaker.tone(1175);
    delay(90);
    M5Cardputer.Speaker.tone(1568);
    delay(140);
    M5Cardputer.Speaker.stop();
  }
  else
  {
    float eased = easeOutCubic(t);
    wheelAngle = fmodf(spinStartAngle + (spinTargetAngle - spinStartAngle) * eased, 360.0f);

    // tick sound each time we cross a slice boundary
    int n = items.size();
    if (n > 0)
    {
      float sliceDeg = 360.0f / n;
      int currentSlice = (int)(fmodf(360.0f - wheelAngle + 90.0f, 360.0f) / sliceDeg);
      if (currentSlice != lastSliceCrossed)
      {
        lastSliceCrossed = currentSlice;
        M5Cardputer.Speaker.tone(2600);
        M5Cardputer.Speaker.stop();
      }
    }
  }

  drawWheelScreen(false);
}

// ---------------------------------------------------------------------
// Input handling
// ---------------------------------------------------------------------
void handleHomeInput(const Keyboard_Class::KeysState &status)
{
  bool navChanged = false;
  for (char c : status.word)
  {
    if (c == ';') { homeSel = (homeSel - 1 + homeMenuCount) % homeMenuCount; navChanged = true; }
    else if (c == '.') { homeSel = (homeSel + 1) % homeMenuCount; navChanged = true; }
  }
  if (navChanged) drawHome(); // was missing entirely - homeSel changed but the
                              // highlighted menu item never got redrawn to show it
  if (status.enter)
  {
    if (homeSel == 0)
    {
      if (!items.empty())
      {
        renderWheelCanvas();
        wheelAngle = 0;
        showResult = false;
        state = STATE_WHEEL;
        drawWheelScreen(true);
      }
      else
      {
        editSel = -1;
        editBuffer = "";
        state = STATE_EDIT_LIST;
        drawEditList();
      }
    }
    else if (homeSel == 1)
    {
      editSel = -1;
      editBuffer = "";
      state = STATE_EDIT_LIST;
      drawEditList();
    }
    else if (homeSel == 2)
    {
      items.clear();
      saveItems();
      drawHome();
    }
  }
}

void handleEditInput(const Keyboard_Class::KeysState &status)
{
  bool changed = false;

  if (status.opt)
  {
    state = STATE_HOME;
    drawHome();
    return;
  }

  if (status.del)
  {
    if (editBuffer.length() > 0)
    {
      editBuffer.remove(editBuffer.length() - 1);
      changed = true;
    }
    else if (!items.empty())
    {
      items.pop_back();
      saveItems();
      changed = true;
    }
  }

  if (status.enter && editBuffer.length() > 0)
  {
    if ((int)items.size() < MAX_ITEMS)
    {
      items.push_back(editBuffer);
      saveItems();
    }
    editBuffer = "";
    changed = true;
  }

  for (char c : status.word)
  {
    if (editBuffer.length() < MAX_ITEM_LEN && isprint((unsigned char)c))
    {
      editBuffer += c;
      changed = true;
    }
  }

  if (changed) drawEditList();
}

void handleWheelInput(const Keyboard_Class::KeysState &status)
{
  if (status.opt)
  {
    state = STATE_HOME;
    drawHome();
    return;
  }
  if (status.enter && !spinning)
  {
    startSpin();
    drawWheelScreen(true);
  }
}

// ---------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------
void setup()
{
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true); // true = also init built-in keyboard/speaker

  tft.init();
  tft.setRotation(1); // was 7 (=3 + horizontal-mirror bit) under the old
                       // Panel_ILI9342 substitute; the real Panel_ILI9341's
                       // MADCTL bits don't line up the same way on this
                       // wiring, and the mirror bit turned out to be wrong
                       // for it. If this still looks mirrored or rotated
                       // wrong, try 1 next, then 5 - see ext_panel.h notes.
  tft.fillScreen(TFT_BLACK);

  wheelCanvas.setColorDepth(16);
  wheelCanvas.setPsram(false); // no PSRAM on this board
  wheelCanvas.createSprite(WHEEL_D, WHEEL_D);
  wheelCanvas.setSwapBytes(true); // matches TamaFi's M5GFX sprite gotcha

  loadItems();
  drawHome();
}

void loop()
{
  M5Cardputer.update();

  if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed())
  {
    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
    switch (state)
    {
      case STATE_HOME: handleHomeInput(status); break;
      case STATE_EDIT_LIST: handleEditInput(status); break;
      case STATE_WHEEL: handleWheelInput(status); break;
    }
  }

  if (state == STATE_WHEEL && spinning)
  {
    updateSpin();
  }

  delay(5);
}
