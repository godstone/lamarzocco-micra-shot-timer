// Rendering for the round 466x466 AMOLED.
#pragma once

#include "brew_state.h"

void displayInit();
void displaySplash(const char *line1, const char *line2);

// Set the page-indicator dots (gallery style) drawn on the idle screens. count<=1 hides them.
void displaySetPageIndicator(int active, int count);

// Set the connection mode for the center-bottom icon: 0=none, 1=websocket, 2=cloud/REST.
void displaySetConn(int mode);

// Startup brand screen: the La Marzocco logo in brand red on black.
void displayLogo();

// Develop mode page 1: diagnostics overlay (mode, wifi/ip/signin/cloud/status/heap/touch/err).
void displayDevInfo(const BrewState &s, int touchPoints, bool demoEnabled);

// Develop mode page 2: big DEMO / BOOTLOG toggles + RESET button.
void displayDevActions(bool demoEnabled, bool bootlogOn);

// Develop mode page 3: screen orientation (ROTATE button cycles 0/90/180/270).
void displayDevDisplay(int rotation);

// Screen rotation, runtime + persisted (default DISPLAY_ROTATION). Applies immediately;
// callers must repaint. touchPoint() reads displayRotation() so touch stays aligned.
void displaySetRotation(int rotation);
int displayRotation();

// Color schemes, one per Linea Micra machine color (the metallic finishes share GRAY).
// Each scheme has a dark and a light palette; status colors (ready-green, warming-amber,
// error-red) keep fixed semantics across all schemes. Persisted; apply immediately but
// callers must repaint.
#define THEME_SCHEMES 6  // 0=RED 1=YELLOW 2=BLUE 3=WHITE 4=GRAY 5=BLACK
void displaySetScheme(int scheme);
int displayScheme();
const char *displaySchemeName(int scheme);
void displaySetDarkMode(bool dark);
bool displayDarkMode();

// Settings page: the 6 scheme swatches (tap targets, see THEME_SW_* in config.h).
void displaySettingsTheme();

// Settings page: DARK / LIGHT mode toggle (buttons at BTN_A_Y / BTN_B_Y).
void displaySettingsMode();

// Boot-log console: recent LOG lines, live while starting up (see bootlog.h).
void displayBootlog();

// Reset confirmation modal: CONFIRM / CANCEL.
void displayResetConfirm();

// Backflush / cleaning screen. ui: 0=idle (START), 1=confirm, 2=running, 3=done.
void displayBackflush(const BrewState &s, int ui);

// Idle / on-hold: screen fully dark.
void displayDark();

// Standby/idle status: machine + steamer readiness, with a heat-up countdown when not ready.
void displayStatus(const BrewState &s);

// Machine powered off / unreachable: machine illustration + "MACHINE OFF".
void displayMachineOff();

// WiFi captive-portal setup prompt (shown while the portal is open), with a live status
// line so the user sees progress. phase: 0 = waiting for a phone to join the portal AP,
// 1 = phone joined (configuring), 2 = WiFi connected (portal about to close).
void displayWifiSetup(int phase);

// Fancy idle stats: shots today (hero) + lifetime total.
void displayStats(int shotsToday, int shotsTotal);

// Brew screen: coffee fills bottom->top over COFFEE_FILL_SECONDS with the timer centered.
// Past full it washes out + the timer reddens (over-extraction). `frozen` = shot ended,
// hold the final frame (no wave animation). `steamFrac` >= 0 overlays the steam heat-up ring
// (so you can see steam readiness while pulling a shot); < 0 hides it. `preInfusionSec` > 0
// shows a realtime pre-infusion phase for the first that-many seconds of the shot.
void displayTimer(float seconds, bool frozen, float steamFrac, float preInfusionSec);
