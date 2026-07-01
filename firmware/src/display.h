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

// Develop mode page 2: big DEMO toggle + RESET buttons.
void displayDevActions(bool demoEnabled);

// Reset confirmation modal: CONFIRM / CANCEL.
void displayResetConfirm();

// Idle / on-hold: screen fully dark.
void displayDark();

// Standby/idle status: machine + steamer readiness, with a heat-up countdown when not ready.
void displayStatus(const BrewState &s);

// Machine powered off / unreachable: machine illustration + "MACHINE OFF".
void displayMachineOff();

// WiFi captive-portal setup prompt (shown while the portal is open).
void displayWifiSetup();

// Fancy idle stats: shots today (hero) + lifetime total.
void displayStats(int shotsToday, int shotsTotal);

// Brew screen: coffee fills bottom->top over COFFEE_FILL_SECONDS with the timer centered.
// Past full it washes out + the timer reddens (over-extraction). `frozen` = shot ended,
// hold the final frame (no wave animation). `steamFrac` >= 0 overlays the steam heat-up ring
// (so you can see steam readiness while pulling a shot); < 0 hides it. `preInfusionSec` > 0
// shows a realtime pre-infusion phase for the first that-many seconds of the shot.
void displayTimer(float seconds, bool frozen, float steamFrac, float preInfusionSec);
