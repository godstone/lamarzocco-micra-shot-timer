// Shared brew-state type, kept dependency-free so both display and client can use it.
#pragma once

#include <cstdint>

struct BrewState {
    bool networkReady = false;  // WiFi + (real mode) signed-in
    bool connected = false;     // machine reachable via cloud (dashboard "connected")
    bool brewing = false;       // a shot is currently pulling
    uint64_t brewStartMs = 0;   // epoch ms when the current/last shot started (0 = never)
    char status[16] = "";       // raw status string, e.g. "Brewing" / "StandBy"
    char lastError[48] = "";    // last network/cloud error, empty if none (shown in dev mode)
    char ip[16] = "";           // local IP when connected (dev screen)
    bool signedIn = false;      // cloud token valid (dev screen)
    bool wifiPortal = false;    // WiFiManager captive portal currently open

    // Boiler readiness (shown on the idle/standby screen).
    bool coffeeReady = false;      // coffee boiler at temperature
    bool steamReady = false;       // steam boiler at temperature
    int coffeeReadyInSec = 0;      // seconds until coffee boiler ready (0 = ready)
    int steamReadyInSec = 0;       // seconds until steam boiler ready (0 = ready)
    int coffeeReadyTotalSec = 0;   // full coffee heat-up duration (for the progress ring)
    int steamReadyTotalSec = 0;    // full steam heat-up duration (for the progress ring)
    float coffeeHeatFrac = 0;      // 0..1 continuous heat progress (smooth ring; 1 = ready)
    float steamHeatFrac = 0;       // 0..1 continuous heat progress (smooth ring; 1 = ready)
    uint64_t coffeeReadyAtMs = 0;  // cloud ETA (epoch ms) while heating; 0 otherwise
    uint64_t steamReadyAtMs = 0;   // countdown/frac are derived live from this each frame

    // Shot stats (shown on the fancy idle screen). From the cloud counter/trend endpoints.
    int shotsToday = 0;
    int shotsTotal = 0;
    long shotsTodayDay = 0;  // epoch-day the "today" count is valid for (0 = unknown)

    // Pre-infusion / pre-brewing the user configured in the app (read from CMPreBrewing).
    bool preInfusionOn = false;
    float preInfusionSec = 0;

    // Current live data path (for the connection icon): 0=none, 1=websocket, 2=cloud/REST.
    int connMode = 0;
};
