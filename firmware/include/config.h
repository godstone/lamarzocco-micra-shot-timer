// Tunable behaviour for the brew-timer display.
#pragma once

// Panel brightness 0-255. AMOLED is very bright; ~140 is comfortable indoors.
#ifndef DISPLAY_BRIGHTNESS
#define DISPLAY_BRIGHTNESS 140
#endif

// Default screen rotation 0-3 (software, on the canvas — the CO5300 has no HW rotation).
// 0 = cable on the left. Use 1 or 3 to put the cable at the bottom/top; 2 = flip 180.
// Runtime-changeable (and persisted) from the dev DISPLAY page; this is only the default.
#define DISPLAY_ROTATION 1

// Seconds for the coffee-fill animation to rise from empty (bottom) to full (top).
#define COFFEE_FILL_SECONDS 30.0f

// After the cup is full, seconds of over-extraction over which the coffee washes out to a
// pale "watery" tone and the timer shifts white -> amber -> red (over-extraction warning).
#define OVEREXTRACT_SECONDS 15.0f

// After a shot ends, freeze the final time + filled cup on screen for this long, then go dark.
#define POST_SHOT_HOLD_MS 15000

// Screen standby (AMOLED longevity): go fully dark after this long with no touch and no machine
// events. Any touch wakes it (the waking touch is swallowed), as does any machine change —
// power on/off, brew start/stop, boiler ready, backflush, WiFi portal opening.
#define STANDBY_AFTER_MS (15UL * 60UL * 1000UL)

// Multi-WiFi: remember up to this many networks (most-recently-used order). Boot connects to
// whichever is visible (best signal first); the captive portal only opens when none is. While
// disconnected, rescan for known networks this often — the longer interval applies while the
// portal is open, so scans don't stall its web UI while someone is configuring.
#define WIFI_MAX_NETWORKS 5
#define WIFI_ROAM_RETRY_MS 30000
#define WIFI_PORTAL_ROAM_MS 60000

// NTP — needed for TLS cert validity and to compute elapsed = now - brewingStartTime.
#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET_SEC 0
#define DAYLIGHT_OFFSET_SEC 0

// LIVE mode: how often the background task polls the cloud dashboard (ms). The timer counts
// up locally between polls, so this only bounds brew-start detection latency.
#define LIVE_POLL_INTERVAL_MS 2000
// How often to refresh the shot-count stats (two extra HTTPS requests; skipped while brewing).
#define STATS_REFRESH_MS 60000
// LM_TIMEZONE (for the "shots today" trend query) comes from .env -> secrets.h; lm_client.cpp
// provides a default if it's not set there.

// Calibration: seconds subtracted from the live shot timer to match the official app
// (e.g. the machine's brewingStartTime includes pre-infusion). Positive = show less time.
#define LIVE_TIMER_OFFSET_S 0.0f

// DEMO_MODE is set in platformio.ini. When 1, lm_client fakes a brew every few seconds so
// the display can be developed/verified without any cloud credentials.
#ifndef DEMO_MODE
#define DEMO_MODE 1
#endif

// Verbose serial logging ([boot]/[live]/[ws]/[display] etc.). Set to 1 to re-enable.
#ifndef DEBUG_LOG
#define DEBUG_LOG 1
#endif

// Big, easy-tap buttons kept inside the round panel. The two BTN_*_Y slots are used by the
// confirm modals (reset, backflush) and the backflush page; the dev "actions" page stacks
// three buttons (DEMO / BOOTLOG / RESET) in its own DEV_BTN_*_Y slots.
#define BTN_X 96
#define BTN_W 274
#define BTN_H 66
#define BTN_A_Y 156  // top button (modals / backflush)
#define BTN_B_Y 258  // bottom button (modals / backflush)
#define DEV_BTN_A_Y 120
#define DEV_BTN_B_Y 213
#define DEV_BTN_C_Y 306

// CANCEL button on the setup screen (shown when the portal was opened from the dev page).
// Shorter than BTN_H so it fits under the instruction text inside the round panel.
#define WIFI_CANCEL_Y 350
#define WIFI_CANCEL_H 56

// Theme settings page: 6 scheme swatches in a 2x3 grid (kept inside the round panel).
#define THEME_SW_W 150
#define THEME_SW_H 64
#define THEME_SW_X0 70   // left column
#define THEME_SW_X1 246  // right column
#define THEME_SW_Y0 130  // rows top->bottom
#define THEME_SW_Y1 206
#define THEME_SW_Y2 282

// Boot-log console (dev toggle): if nobody taps it away, leave it this long after power-on.
#define BOOTLOG_HOLD_MS 120000

// STARTUP_TEST: on boot, cycle RED/GREEN/BLUE/WHITE + ramp brightness to prove the panel
// works independent of the UI. Flip to 1 if you ever need to debug the display again.
#ifndef STARTUP_TEST
#define STARTUP_TEST 0
#endif
