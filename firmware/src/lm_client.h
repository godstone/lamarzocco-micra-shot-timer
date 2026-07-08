// La Marzocco data source.
//   DEMO_MODE=1: simulates a brew cycle so the display can be built without credentials.
//   DEMO_MODE=0: polls the LM cloud dashboard via REST (coexists with the official app).
#pragma once

#include "brew_state.h"

// One-time setup. In real mode: sign in to the cloud (assumes WiFi+NTP already up).
void lmBegin();

// Advance the demo simulation (LIVE polling runs on its own background task).
// Safe to call every loop iteration.
void lmPoll();

const BrewState &lmState();

// Live elapsed seconds for the current (or just-finished) shot.
float lmElapsedSeconds();

// Runtime demo toggle (exposed on the dev screen). In a LIVE build these are no-ops/false.
void lmSetDemo(bool enabled);
bool lmDemo();

// Wipe WiFi + LM cloud/key settings so the device onboards fresh. Caller should restart after.
void lmFactoryReset();

// Queue a backflush cleaning command (sent by the background task once signed in). No-op in demo.
void lmRequestBackflush();

// Drop a queued-but-unsent backflush command (call when the UI stops waiting for it).
void lmCancelBackflush();

// Toggle the setup portal (WiFi + LM account) from the dev screen, e.g. to fix a typo'd
// password without a factory reset. Opens if closed, closes if it was opened this way.
void lmOpenSetupPortal();
