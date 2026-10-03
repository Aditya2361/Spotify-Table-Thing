// NowPlaying Desk - ESP32 firmware
//
// STATUS: DRAFT. NOT YET TESTED ON HARDWARE.
// Written before the board arrived. Things that must be checked on the real
// board are marked with "CHECK ON BOARD".
//
// What it does:
//   1. Connects to WiFi.
//   2. About once a second, asks the Mac server for /state (song info).
//   3. When the cover changes, downloads /cover.raw (80x80 RGB565) and draws it.
//   4. Draws title, artist, progress bar and buttons (same layout as layout_mockup.py).
//   5. Touch on a button sends a command back to the Mac (touch is a stub for now).
//
// Libraries (Arduino Library Manager): LovyanGFX, ArduinoJson (version 7)
// Board: "ESP32 Dev Module"

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "secrets.h"   // WIFI_SSID, WIFI_PASS, MAC_IP, ACCESS_KEY (copy secrets_example.h)

// ============================================================
// SCREEN SETUP (Freenove ESP32 Display 2.8", ILI9341)
// Screen pins come from Freenove's documentation.
// ============================================================

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;

public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 14;   // LCD_SCK
      cfg.pin_mosi = 13;   // LCD_MOSI
      cfg.pin_miso = 12;   // LCD_MISO
      cfg.pin_dc = 2;      // LCD_RS
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = 15;     // LCD_CS
      cfg.pin_rst = -1;
      cfg.pin_busy = -1;
      cfg.memory_width = 240;
      cfg.memory_height = 320;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = true;
      cfg.invert = false;      // CHECK ON BOARD: if colours look inverted, set true
      cfg.rgb_order = false;   // CHECK ON BOARD: if red and blue are swapped, set true
      cfg.dlinear = false;
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = 21;         // CHECK ON BOARD: backlight pin not confirmed in Freenove docs.
                               // If the screen stays black, check this pin first.
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }
};

LGFX tft;

// ============================================================
// SETTINGS AND LAYOUT (same numbers as layout_mockup.py)
// ============================================================

const int MAC_PORT = 8000;
const uint32_t POLL_MS = 1000;

const int SCREEN_W = 320;
const int SCREEN_H = 240;
const int MARGIN = 12;
const int COVER_W = 80;
const int COVER_H = 80;
const int TOP_END = SCREEN_H / 2;                    // top half: 0 - 120
const int PROG_END = SCREEN_H / 2 + SCREEN_H / 4;    // progress zone: 120 - 180
const int BAR_H = 6;
const int BAR_Y = 138;

const int BTN_Y = 210;                               // controls zone: 180 - 240
const int BTN_X[3] = {80, 160, 240};
const char* const BTN_CMD[3] = {"previous", "toggle", "next"};
const int HIT_W = 64;
const int HIT_H = 56;

const uint16_t BG = 0x0000;      // black
const uint16_t FG = 0xFFFF;      // white
const uint16_t SUB = 0xB596;     // light grey
const uint16_t TRACK = 0x4208;   // dark grey

// ============================================================
// DATA
// ============================================================

struct SongState {
  bool active = false;
  bool playing = false;
  bool hasCover = false;
  int coverId = 0;
  float elapsed = 0;
  float duration = 0;
  String title;
  String artist;
};

static uint8_t rawBuf[COVER_W * COVER_H * 2];
static uint16_t coverPx[COVER_W * COVER_H];

enum Mode { MODE_NONE, MODE_MESSAGE, MODE_SONG };
Mode shownMode = MODE_NONE;
String shownMessage = "";
String shownTitle = "";
String shownArtist = "";
int shownCoverId = -1;
bool coverDrawn = false;
int shownPlaying = -1;
uint32_t lastPoll = 0;

// ============================================================
// NETWORK
// ============================================================

String urlFor(const String& path) {
  return String("http://") + MAC_IP + ":" + String(MAC_PORT) + path + "?key=" + ACCESS_KEY;
}

// The small screen fonts only cover basic English letters, so other
// characters become "?". (Non-ASCII characters take several bytes.)
String asciiOnly(const char* in) {
  String out;
  for (const uint8_t* p = (const uint8_t*)in; *p; p++) {
    if (*p < 0x80) out += (char)*p;
    else if (*p >= 0xC0) out += '?';   // start of a non-ASCII character
    // 0x80 - 0xBF are the rest of that character, so skip them
  }
  return out;
}

bool fetchState(SongState& s) {
  HTTPClient http;
  http.begin(urlFor("/state"));
  http.setTimeout(2000);
  int code = http.GET();
  if (code != 200) {
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;

  s.active = doc["active"] | false;
  s.playing = doc["playing"] | false;
  s.hasCover = doc["has_cover"] | false;
  s.coverId = doc["cover_id"] | 0;
  s.elapsed = doc["elapsed"] | 0.0f;
  s.duration = doc["duration"] | 0.0f;
  s.title = asciiOnly(doc["title"] | "");
  s.artist = asciiOnly(doc["artist"] | "");
  return true;
}

bool fetchCover() {
  HTTPClient http;
  http.begin(urlFor("/cover.raw"));
  http.setTimeout(3000);
  int code = http.GET();
  if (code != 200) {
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  const size_t need = COVER_W * COVER_H * 2;
  size_t got = 0;
  uint32_t start = millis();
  while (got < need && millis() - start < 4000) {
    int n = stream->readBytes(rawBuf + got, need - got);
    if (n > 0) got += n;
  }
  http.end();
  if (got != need) return false;

  // The Mac sends each pixel as 2 bytes, high byte first.
  for (int i = 0; i < COVER_W * COVER_H; i++) {
    coverPx[i] = ((uint16_t)rawBuf[2 * i] << 8) | rawBuf[2 * i + 1];
  }
  return true;
}

void sendCommand(const char* name) {
  HTTPClient http;
  http.begin(urlFor(String("/cmd/") + name));
  http.setTimeout(2000);
  http.POST("");
  http.end();
}

// ============================================================
// DRAWING HELPERS
// ============================================================

String mmss(float seconds) {
  int t = (int)seconds;
  if (t < 0) t = 0;
  char buf[12];
  snprintf(buf, sizeof(buf), "%d:%02d", t / 60, t % 60);
  return String(buf);
}

// Shortens text with "..." until it fits. Uses the font that is currently set.
String fitText(const String& text, int maxW) {
  if (tft.textWidth(text) <= maxW) return text;
  String t = text;
  while (t.length() > 1 && tft.textWidth(t + "...") > maxW) {
    t.remove(t.length() - 1);
  }
  return t + "...";
}

void showMessage(const String& msg) {
  if (shownMode == MODE_MESSAGE && shownMessage == msg) return;
  tft.fillScreen(BG);
  tft.setFont(&fonts::FreeSansBold9pt7b);
  tft.setTextColor(SUB, BG);
  int x = (SCREEN_W - tft.textWidth(msg)) / 2;
  int y = (SCREEN_H - tft.fontHeight()) / 2;
  tft.drawString(msg, x, y);
  shownMode = MODE_MESSAGE;
  shownMessage = msg;
}

void drawCoverPlaceholder() {
  int coverY = (TOP_END - COVER_H) / 2;
  tft.fillRect(MARGIN, coverY, COVER_W, COVER_H, TRACK);
}

void drawCover() {
  int coverY = (TOP_END - COVER_H) / 2;
  tft.pushImage(MARGIN, coverY, COVER_W, COVER_H, coverPx);
}

void drawHeader(const String& title, const String& artist) {
  const int coverY = (TOP_END - COVER_H) / 2;
  const int textX = MARGIN + COVER_W + MARGIN;
  const int textW = SCREEN_W - MARGIN - textX;

  tft.fillRect(textX, 0, SCREEN_W - textX, TOP_END, BG);

  tft.setFont(&fonts::FreeSansBold12pt7b);
  int titleH = tft.fontHeight();
  tft.setFont(&fonts::FreeSans9pt7b);
  int artistH = tft.fontHeight();

  int blockH = titleH + 4 + artistH;
  int y0 = coverY + (COVER_H - blockH) / 2;

  tft.setFont(&fonts::FreeSansBold12pt7b);
  tft.setTextColor(FG, BG);
  tft.drawString(fitText(title, textW), textX, y0);

  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextColor(SUB, BG);
  tft.drawString(fitText(artist, textW), textX, y0 + titleH + 4);
}

void drawProgress(float elapsed, float duration) {
  const int x0 = MARGIN;
  const int x1 = SCREEN_W - MARGIN;

  float frac = duration > 0 ? elapsed / duration : 0;
  if (frac < 0) frac = 0;
  if (frac > 1) frac = 1;

  tft.fillRect(0, TOP_END + 4, SCREEN_W, PROG_END - TOP_END - 4, BG);
  tft.fillRoundRect(x0, BAR_Y, x1 - x0, BAR_H, BAR_H / 2, TRACK);
  int fillW = (int)((x1 - x0) * frac);
  if (fillW > 0) {
    if (fillW < BAR_H) fillW = BAR_H;
    tft.fillRoundRect(x0, BAR_Y, fillW, BAR_H, BAR_H / 2, FG);
  }

  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextColor(SUB, BG);
  String e = mmss(elapsed);
  String d = mmss(duration);
  int ty = BAR_Y + BAR_H + 6;
  tft.drawString(e, x0, ty);
  tft.drawString(d, x1 - tft.textWidth(d), ty);
}

void drawButtons(bool playing) {
  tft.fillRect(0, PROG_END, SCREEN_W, SCREEN_H - PROG_END, BG);
  int cy = BTN_Y;

  // previous
  int cx = BTN_X[0];
  tft.fillRect(cx - 12, cy - 9, 4, 19, FG);
  tft.fillTriangle(cx + 11, cy - 9, cx + 11, cy + 9, cx - 7, cy, FG);

  // play / pause (shows PAUSE while playing, PLAY while paused)
  cx = BTN_X[1];
  tft.drawCircle(cx, cy, 24, FG);
  tft.drawCircle(cx, cy, 23, FG);
  if (playing) {
    tft.fillRect(cx - 8, cy - 11, 6, 23, FG);
    tft.fillRect(cx + 3, cy - 11, 6, 23, FG);
  } else {
    tft.fillTriangle(cx - 7, cy - 11, cx - 7, cy + 12, cx + 12, cy, FG);
  }

  // next
  cx = BTN_X[2];
  tft.fillTriangle(cx - 11, cy - 9, cx - 11, cy + 9, cx + 7, cy, FG);
  tft.fillRect(cx + 9, cy - 9, 4, 19, FG);
}

// Draws only what changed, so the screen does not flicker.
void showSong(const SongState& s) {
  if (shownMode != MODE_SONG) {
    tft.fillScreen(BG);
    shownMode = MODE_SONG;
    shownTitle = "";
    shownArtist = "";
    coverDrawn = false;
    shownPlaying = -1;
  }

  if (s.title != shownTitle || s.artist != shownArtist) {
    drawHeader(s.title, s.artist);
    drawCoverPlaceholder();
    coverDrawn = false;
    shownTitle = s.title;
    shownArtist = s.artist;
  }

  if (s.hasCover && (!coverDrawn || s.coverId != shownCoverId)) {
    if (fetchCover()) {
      drawCover();
      coverDrawn = true;
      shownCoverId = s.coverId;
    }
  }

  if ((int)s.playing != shownPlaying) {
    drawButtons(s.playing);
    shownPlaying = s.playing;
  }

  drawProgress(s.elapsed, s.duration);
}

// ============================================================
// TOUCH
// ============================================================

// CHECK ON BOARD: touch is NOT implemented yet.
// The touch chip and its pins are not confirmed in Freenove's docs for this
// model. Once they are known, fill them in using LovyanGFX's touch support
// (for an XPT2046 chip: a lgfx::Touch_XPT2046 block in the LGFX class above),
// then replace this function with: return tft.getTouch(&x, &y);
// Touch coordinates also depend on the screen rotation set in setup().
bool getTouchPoint(int& x, int& y) {
  return false;
}

void handleTouch(int x, int y) {
  static uint32_t lastTouch = 0;
  if (millis() - lastTouch < 500) return;   // ignore repeats

  for (int i = 0; i < 3; i++) {
    if (abs(x - BTN_X[i]) <= HIT_W / 2 && abs(y - BTN_Y) <= HIT_H / 2) {
      sendCommand(BTN_CMD[i]);
      lastTouch = millis();
      lastPoll = millis() - (POLL_MS - 600);   // refresh soon, so the icon updates
      return;
    }
  }
}

// ============================================================
// SETUP AND LOOP
// ============================================================

void connectWifi() {
  showMessage("Connecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(250);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected. Board IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi failed");
  }
}

void setup() {
  Serial.begin(115200);
  tft.begin();
  tft.setRotation(1);          // CHECK ON BOARD: if the picture is upside down, try 3
  tft.setBrightness(255);
  tft.fillScreen(BG);
  connectWifi();
}

void loop() {
  int tx, ty;
  if (getTouchPoint(tx, ty)) handleTouch(tx, ty);

  if (WiFi.status() != WL_CONNECTED) {
    showMessage("WiFi lost, retrying...");
    WiFi.reconnect();
    delay(1000);
    return;
  }

  if (millis() - lastPoll >= POLL_MS) {
    lastPoll = millis();
    SongState s;
    if (!fetchState(s)) {
      showMessage("Waiting for Mac...");
    } else if (!s.active) {
      showMessage("Nothing playing");
    } else {
      showSong(s);
    }
  }
}
