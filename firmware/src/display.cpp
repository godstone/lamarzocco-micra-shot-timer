#include "display.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

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

static uint16_t COL_BG;
static uint16_t COL_FG;
static uint16_t COL_ACCENT;  // La Marzocco red-ish
static uint16_t COL_DIM;
static uint16_t COL_COFFEE;  // espresso body
static uint16_t COL_CREMA;   // lighter crema band
static uint16_t COL_LM_RED;   // brand red
static uint16_t COL_MACHINE;  // coffee-boiler ring (pink/purple)
static uint16_t COL_STEAM;    // steam-boiler ring (blue)
static uint16_t COL_PREINF;   // pre-infusion accent (teal)

static int g_pageActive = 0;  // page-indicator state
static int g_pageCount = 0;
static int g_connMode = 0;  // 0=none, 1=websocket, 2=cloud/REST

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
        uint16_t c = gfx->color565(120, 210, 175);  // mint
        canvas->fillTriangle(cx + 3, y - 8, cx - 4, y + 2, cx + 1, y + 1, c);
        canvas->fillTriangle(cx - 3, y + 8, cx + 4, y - 2, cx - 1, y - 1, c);
    } else {  // REST — cloud
        uint16_t c = gfx->color565(140, 155, 175);  // soft blue-gray
        canvas->fillCircle(cx - 6, y + 2, 5, c);
        canvas->fillCircle(cx + 6, y + 2, 5, c);
        canvas->fillCircle(cx, y - 3, 6, c);
        canvas->fillRect(cx - 7, y + 1, 14, 5, c);
    }
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
    canvas->setRotation(DISPLAY_ROTATION);  // software rotation (square panel, no HW rotate)
    LOGF("[display] canvas->begin() = %d, framebuffer = %p, free heap = %u\n",
                  ok, (void *)canvas->getFramebuffer(), ESP.getFreeHeap());

    COL_BG = gfx->color565(0, 0, 0);
    COL_FG = gfx->color565(245, 245, 245);
    COL_ACCENT = gfx->color565(220, 40, 40);
    COL_DIM = gfx->color565(90, 90, 90);
    COL_COFFEE = gfx->color565(70, 35, 14);    // dark espresso
    COL_CREMA = gfx->color565(175, 115, 60);   // crema
    COL_LM_RED = gfx->color565(213, 0, 28);    // La Marzocco brand red
    COL_MACHINE = gfx->color565(232, 70, 200);  // pink/purple
    COL_STEAM = gfx->color565(40, 130, 255);    // blue
    COL_PREINF = gfx->color565(80, 210, 200);   // teal

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
    canvas->fillScreen(COL_BG);
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
    canvas->drawCircle(canvas->width() / 2, cy, canvas->width() / 2 - 4, COL_STEAM);
    drawCenteredClassic("WIFI SETUP", cy - 70, 3, COL_LM_RED);
    drawCenteredClassic("on your phone, join wifi:", cy - 16, 2, COL_DIM);
    drawCenteredClassic("LaMarzocco-Display", cy + 18, 2, COL_FG);
    drawCenteredClassic("then pick your network", cy + 64, 2, COL_DIM);
    canvas->flush();
}

void displayStats(int shotsToday, int shotsTotal) {
    int cx = canvas->width() / 2, cy = canvas->height() / 2;
    char b[12];

    canvas->fillScreen(COL_BG);

    // Decorative double ring in brand red.
    canvas->drawCircle(cx, cy, canvas->width() / 2 - 4, COL_LM_RED);
    canvas->drawCircle(cx, cy, canvas->width() / 2 - 7, gfx->color565(95, 20, 24));

    // Hero: shots today.
    drawCenteredClassic("SHOTS TODAY", 112, 2, COL_DIM);
    snprintf(b, sizeof(b), "%d", shotsToday);
    drawCentered(b, 188, &LuckiestGuy50pt7b, COL_LM_RED);

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
    uint16_t green = gfx->color565(120, 200, 165);  // faded mint
    uint16_t amber = gfx->color565(255, 170, 40);
    char buf[8];
    int cx = canvas->width() / 2, cy = canvas->height() / 2;

    canvas->fillScreen(COL_BG);

    // Heat-up rings: show the REMAINING fraction so the ring depletes as it warms and is
    // gone once ready. Machine (pink/purple) outer, steam (blue) inner.
    drawProgressArc(cx, cy, 231, 223, 1.0f - s.coffeeHeatFrac, COL_MACHINE);
    drawProgressArc(cx, cy, 219, 211, 1.0f - s.steamHeatFrac, COL_STEAM);

    // Coffee boiler (machine).
    drawCenteredClassic("MACHINE", 116, 3, COL_MACHINE);
    if (s.coffeeReady) {
        drawCentered("READY", 166, &LuckiestGuy36pt7b, green);
    } else {
        formatMMSS(s.coffeeReadyInSec, buf, sizeof(buf));
        drawCentered(buf, 166, &LuckiestGuy36pt7b, amber);
    }

    canvas->drawFastHLine(123, 233, 220, COL_DIM);  // divider

    // Steam boiler (steamer).
    drawCenteredClassic("STEAM", 286, 3, COL_STEAM);
    if (s.steamReady) {
        drawCentered("READY", 336, &LuckiestGuy36pt7b, green);
    } else {
        formatMMSS(s.steamReadyInSec, buf, sizeof(buf));
        drawCentered(buf, 336, &LuckiestGuy36pt7b, amber);
    }

    drawPageDots();
    drawConnIcon();
    canvas->flush();
}

void displayDevInfo(const BrewState &s, int touchPoints, bool demoEnabled) {
    uint16_t green = gfx->color565(40, 200, 90);
    uint16_t red = gfx->color565(230, 60, 50);

    canvas->fillScreen(COL_BG);
    drawCentered("DEV", 70, &LuckiestGuy36pt7b, COL_LM_RED);

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

void displayDevActions(bool demoEnabled) {
    uint16_t green = gfx->color565(120, 200, 165);
    uint16_t amber = gfx->color565(255, 150, 60);
    canvas->fillScreen(COL_BG);
    drawCentered("DEV", 74, &LuckiestGuy36pt7b, COL_LM_RED);
    drawButton(BTN_X, BTN_A_Y, BTN_W, BTN_H, demoEnabled ? "DEMO ON" : "DEMO OFF",
               demoEnabled ? green : COL_DIM);
    drawButton(BTN_X, BTN_B_Y, BTN_W, BTN_H, "RESET DEVICE", amber);
    drawPageDots();
    canvas->flush();
}

void displayResetConfirm() {
    uint16_t red = gfx->color565(230, 60, 50);
    canvas->fillScreen(COL_BG);
    drawCenteredClassic("RESET DEVICE?", 92, 3, red);
    drawCenteredClassic("clears WiFi + LM account", 132, 2, COL_DIM);
    drawButton(BTN_X, BTN_A_Y, BTN_W, BTN_H, "CONFIRM", red);
    drawButton(BTN_X, BTN_B_Y, BTN_W, BTN_H, "CANCEL", COL_FG);
    canvas->flush();
}

// Backflush / cleaning screen. ui: 0=idle (START), 1=confirm, 2=running, 3=done.
void displayBackflush(const BrewState &s, int ui) {
    uint16_t green = gfx->color565(120, 200, 165);
    int cx = canvas->width() / 2, cy = canvas->height() / 2;
    canvas->fillScreen(COL_BG);

    if (ui == 1) {  // confirm modal (no dots/icon — same pattern as reset-confirm)
        drawCenteredClassic("BACKFLUSH?", 92, 3, COL_STEAM);
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
        canvas->fillCircle(dx, dy, 8, COL_STEAM);
        const char *label = (s.backflushStatus == 2) ? "CLEANING" : "STARTING";
        drawCentered(label, cy - 8, &LuckiestGuy36pt7b, COL_STEAM);
        drawCenteredClassic("keep filter in", 300, 2, COL_DIM);
        drawCenteredClassic("until it stops", 326, 2, COL_DIM);
        drawPageDots();
        drawConnIcon();
        canvas->flush();
        return;
    }

    // Idle: title, last-cleaned info, and a START button (gated when the machine is off).
    drawCentered("BACKFLUSH", 100, &LuckiestGuy36pt7b, COL_STEAM);
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
        drawProgressArc(cx, cy, 231, 223, seconds / preInfusionSec, COL_PREINF);
        int phase = (int)(seconds * 38.0f);  // slow drip
        for (int sidx = 0; sidx < 2; sidx++) {
            int sx = cx + (sidx == 0 ? -10 : 10);
            for (int y = 0; y < cy + 30; y++)
                if ((((y - phase) % 40) + 40) % 40 < 3) canvas->drawFastHLine(sx - 1, y, 3, COL_PREINF);
        }
        drawCenteredClassic("PRE-INFUSION", cy - 78, 2, COL_PREINF);
    }

    // Steam heat-up ring (blue) overlaid at the edge, so you can see steam readiness while
    // pulling the shot. Shown only when requested (steamFrac >= 0) and not during pre-infusion.
    if (steamFrac >= 0 && !inPre) drawProgressArc(cx, cy, 231, 223, steamFrac, COL_STEAM);

    // Timer in the center. White normally; warns white -> amber -> red while over-extracting.
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
