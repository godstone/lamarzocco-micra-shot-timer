#include <Arduino.h>

#include "config.h"
#include "display.h"
#include "lm_client.h"
#include "log.h"
#include "touch.h"

// WiFi + NTP + cloud connection are handled inside lm_client (LIVE mode); see lmBegin().

enum Mode { MODE_IDLE, MODE_BREW, MODE_FROZEN };
static Mode g_mode = MODE_IDLE;
static float g_lastElapsed = 0;  // captured at the moment brewing stops
static uint32_t g_frozenAt = 0;  // millis() when the shot ended

// Develop mode: toggled by a two-finger touch; shows a diagnostics overlay.
static bool g_dev = false;
static uint32_t g_lastDevRender = 0;
#define IDLE_PAGES 2  // swipe carousel: 0 = status (off/readiness), 1 = stats

static int g_idleScreen = -1;  // last rendered screen id; -1 forces redraw
static long g_idleData = -1;
static int g_idlePage = 0;  // 0 = status page (machine-off or readiness), 1 = stats
static uint32_t g_idleLastRender = 0;

// Demo gallery: a manual, looping carousel of all four screens (decoupled from machine state).
#define GALLERY_PAGES 4
static int g_galleryPage = 0;  // 0=off, 1=timer, 2=shot, 3=stats
static int g_galleryLastPage = -1;
static uint32_t g_galleryAnimBase = 0;
static uint32_t g_galleryLastRenderMs = 0;

// Render the swipe-selected gallery page with its own looping animation.
static void renderGallery(uint32_t now) {
    displaySetPageIndicator(g_galleryPage, GALLERY_PAGES);
    bool pageChanged = (g_galleryPage != g_galleryLastPage);
    float t = (now - g_galleryAnimBase) / 1000.0f;

    switch (g_galleryPage) {
        case 0:  // machine off (static)
            if (pageChanged) displayMachineOff();
            break;
        case 1: {  // machine/steam heat-up rings, looping
            BrewState v;
            float lt = fmodf(t, 34.0f);
            v.connected = true;
            v.coffeeReadyTotalSec = 22;
            v.steamReadyTotalSec = 30;
            v.coffeeHeatFrac = min(1.0f, lt / 22.0f);
            v.steamHeatFrac = min(1.0f, lt / 30.0f);
            v.coffeeReadyInSec = max(0, (int)(22.0f - lt + 0.999f));
            v.steamReadyInSec = max(0, (int)(30.0f - lt + 0.999f));
            v.coffeeReady = v.coffeeHeatFrac >= 1.0f;
            v.steamReady = v.steamHeatFrac >= 1.0f;
            strncpy(v.status, "StandBy", sizeof(v.status));
            displayStatus(v);
            break;
        }
        case 2: {  // shot: pre-infusion -> looping coffee fill, steam ring depleting then gone
            float st = fmodf(t, 44.0f);
            float sf = 1.0f - st / 34.0f;
            if (sf <= 0.0f) sf = -1.0f;
            displayTimer(st, false, sf, 4.0f);  // demo 4s pre-infusion
            break;
        }
        case 3:  // stats (static)
            if (pageChanged) displayStats(lmState().shotsToday, lmState().shotsTotal);
            break;
    }
    g_galleryLastPage = g_galleryPage;
}

// Pick and render the idle screen. Off -> machine-off; otherwise the swipe-selected page
// (readiness with heat-up rings, or fancy stats). While heating, the readiness page is
// re-rendered at ~30fps so the rings animate smoothly; everything else redraws only on change.
static void renderIdle() {
    const BrewState &s = lmState();
    uint32_t now = millis();
    displaySetConn(s.connMode);

    // WiFi captive portal open -> show the setup prompt (overrides everything).
    if (s.wifiPortal) {
        if (g_idleScreen != 9) {
            displaySetPageIndicator(0, 0);
            displayWifiSetup();
            g_idleScreen = 9;
        }
        return;
    }

    bool off = (strcmp(s.status, "Off") == 0) || !s.connected;
    displaySetPageIndicator(g_idlePage, IDLE_PAGES);

    if (g_idlePage == 0) {  // status page: machine-off, or readiness with heat-up rings
        if (off) {
            if (g_idleScreen != 0) {
                displayMachineOff();
                g_idleScreen = 0;
                g_idleData = 0;
            }
            return;
        }
        bool heating = !(s.coffeeReady && s.steamReady);
        if (heating) {
            if (now - g_idleLastRender >= 33) {  // smooth ring animation
                displayStatus(s);
                g_idleLastRender = now;
                g_idleScreen = 1;
                g_idleData = -1;  // ensure a redraw when it later goes static
            }
        } else if (g_idleScreen != 1 || g_idleData != -2) {
            displayStatus(s);  // both ready: draw once (static full rings)
            g_idleScreen = 1;
            g_idleData = -2;
        }
    } else {  // stats page
        long key = (long)s.shotsToday * 1000000L + s.shotsTotal;
        if (g_idleScreen != 3 || g_idleData != key) {
            displayStats(s.shotsToday, s.shotsTotal);
            g_idleScreen = 3;
            g_idleData = key;
        }
    }
}

// Repaint the current normal-mode screen (used when leaving dev mode).
static void repaintCurrent() {
    if (g_mode == MODE_FROZEN)
        displayTimer(g_lastElapsed, true, -1.0f, 0.0f);
    else if (g_mode != MODE_BREW)
        g_idleScreen = -1;  // force the idle screen to redraw next loop
}

void setup() {
    Serial.begin(115200);
    delay(300);  // let USB CDC enumerate so early logs aren't lost
    LOGLN("\n[boot] La Marzocco brew-timer display");
    LOGF("[boot] free heap: %u, PSRAM: %u\n", ESP.getFreeHeap(),
                  ESP.getPsramSize());

    displayInit();
    LOGLN("[boot] display init done");

    displayLogo();
    delay(1800);

    touchBegin();
    lmBegin();  // connects WiFi + NTP and starts the cloud poller (LIVE mode)
    g_mode = MODE_IDLE;
    g_idleScreen = -1;  // idle screen renders on the first loop
}

void loop() {
    lmPoll();
    uint32_t now = millis();

    // Always return to the status page after the machine powers off (consistent start order).
    static bool prevOff = true;
    bool offNow = (strcmp(lmState().status, "Off") == 0) || !lmState().connected;
    if (offNow && !prevOff) {
        g_idlePage = 0;
        g_idleScreen = -1;
    }
    prevOff = offNow;

    // Entering demo mode -> start the gallery on page 0 for a consistent order.
    static bool prevDemo = false;
    if (lmDemo() && !prevDemo) {
        g_galleryPage = 0;
        g_galleryLastPage = -1;
        g_galleryAnimBase = now;
    }
    prevDemo = lmDemo();

    // Develop mode: open with a long-press (hold ~1.5s), close with a quick double-tap.
    // (This panel reliably reports only one touch point, so we use single-finger gestures.)
    // One read gives both presence and coordinates, so a quick tap isn't lost between reads.
    int tx = 0, ty = 0;
    bool down = touchPoint(&tx, &ty);
    static bool prevDown = false;
    static uint32_t pressStart = 0;
    static bool longFired = false;
    static uint32_t lastTapMs = 0;
    static int tapCount = 0;

    bool rising = down && !prevDown;
    prevDown = down;

    // Horizontal swipe -> step the idle page (wrapping loop). Detected MID-gesture (as soon
    // as you've moved far enough), which is robust to brief touch dropouts on release.
    static int swStartX = 0, swStartY = 0;
    static bool swiped = false;
    if (rising) {
        swStartX = tx;
        swStartY = ty;
        swiped = false;
    }
    if (down && !swiped && !g_dev) {
        int dx = tx - swStartX, dy = ty - swStartY;
        if (abs(dx) > 55 && abs(dx) > abs(dy)) {
            int dir = (dx < 0) ? 1 : -1;  // swipe left = next (flick current screen away)
            if (lmDemo()) {  // gallery: 4-page loop
                g_galleryPage = (g_galleryPage + dir + GALLERY_PAGES) % GALLERY_PAGES;
                g_galleryLastPage = -1;     // force redraw
                g_galleryAnimBase = now;    // restart the new page's animation cleanly
            } else {  // real idle: 2-page loop
                g_idlePage = (g_idlePage + dir + IDLE_PAGES) % IDLE_PAGES;
                g_idleScreen = -1;
            }
            swiped = true;
            tapCount = 0;  // a swipe isn't a tap
        }
    }
    if (!down) swiped = false;

    // DEMO button: toggle on press with an INSTANT redraw, then debounce. Generous hit-box
    // (measured taps land ~y450, low vs. the drawn button); nothing else is tappable there.
    if (rising && g_dev && tx >= DEV_BTN_X - 30 && tx <= DEV_BTN_X + DEV_BTN_W + 30 &&
        ty >= DEV_BTN_Y - 25 && ty <= DEV_BTN_Y + DEV_BTN_H + 45) {
        lmSetDemo(!lmDemo());
        displayDevInfo(lmState(), 1, lmDemo());  // immediate feedback
        g_lastDevRender = now;
        delay(180);  // debounce the press
        return;
    }

    if (rising) {
        tapCount = (now - lastTapMs < 800) ? tapCount + 1 : 1;
        lastTapMs = now;
    }
    if (down) {
        if (pressStart == 0) pressStart = now;
    } else {
        pressStart = 0;
        longFired = false;
    }

    if (!g_dev) {
        // long-press to open
        if (down && !longFired && now - pressStart >= 1500) {
            g_dev = true;
            longFired = true;
            tapCount = 0;
        }
    } else if (tapCount >= 2) {
        // double-tap to close
        g_dev = false;
        tapCount = 0;
        repaintCurrent();
    }

    if (g_dev) {
        if (now - g_lastDevRender > 250) {
            displayDevInfo(lmState(), down ? 1 : 0, lmDemo());
            g_lastDevRender = now;
        }
        delay(33);
        return;
    }

    // Demo mode is a manual gallery of all screens; real mode is state-driven.
    if (lmDemo()) {
        // Cap animation to ~25fps and spin fast between frames so swipes are sampled densely
        // (a full-frame render blocks ~40ms; without this the shot page misses swipes).
        if (g_galleryPage != g_galleryLastPage || now - g_galleryLastRenderMs >= 40) {
            renderGallery(now);
            g_galleryLastRenderMs = now;
        }
        delay(6);
        return;
    }

    displaySetPageIndicator(0, 0);  // no page dots on live brew/frozen screens
    bool brewing = lmState().brewing;

    if (brewing) {
        // Animate the coffee fill + timer every frame for smoothness.
        const BrewState &s = lmState();
        g_lastElapsed = lmElapsedSeconds();
        g_mode = MODE_BREW;
        displaySetConn(s.connMode);
        // Steam ring while pulling: remaining fraction (depletes as it heats), gone when ready.
        float steamFrac = s.steamReady ? -1.0f : (1.0f - s.steamHeatFrac);
        float preSec = s.preInfusionOn ? s.preInfusionSec : 0.0f;  // configured in the app
        displayTimer(g_lastElapsed, false, steamFrac, preSec);
    } else if (g_mode == MODE_BREW) {
        // Shot just ended — freeze the final cup + time and start the hold timer.
        g_mode = MODE_FROZEN;
        g_frozenAt = now;
        displayTimer(g_lastElapsed, true, -1.0f, 0.0f);  // rendered once; frame stays on screen
    } else if (g_mode == MODE_FROZEN) {
        if (now - g_frozenAt >= POST_SHOT_HOLD_MS) {
            g_mode = MODE_IDLE;
            g_idleScreen = -1;  // force the idle screen to draw
        }
    }

    if (g_mode == MODE_IDLE) {
        // Standby: machine-off, heat-up countdown, or fancy shot stats.
        renderIdle();
    }

    delay(33);  // ~30 fps while brewing
}
