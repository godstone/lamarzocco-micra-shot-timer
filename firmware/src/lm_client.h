// La Marzocco data source.
//   DEMO_MODE=1: simulates a brew cycle so the display can be built without credentials.
//   DEMO_MODE=0: polls the LM cloud dashboard via REST (coexists with the official app).
#pragma once

#include "brew_state.h"

// One-time setup. In real mode: sign in to the cloud (assumes WiFi+NTP already up).
void lmBegin();

// Refresh state from the cloud (real) or advance the simulation (demo).
// Safe to call frequently; it self-rate-limits to POLL_INTERVAL_MS.
void lmPoll();

const BrewState &lmState();

// Live elapsed seconds for the current (or just-finished) shot.
float lmElapsedSeconds();

// Runtime demo toggle (exposed on the dev screen). In a LIVE build these are no-ops/false.
void lmSetDemo(bool enabled);
bool lmDemo();

// Wipe WiFi + LM cloud/key settings so the device onboards fresh. Caller should restart after.
void lmFactoryReset();
