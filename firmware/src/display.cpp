#include "display.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Preferences.h>

#include "bootlog.h"
#include "config.h"
#include "log.h"
#include "fonts/LuckiestGuy36.h"
#include "fonts/LuckiestGuy50.h"
#include "logo_lm.h"
#include "machine_off.h"
#include "pin_config.h"

// QSPI bus (see pin_config.h, derived from the LilyGO factory example).
static Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);

// This 1.43" board ships with one of TWO panels needing different drivers (per LilyGO's
// factory example): DO0143FAT01 -> SH8601, DO0143FMST10 -> CO5300 (with x-offset 6).
// Switch via -DUSE_CO5300 in platformio.ini. Typed as Arduino_OLED* (the common base that
// declares setBrightness) so both branches share the same `gfx` API.
#ifndef USE_CO5300
#define USE_CO5300 1
#endif
// Note: Arduino_GFX 1.4.x ctor is (bus, rst, rotation, w, h, offsets...) — no `ips` arg.
#if USE_CO5300
static Arduino_OLED *gfx = new Arduino_CO5300(
    bus, LCD_RST, 0 /* rotation */, LCD_WIDTH, LCD_HEIGHT,
    6 /* col_offset1 */, 0, 0, 0);
#else
static Arduino_OLED *gfx = new Arduino_SH8601(
    bus, LCD_RST, 0 /* rotation */, LCD_WIDTH, LCD_HEIGHT);
#endif

// Off-screen framebuffer (in PSRAM). We draw to `canvas` and flush a whole frame at once,
// which eliminates the flicker of clearing + redrawing directly on the panel.
static Arduino_Canvas *canvas = new Arduino_Canvas(LCD_WIDTH, LCD_HEIGHT, gfx);

// The active palette. Five themed roles per scheme (bg/text/dim/accent/accent2) plus fixed
// semantics: OK green / WARN amber / ERR red mean the same in every scheme (they only get a
// darker variant in light mode for contrast), crema + brand red never change.
static uint16_t COL_BG;
static uint16_t COL_FG;
static uint16_t COL_DIM;
static uint16_t COL_ACCENT;   // primary machine color (rings, titles, hero numbers)
static uint16_t COL_ACCENT2;  // secondary (steam ring, pre-infusion, secondary titles)
static uint16_t COL_OK;       // ready / good
static uint16_t COL_WARN;     // warming up / caution
static uint16_t COL_ERR;      // error / over-extraction
static uint16_t COL_CREMA;    // crema dots (literal coffee color, never themed)
static uint16_t COL_LM_RED;   // La Marzocco brand red (logo only, trademark color)

// Scheme palettes, RGB888 so derived shades can be computed (see darkened()).
struct ThemePal {
    uint8_t bg[3], text[3], dim[3], accent[3], accent2[3];
};
struct ThemeDef {
    const char *name;
    ThemePal dark, light;
};
// One scheme per Linea Micra machine color; Silver Satin / Stainless / Satin metal share GRAY.
static const ThemeDef THEMES[THEME_SCHEMES] = {
    {"RED",  // classic La Marzocco
     {{0, 0, 0}, {245, 245, 245}, {90, 90, 90}, {226, 28, 44}, {255, 140, 120}},
     {{250, 245, 243}, {36, 20, 22}, {148, 128, 126}, {192, 0, 26}, {232, 88, 76}}},
    {"YELLOW",
     {{0, 0, 0}, {247, 243, 232}, {104, 96, 72}, {240, 180, 36}, {255, 222, 120}},
     {{251, 247, 236}, {40, 32, 16}, {158, 146, 114}, {196, 140, 10}, {140, 102, 8}}},
    {"BLUE",
     {{0, 0, 0}, {236, 246, 250}, {80, 100, 110}, {134, 196, 222}, {66, 142, 178}},
     {{242, 248, 251}, {18, 36, 46}, {128, 150, 161}, {44, 122, 156}, {100, 170, 198}}},
    {"WHITE",
     {{0, 0, 0}, {245, 245, 245}, {92, 92, 92}, {255, 255, 255}, {198, 190, 176}},
     {{255, 255, 255}, {26, 26, 26}, {156, 156, 156}, {70, 70, 70}, {122, 118, 110}}},
    {"GRAY",  // Silver Satin / Stainless Steel / Satin metal
     {{0, 0, 0}, {240, 243, 245}, {84, 90, 96}, {186, 192, 199}, {126, 135, 143}},
     {{242, 244, 246}, {27, 33, 38}, {138, 147, 155}, {84, 94, 102}, {128, 138, 146}}},
    {"BLACK",
     {{0, 0, 0}, {232, 232, 232}, {70, 70, 70}, {214, 214, 214}, {128, 128, 128}},
     {{236, 236, 236}, {16, 16, 16}, {128, 128, 128}, {10, 10, 10}, {84, 84, 84}}},
};

static int g_scheme = 0;      // active scheme (persisted)
static bool g_darkMode = true;  // dark (true) / light palette (persisted)
static const ThemePal *g_pal = &THEMES[0].dark;  // active palette (for derived shades)

static int g_pageActive = 0;  // page-indicator state
static int g_pageCount = 0;
static int g_connMode = 0;  // 0=none, 1=websocket, 2=cloud/REST
static int g_rotation = DISPLAY_ROTATION;  // runtime screen rotation (persisted)

int displayRotation() { return g_rotation; }

void displaySetRotation(int rotation) {
    g_rotation = rotation & 3;
    canvas->setRotation(g_rotation);
    Preferences p;
    p.begin("display", false);
    p.putInt("rot", g_rotation);
    p.end();
    LOGF("[display] rotation -> %d (%d deg)\n", g_rotation, g_rotation * 90);
}

void displaySetPageIndicator(int active, int count) {
    g_pageActive = active;
    g_pageCount = count;
}

void displaySetConn(int mode) { g_connMode = mode; }

// Draw `text` horizontally centered with its vertical center at `cy`, in the given GFX font.
static void drawCentered(const char *text, int cy, const GFXfont *font, uint16_t color) {
    canvas->setFont(font);
    canvas->setTextSize(1);
    canvas->setTextColor(color);
    int16_t x1, y1;
    uint16_t w, h;
    canvas->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    int cx = (canvas->width() - (int)w) / 2 - x1;
    canvas->setCursor(cx, cy - (int)h / 2 - y1);
    canvas->print(text);
}

// Linear interpolate between two RGB colors, return a 565 value.
static uint16_t lerp565(int r1, int g1, int b1, int r2, int g2, int b2, float t) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    int r = r1 + (int)((r2 - r1) * t);
    int g = g1 + (int)((g2 - g1) * t);
    int b = b1 + (int)((b2 - b1) * t);
    return gfx->color565(r, g, b);
}

// Left/centered text in the built-in font (compact labels).
static void drawCenteredClassic(const char *text, int cy, uint8_t size, uint16_t color) {
    canvas->setFont(NULL);
    canvas->setTextSize(size);
    canvas->setTextColor(color);
    int16_t x1, y1;
    uint16_t w, h;
    canvas->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    canvas->setCursor((canvas->width() - (int)w) / 2 - x1, cy - (int)h / 2 - y1);
    canvas->print(text);
}

// Like drawCenteredClassic, but centered on an arbitrary point (e.g. inside a swatch).
static void drawCenteredClassicAt(const char *text, int cx, int cy, uint8_t size,
                                  uint16_t color) {
    canvas->setFont(NULL);
    canvas->setTextSize(size);
    canvas->setTextColor(color);
    int16_t x1, y1;
    uint16_t w, h;
    canvas->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    canvas->setCursor(cx - (int)w / 2 - x1, cy - (int)h / 2 - y1);
    canvas->print(text);
}

// Progress ring: fills clockwise from 12 o'clock, `frac` of a full circle, as a solid band
// between rIn and rOut. Drawn as filled triangle sectors so there are no gaps/artifacts.
static void drawProgressArc(int cx, int cy, int rOut, int rIn, float frac, uint16_t color) {
    if (frac <= 0) return;
    if (frac > 1) frac = 1;
    const float end = frac * 360.0f;
    const float step = 1.5f;
    for (float d = 0; d < end; d += step) {
        float a1 = (-90.0f + d) * (float)M_PI / 180.0f;
        float a2 = (-90.0f + (d + step > end ? end : d + step)) * (float)M_PI / 180.0f;
        float c1 = cosf(a1), s1 = sinf(a1), c2 = cosf(a2), s2 = sinf(a2);
        int x1i = cx + (int)(c1 * rIn), y1i = cy + (int)(s1 * rIn);
        int x1o = cx + (int)(c1 * rOut), y1o = cy + (int)(s1 * rOut);
        int x2i = cx + (int)(c2 * rIn), y2i = cy + (int)(s2 * rIn);
        int x2o = cx + (int)(c2 * rOut), y2o = cy + (int)(s2 * rOut);
        canvas->fillTriangle(x1i, y1i, x1o, y1o, x2o, y2o, color);
        canvas->fillTriangle(x1i, y1i, x2o, y2o, x2i, y2i, color);
    }
}

// Big rounded button with a centered label (double outline for weight).
static void drawButton(int x, int y, int w, int h, const char *label, uint16_t color) {
    canvas->drawRoundRect(x, y, w, h, 14, color);
    canvas->drawRoundRect(x + 1, y + 1, w - 2, h - 2, 13, color);
    drawCenteredClassic(label, y + h / 2, 3, color);
}

// Filled (selected) variant: solid fill with the label knocked out in the background color.
static void drawButtonFilled(int x, int y, int w, int h, const char *label, uint16_t color) {
    canvas->fillRoundRect(x, y, w, h, 14, color);
    drawCenteredClassic(label, y + h / 2, 3, COL_BG);
}

// Gallery-style page dots near the bottom; the active page is a filled dot.
static void drawPageDots() {
    if (g_pageCount <= 1) return;
    const int y = 432, gap = 24, r = 5;
    int startx = canvas->width() / 2 - (g_pageCount - 1) * gap / 2;
    for (int i = 0; i < g_pageCount; i++) {
        int x = startx + i * gap;
        if (i == g_pageActive)
            canvas->fillCircle(x, y, r, COL_FG);
        else
            canvas->drawCircle(x, y, r, COL_DIM);
    }
}

// Small connection icon at the top center: lightning bolt = realtime websocket,
// cloud = REST fallback. Hidden when the mode is 0 (no connection).
static void drawConnIcon() {
    if (g_connMode == 0) return;  // none = hide; otherwise sits at the top center
    int cx = canvas->width() / 2, y = 40;
    if (g_connMode == 1) {  // websocket — lightning bolt (realtime/instant)
        uint16_t c = COL_OK;
        canvas->fillTriangle(cx + 3, y - 8, cx - 4, y + 2, cx + 1, y + 1, c);
        canvas->fillTriangle(cx - 3, y + 8, cx + 4, y - 2, cx - 1, y - 1, c);
    } else {  // REST — cloud
        uint16_t c = COL_DIM;
        canvas->fillCircle(cx - 6, y + 2, 5, c);
        canvas->fillCircle(cx + 6, y + 2, 5, c);
        canvas->fillCircle(cx, y - 3, 6, c);
        canvas->fillRect(cx - 7, y + 1, 14, 5, c);
    }
}

static uint16_t rgb565(const uint8_t c[3]) { return gfx->color565(c[0], c[1], c[2]); }

// A darker shade of an RGB888 color (e.g. the stats page's inner decorative ring).
static uint16_t darkened(const uint8_t c[3], float f) {
    return gfx->color565((uint8_t)(c[0] * f), (uint8_t)(c[1] * f), (uint8_t)(c[2] * f));
}

// Load the active scheme+mode palette into the COL_* globals. Callers repaint.
static void applyTheme() {
    g_pal = g_darkMode ? &THEMES[g_scheme].dark : &THEMES[g_scheme].light;
    COL_BG = rgb565(g_pal->bg);
    COL_FG = rgb565(g_pal->text);
    COL_DIM = rgb565(g_pal->dim);
    COL_ACCENT = rgb565(g_pal->accent);
    COL_ACCENT2 = rgb565(g_pal->accent2);
    COL_OK = g_darkMode ? gfx->color565(120, 200, 165) : gfx->color565(20, 135, 88);
    COL_WARN = g_darkMode ? gfx->color565(255, 170, 40) : gfx->color565(190, 115, 0);
    COL_ERR = g_darkMode ? gfx->color565(230, 60, 50) : gfx->color565(196, 36, 28);
    COL_CREMA = gfx->color565(175, 115, 60);
    COL_LM_RED = gfx->color565(213, 0, 28);
}

int displayScheme() { return g_scheme; }
const char *displaySchemeName(int scheme) { return THEMES[scheme % THEME_SCHEMES].name; }
bool displayDarkMode() { return g_darkMode; }

void displaySetScheme(int scheme) {
    g_scheme = ((scheme % THEME_SCHEMES) + THEME_SCHEMES) % THEME_SCHEMES;
    applyTheme();
    Preferences p;
    p.begin("display", false);
    p.putInt("scheme", g_scheme);
    p.end();
    LOGF("[display] scheme -> %s\n", THEMES[g_scheme].name);
}

void displaySetDarkMode(bool dark) {
    g_darkMode = dark;
    applyTheme();
    Preferences p;
    p.begin("display", false);
    p.putBool("dark", g_darkMode);
    p.end();
    LOGF("[display] mode -> %s\n", dark ? "dark" : "light");
}

static void formatMMSS(int secs, char *out, size_t n) {
    if (secs < 0) secs = 0;
    snprintf(out, n, "%d:%02d", secs / 60, secs % 60);
}

static void formatElapsed(float seconds, char *out, size_t n) {
    if (seconds < 0) seconds = 0;
    int total = (int)seconds;
    int mins = total / 60;
    int secs = total % 60;
    int tenths = (int)(seconds * 10) % 10;
    if (mins > 0)
        snprintf(out, n, "%d:%02d.%d", mins, secs, tenths);
    else
        snprintf(out, n, "%d.%d", secs, tenths);
}

void displayInit() {
    pinMode(LCD_EN, OUTPUT);
    digitalWrite(LCD_EN, HIGH);  // enable panel power
    LOGF("[display] driver = %s\n", USE_CO5300 ? "CO5300" : "SH8601");

    bool ok = canvas->begin();  // also calls gfx->begin()
    {
        Preferences p;  // user-set rotation (dev DISPLAY page); default from config.h
        p.begin("display", true);
        g_rotation = p.getInt("rot", DISPLAY_ROTATION) & 3;
        p.end();
    }
    canvas->setRotation(g_rotation);  // software rotation (square panel, no HW rotate)
    LOGF("[display] canvas->begin() = %d, framebuffer = %p, free heap = %u\n",
                  ok, (void *)canvas->getFramebuffer(), ESP.getFreeHeap());

    {
        Preferences p;  // user-picked color scheme + dark/light mode (theme settings pages)
        p.begin("display", true);
        g_scheme = p.getInt("scheme", 0) % THEME_SCHEMES;
        g_darkMode = p.getBool("dark", true);
        p.end();
    }
    applyTheme();

    // Fade brightness up to the configured level (some panels start at 0 until written).
    for (int i = 0; i <= DISPLAY_BRIGHTNESS; i += 5) {
        gfx->setBrightness(i);
        delay(2);
    }
    gfx->setBrightness(DISPLAY_BRIGHTNESS);

#if STARTUP_TEST
    LOGLN("[display] startup color test");
    const uint16_t tests[] = {gfx->color565(255, 0, 0), gfx->color565(0, 255, 0),
                              gfx->color565(0, 0, 255), gfx->color565(255, 255, 255)};
    for (uint16_t c : tests) {
        canvas->fillScreen(c);
        canvas->flush();
        delay(500);
    }
#endif

    canvas->fillScreen(COL_BG);
    canvas->flush();
}

void displaySplash(const char *line1, const char *line2) {
    canvas->fillScreen(COL_BG);
    drawCentered(line1, canvas->height() / 2 - 44, &LuckiestGuy36pt7b, COL_FG);
    if (line2 && line2[0])
        drawCentered(line2, canvas->height() / 2 + 44, &LuckiestGuy36pt7b, COL_DIM);
    canvas->flush();
}

void displayLogo() {
    canvas->fillScreen(COL_BG);
    int x = (canvas->width() - LM_LOGO_W) / 2;
    int y = (canvas->height() - LM_LOGO_H) / 2;
    canvas->drawBitmap(x, y, LM_LOGO_BITMAP, LM_LOGO_W, LM_LOGO_H, COL_LM_RED);
    canvas->flush();
}

void displayDark() {
    canvas->fillScreen(0x0000);  // always true black: standby = AMOLED pixels off, any theme
    canvas->flush();
}

void displayMachineOff() {
    canvas->fillScreen(COL_BG);
    int x = (canvas->width() - MACHINE_OFF_W) / 2;
    canvas->draw16bitRGBBitmap(x, 36, (uint16_t *)MACHINE_OFF_BITMAP, MACHINE_OFF_W,
                               MACHINE_OFF_H);
    drawCenteredClassic("MACHINE OFF", 360, 4, COL_DIM);
    drawPageDots();
    canvas->flush();
}

void displayWifiSetup() {
    int cy = canvas->height() / 2;
    canvas->fillScreen(COL_BG);
    canvas->drawCircle(canvas->width() / 2, cy, canvas->width() / 2 - 4, COL_ACCENT2);
    drawCenteredClassic("WIFI SETUP", cy - 70, 3, COL_ACCENT);
    drawCenteredClassic("on your phone, join wifi:", cy - 16, 2, COL_DIM);
    drawCenteredClassic("LaMarzocco-Display", cy + 18, 2, COL_FG);
    drawCenteredClassic("then pick your network", cy + 64, 2, COL_DIM);
    canvas->flush();
}

void displayStats(int shotsToday, int shotsTotal) {
    int cx = canvas->width() / 2, cy = canvas->height() / 2;
    char b[12];

    canvas->fillScreen(COL_BG);

    // Decorative double ring in the scheme accent.
    canvas->drawCircle(cx, cy, canvas->width() / 2 - 4, COL_ACCENT);
    canvas->drawCircle(cx, cy, canvas->width() / 2 - 7, darkened(g_pal->accent, 0.45f));

    // Hero: shots today.
    drawCenteredClassic("SHOTS TODAY", 112, 2, COL_DIM);
    snprintf(b, sizeof(b), "%d", shotsToday);
    drawCentered(b, 188, &LuckiestGuy50pt7b, COL_ACCENT);

    // Crema dots, one per shot today (capped).
    int n = shotsToday > 9 ? 9 : shotsToday;
    int spacing = 26, startx = cx - (n - 1) * spacing / 2;
    for (int i = 0; i < n; i++) canvas->fillCircle(startx + i * spacing, 250, 5, COL_CREMA);

    canvas->drawFastHLine(123, 286, 220, COL_DIM);

    // Lifetime total.
    drawCenteredClassic("LIFETIME", 320, 2, COL_DIM);
    snprintf(b, sizeof(b), "%d", shotsTotal);
    drawCentered(b, 374, &LuckiestGuy36pt7b, COL_FG);

    drawPageDots();
    drawConnIcon();
    canvas->flush();
}

void displayStatus(const BrewState &s) {
    char buf[8];
    int cx = canvas->width() / 2, cy = canvas->height() / 2;

    canvas->fillScreen(COL_BG);

    // Heat-up rings: show the REMAINING fraction so the ring depletes as it warms and is
    // gone once ready. Machine (accent) outer, steam (accent2) inner.
    drawProgressArc(cx, cy, 231, 223, 1.0f - s.coffeeHeatFrac, COL_ACCENT);
    drawProgressArc(cx, cy, 219, 211, 1.0f - s.steamHeatFrac, COL_ACCENT2);

    // Coffee boiler (machine).
    drawCenteredClassic("MACHINE", 116, 3, COL_ACCENT);
    if (s.coffeeReady) {
        drawCentered("READY", 166, &LuckiestGuy36pt7b, COL_OK);
    } else {
        formatMMSS(s.coffeeReadyInSec, buf, sizeof(buf));
        drawCentered(buf, 166, &LuckiestGuy36pt7b, COL_WARN);
    }

    canvas->drawFastHLine(123, 233, 220, COL_DIM);  // divider

    // Steam boiler (steamer).
    drawCenteredClassic("STEAM", 286, 3, COL_ACCENT2);
    if (s.steamReady) {
        drawCentered("READY", 336, &LuckiestGuy36pt7b, COL_OK);
    } else {
        formatMMSS(s.steamReadyInSec, buf, sizeof(buf));
        drawCentered(buf, 336, &LuckiestGuy36pt7b, COL_WARN);
    }

    drawPageDots();
    drawConnIcon();
    canvas->flush();
}

void displayDevInfo(const BrewState &s, int touchPoints, bool demoEnabled) {
    uint16_t green = COL_OK;
    uint16_t red = COL_ERR;

    canvas->fillScreen(COL_BG);
    drawCentered("DEV", 70, &LuckiestGuy36pt7b, COL_ACCENT);

    // Compact left-aligned diagnostics in the built-in font.
    canvas->setFont(NULL);
    canvas->setTextSize(2);
    int x = 78, y = 116;
    const int dy = 25;
    char buf[80];
    auto line = [&](const char *txt, uint16_t c) {
        canvas->setTextColor(c);
        canvas->setCursor(x, y);
        canvas->print(txt);
        y += dy;
    };

    line(demoEnabled ? "demo:   ON" : "demo:   OFF", demoEnabled ? green : COL_DIM);
    bool wifiUp = s.ip[0] != 0;  // having an IP = connected
    snprintf(buf, sizeof(buf), "wifi:   %s", wifiUp ? "up" : "down");
    line(buf, wifiUp ? green : red);
    snprintf(buf, sizeof(buf), "ip:     %s", s.ip[0] ? s.ip : "--");
    line(buf, s.ip[0] ? COL_FG : COL_DIM);
    snprintf(buf, sizeof(buf), "signin: %s", s.signedIn ? "yes" : "no");
    line(buf, s.signedIn ? green : red);
    snprintf(buf, sizeof(buf), "cloud:  %s", s.connected ? "connected" : "--");
    line(buf, s.connected ? green : red);
    snprintf(buf, sizeof(buf), "status: %s", s.status[0] ? s.status : "-");
    line(buf, COL_FG);
    snprintf(buf, sizeof(buf), "heap:   %u", (unsigned)ESP.getFreeHeap());
    line(buf, COL_DIM);
    snprintf(buf, sizeof(buf), "touch:  %d", touchPoints);
    line(buf, COL_DIM);
    if (s.lastError[0]) {
        snprintf(buf, sizeof(buf), "err: %s", s.lastError);
        line(buf, red);
    } else {
        line("err:    none", green);
    }

    drawPageDots();  // 2 dev pages: info + actions
    canvas->flush();
}

void displayDevActions(bool demoEnabled, bool bootlogOn) {
    canvas->fillScreen(COL_BG);
    drawCentered("DEV", 64, &LuckiestGuy36pt7b, COL_ACCENT);
    drawButton(BTN_X, DEV_BTN_A_Y, BTN_W, BTN_H, demoEnabled ? "DEMO ON" : "DEMO OFF",
               demoEnabled ? COL_OK : COL_DIM);
    drawButton(BTN_X, DEV_BTN_B_Y, BTN_W, BTN_H, bootlogOn ? "BOOTLOG ON" : "BOOTLOG OFF",
               bootlogOn ? COL_OK : COL_DIM);
    drawButton(BTN_X, DEV_BTN_C_Y, BTN_W, BTN_H, "RESET DEVICE", COL_WARN);
    drawPageDots();
    canvas->flush();
}

void displayDevDisplay(int rotation) {
    canvas->fillScreen(COL_BG);
    drawCentered("DEV", 64, &LuckiestGuy36pt7b, COL_ACCENT);
    static const char *cable[] = {"left", "bottom", "right", "top"};
    char b[28];
    snprintf(b, sizeof(b), "rotation: %d", (rotation & 3) * 90);
    drawCenteredClassic(b, 150, 2, COL_FG);
    snprintf(b, sizeof(b), "usb cable: %s", cable[rotation & 3]);
    drawCenteredClassic(b, 178, 2, COL_DIM);
    drawButton(BTN_X, DEV_BTN_B_Y, BTN_W, BTN_H, "ROTATE 90", COL_ACCENT2);
    drawCenteredClassic("tap until it looks right", 320, 2, COL_DIM);
    drawPageDots();
    canvas->flush();
}

// Settings: color-scheme picker. One swatch per machine color, drawn in that scheme's own
// accent (for the CURRENT dark/light mode); the active scheme is filled, the rest outlined.
void displaySettingsTheme() {
    canvas->fillScreen(COL_BG);
    drawCentered("THEME", 88, &LuckiestGuy36pt7b, COL_ACCENT);  // lower than DEV: wider word,
                                                                // must clear the round mask

    static const int xs[2] = {THEME_SW_X0, THEME_SW_X1};
    static const int ys[3] = {THEME_SW_Y0, THEME_SW_Y1, THEME_SW_Y2};
    for (int i = 0; i < THEME_SCHEMES; i++) {
        int x = xs[i % 2], y = ys[i / 2];
        const ThemePal &p = g_darkMode ? THEMES[i].dark : THEMES[i].light;
        uint16_t c = rgb565(p.accent);
        if (i == g_scheme) {
            canvas->fillRoundRect(x, y, THEME_SW_W, THEME_SW_H, 14, c);
            drawCenteredClassicAt(THEMES[i].name, x + THEME_SW_W / 2, y + THEME_SW_H / 2, 2,
                                  COL_BG);
        } else {
            canvas->drawRoundRect(x, y, THEME_SW_W, THEME_SW_H, 14, c);
            canvas->drawRoundRect(x + 1, y + 1, THEME_SW_W - 2, THEME_SW_H - 2, 13, c);
            drawCenteredClassicAt(THEMES[i].name, x + THEME_SW_W / 2, y + THEME_SW_H / 2, 2, c);
        }
    }

    drawCenteredClassic("machine colors", 372, 2, COL_DIM);
    drawPageDots();
    canvas->flush();
}

// Settings: dark / light mode toggle for the active scheme.
void displaySettingsMode() {
    canvas->fillScreen(COL_BG);
    drawCentered("DISPLAY", 88, &LuckiestGuy36pt7b, COL_ACCENT);
    if (g_darkMode)
        drawButtonFilled(BTN_X, BTN_A_Y, BTN_W, BTN_H, "DARK", COL_FG);
    else
        drawButton(BTN_X, BTN_A_Y, BTN_W, BTN_H, "DARK", COL_DIM);
    if (!g_darkMode)
        drawButtonFilled(BTN_X, BTN_B_Y, BTN_W, BTN_H, "LIGHT", COL_FG);
    else
        drawButton(BTN_X, BTN_B_Y, BTN_W, BTN_H, "LIGHT", COL_DIM);
    drawCenteredClassic("dark saves power (AMOLED)", 372, 2, COL_DIM);
    drawPageDots();
    canvas->flush();
}

// Boot-log console: the most recent LOG lines, updated live while starting up.
void displayBootlog() {
    canvas->fillScreen(COL_BG);
    drawCenteredClassic("BOOT LOG", 56, 2, COL_ACCENT);

    char lines[BOOTLOG_LINES][BOOTLOG_COLS + 1];
    int n = bootlogGetLines(lines, BOOTLOG_LINES);
    canvas->setFont(NULL);
    canvas->setTextSize(2);
    canvas->setTextColor(COL_FG);
    int y = 92;
    for (int i = 0; i < n; i++) {
        canvas->setCursor(66, y);
        canvas->print(lines[i]);
        y += 22;
    }

    drawCenteredClassic("tap to continue", 414, 2, COL_DIM);
    canvas->flush();
}

void displayResetConfirm() {
    uint16_t red = COL_ERR;
    canvas->fillScreen(COL_BG);
    drawCenteredClassic("RESET DEVICE?", 92, 3, red);
    drawCenteredClassic("clears WiFi + LM account", 132, 2, COL_DIM);
    drawButton(BTN_X, BTN_A_Y, BTN_W, BTN_H, "CONFIRM", red);
    drawButton(BTN_X, BTN_B_Y, BTN_W, BTN_H, "CANCEL", COL_FG);
    canvas->flush();
}

// Backflush / cleaning screen. ui: 0=idle (START), 1=confirm, 2=running, 3=done.
void displayBackflush(const BrewState &s, int ui) {
    uint16_t green = COL_OK;
    int cx = canvas->width() / 2, cy = canvas->height() / 2;
    canvas->fillScreen(COL_BG);

    if (ui == 1) {  // confirm modal (no dots/icon — same pattern as reset-confirm)
        drawCenteredClassic("BACKFLUSH?", 92, 3, COL_ACCENT2);
        drawCenteredClassic("insert blind filter", 130, 2, COL_DIM);
        drawCenteredClassic("+ detergent", 156, 2, COL_DIM);
        drawButton(BTN_X, BTN_A_Y + 8, BTN_W, BTN_H, "CONFIRM", green);
        drawButton(BTN_X, BTN_B_Y, BTN_W, BTN_H, "CANCEL", COL_FG);
        canvas->flush();
        return;
    }

    if (ui == 3) {  // done
        drawCentered("DONE", cy - 10, &LuckiestGuy36pt7b, green);
        drawCenteredClassic("cleaning complete", cy + 40, 2, COL_DIM);
        drawPageDots();
        drawConnIcon();
        canvas->flush();
        return;
    }

    if (ui == 2 || s.backflushStatus != 0) {  // running (Requested / Cleaning)
        // Spinner: a faint full ring + a dot rotating around it.
        drawProgressArc(cx, cy, 229, 225, 1.0f, COL_DIM);
        float ang = (millis() % 1200) / 1200.0f * 2.0f * (float)M_PI - (float)M_PI / 2.0f;
        int dx = cx + (int)(cosf(ang) * 216), dy = cy + (int)(sinf(ang) * 216);
        canvas->fillCircle(dx, dy, 8, COL_ACCENT2);
        const char *label = (s.backflushStatus == 2) ? "CLEANING" : "STARTING";
        drawCentered(label, cy - 8, &LuckiestGuy36pt7b, COL_ACCENT2);
        drawCenteredClassic("keep filter in", 300, 2, COL_DIM);
        drawCenteredClassic("until it stops", 326, 2, COL_DIM);
        drawPageDots();
        drawConnIcon();
        canvas->flush();
        return;
    }

    // Idle: title, last-cleaned info, and a START button (gated when the machine is off).
    drawCentered("BACKFLUSH", 100, &LuckiestGuy36pt7b, COL_ACCENT2);
    bool off = (strcmp(s.status, "Off") == 0);
    if (off) {
        drawButton(BTN_X, BTN_A_Y, BTN_W, BTN_H, "MACHINE OFF", COL_DIM);
        drawCenteredClassic("turn machine on first", 272, 2, COL_DIM);
    } else {
        drawButton(BTN_X, BTN_A_Y, BTN_W, BTN_H, "START", green);
        if (s.lastCleaningStartMs) {
            long days = ((long)time(nullptr) - (long)(s.lastCleaningStartMs / 1000)) / 86400L;
            char lc[28];
            if (days <= 0)
                snprintf(lc, sizeof(lc), "cleaned today");
            else if (days == 1)
                snprintf(lc, sizeof(lc), "cleaned yesterday");
            else
                snprintf(lc, sizeof(lc), "cleaned %ld days ago", days);
            drawCenteredClassic(lc, 272, 2, COL_DIM);
        }
    }
    drawPageDots();
    drawConnIcon();
    canvas->flush();
}

void displayTimer(float seconds, bool frozen, float steamFrac, float preInfusionSec) {
    char buf[16];
    formatElapsed(seconds, buf, sizeof(buf));  // center timer = total shot time

    const int w = canvas->width();
    const int h = canvas->height();
    const int cx = w / 2, cy = h / 2;

    canvas->fillScreen(COL_BG);

    // Pre-infusion is the first preInfusionSec of the shot; the cup fills only afterward.
    bool inPre = (!frozen && preInfusionSec > 0.1f && seconds < preInfusionSec);
    float shotSeconds = (preInfusionSec > 0.1f) ? (seconds - preInfusionSec) : seconds;
    if (shotSeconds < 0) shotSeconds = 0;

    // Coffee rises from the bottom; full after COFFEE_FILL_SECONDS (post pre-infusion).
    float frac = shotSeconds / COFFEE_FILL_SECONDS;
    if (frac > 1.0f) frac = 1.0f;
    int baseSurfaceY = (int)(h * (1.0f - frac));

    // Over-extraction: time past full, normalised 0..1 over OVEREXTRACT_SECONDS.
    float over = (shotSeconds - COFFEE_FILL_SECONDS) / OVEREXTRACT_SECONDS;
    if (over < 0) over = 0;
    if (over > 1) over = 1;

    // Coffee washes out from rich espresso toward a pale "watery" tone as it over-extracts.
    uint16_t bodyCol = lerp565(70, 35, 14, 150, 122, 96, over);
    uint16_t cremaCol = lerp565(175, 115, 60, 188, 168, 142, over);

    // Gentle wavy surface while brewing (still when frozen).
    const float amp = frozen ? 0.0f : 5.0f;
    for (int x = 0; x < w; x++) {
        int surfaceY = baseSurfaceY + (int)(amp * sinf(x * 0.045f + seconds * 3.0f));
        if (surfaceY < 0) surfaceY = 0;
        if (surfaceY >= h) continue;
        canvas->drawFastVLine(x, surfaceY, h - surfaceY, bodyCol);
        int crema = (surfaceY + 10 < h) ? 10 : (h - surfaceY);
        if (crema > 0) canvas->drawFastVLine(x, surfaceY, crema, cremaCol);
    }

    // Two espresso streams pouring from the top into the cup (while brewing). Each stream
    // wavers (the espresso "mouse tail"), converges toward center as it falls, carries a
    // crema shimmer flowing downward, and splashes where it meets the surface.
    if (!frozen && !inPre && baseSurfaceY > 2) {
        uint16_t streamBody = lerp565(120, 70, 34, 150, 122, 96, over);  // mid espresso
        int phase = (int)(seconds * 150.0f);  // downward shimmer speed
        for (int s = 0; s < 2; s++) {
            float dir = (s == 0) ? -1.0f : 1.0f;
            int impactX = cx;
            for (int y = 0; y < baseSurfaceY; y++) {
                float t = (float)y / baseSurfaceY;            // 0 at top, 1 at surface
                float spread = 16.0f * (1.0f - t) + 5.0f * t; // converge 16px -> 5px
                float wob = sinf(y * 0.07f + seconds * 7.0f + s * PI) * (1.0f + 3.0f * t);
                int sx = cx + (int)(dir * spread + wob);
                int half = (t < 0.5f) ? 1 : 2;               // taper: thin top, fuller low
                // crema shimmer travelling downward
                bool hi = ((((y - phase) % 13) + 13) % 13) < 4;
                canvas->drawFastHLine(sx - half, y, half * 2 + 1, hi ? cremaCol : streamBody);
                impactX = sx;
            }
            // splash / ripple where the stream meets the coffee
            canvas->drawFastHLine(impactX - 7, baseSurfaceY - 1, 15, cremaCol);
            canvas->drawFastHLine(impactX - 4, baseSurfaceY - 2, 9, cremaCol);
        }
    }

    // Pre-infusion phase: teal ring filling over the configured time, slow low-pressure drips,
    // and a label. The cup stays empty until pre-infusion ends, then the fill begins.
    if (inPre) {
        drawProgressArc(cx, cy, 231, 223, seconds / preInfusionSec, COL_ACCENT2);
        int phase = (int)(seconds * 38.0f);  // slow drip
        for (int sidx = 0; sidx < 2; sidx++) {
            int sx = cx + (sidx == 0 ? -10 : 10);
            for (int y = 0; y < cy + 30; y++)
                if ((((y - phase) % 40) + 40) % 40 < 3) canvas->drawFastHLine(sx - 1, y, 3, COL_ACCENT2);
        }
        drawCenteredClassic("PRE-INFUSION", cy - 78, 2, COL_ACCENT2);
    }

    // Steam heat-up ring (blue) overlaid at the edge, so you can see steam readiness while
    // pulling the shot. Shown only when requested (steamFrac >= 0) and not during pre-infusion.
    if (steamFrac >= 0 && !inPre) drawProgressArc(cx, cy, 231, 223, steamFrac, COL_ACCENT2);

    // Timer in the center. White normally; warns white -> amber -> red while over-extracting.
    // Deliberately NOT themed: it sits on the coffee fill (dark in every scheme), so light
    // mode's dark text color would vanish against it.
    uint16_t timerCol;
    if (over <= 0)
        timerCol = COL_FG;
    else if (over < 0.5f)
        timerCol = lerp565(245, 245, 245, 255, 170, 40, over / 0.5f);
    else
        timerCol = lerp565(255, 170, 40, 230, 45, 30, (over - 0.5f) / 0.5f);
    drawCentered(buf, cy, &LuckiestGuy50pt7b, timerCol);

    drawPageDots();  // shown only when a page indicator is active (gallery preview)
    drawConnIcon();
    canvas->flush();
}
