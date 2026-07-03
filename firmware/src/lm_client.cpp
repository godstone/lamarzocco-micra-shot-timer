#include "lm_client.h"

#include <Arduino.h>
#include <sys/time.h>

#include "config.h"
#include "log.h"

// secrets.h is optional: without it, LIVE mode is unavailable and the device runs the demo.
#if __has_include("secrets.h")
#include "secrets.h"
#define HAVE_SECRETS 1
#endif

// Timezone for the "shots today" trend query — set via .env -> secrets.h; default if unset.
#ifndef LM_TIMEZONE
#define LM_TIMEZONE "Europe/Zurich"
#endif

static BrewState g_state;
static BrewState g_snapshot;
static SemaphoreHandle_t g_mutex = nullptr;
static bool g_demoEnabled = false;  // off by default -> try LIVE; toggle on the dev screen

static uint64_t epochMs() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);  // millisecond precision (NTP-synced)
    return (uint64_t)tv.tv_sec * 1000ULL + tv.tv_usec / 1000ULL;
}

static void lockState() {
    if (g_mutex) xSemaphoreTake(g_mutex, portMAX_DELAY);
}
static void unlockState() {
    if (g_mutex) xSemaphoreGive(g_mutex);
}

const BrewState &lmState() {
    lockState();
    g_snapshot = g_state;
    unlockState();
    // LIVE: derive the heat-up countdown + ring fraction from the stored ETA each call, so they
    // tick smoothly between the cloud's infrequent updates. (Demo sets these directly.)
    if (!g_demoEnabled) {
        time_t now = time(nullptr);
        auto derive = [&](bool ready, uint64_t etaMs, int total, int &sec, float &frac) {
            if (ready) {
                sec = 0;
                frac = 1.0f;
            } else if (etaMs > 0) {
                int remain = (int)((int64_t)(etaMs / 1000) - now);
                if (remain < 0) remain = 0;
                sec = remain;
                frac = (total > 0) ? 1.0f - (float)remain / total : 0.0f;
            } else {
                sec = 0;
                frac = 0.0f;
            }
        };
        derive(g_snapshot.coffeeReady, g_snapshot.coffeeReadyAtMs, g_snapshot.coffeeReadyTotalSec,
               g_snapshot.coffeeReadyInSec, g_snapshot.coffeeHeatFrac);
        derive(g_snapshot.steamReady, g_snapshot.steamReadyAtMs, g_snapshot.steamReadyTotalSec,
               g_snapshot.steamReadyInSec, g_snapshot.steamHeatFrac);

        // "Shots today" is only valid if the stored count is from the current day and the clock
        // is synced to confirm it — otherwise show 0 ("no shots today"). Lifetime total is always
        // valid. This keeps a stale count from a previous day (e.g. offline) from misleading.
        bool timeValid = now > 1700000000;
        long curDay = timeValid ? (long)(now / 86400) : -1;
        if (!timeValid || g_snapshot.shotsTodayDay != curDay) g_snapshot.shotsToday = 0;
    }
    return g_snapshot;
}

bool lmDemo() { return g_demoEnabled; }

// ============================ DEMO SIMULATION ================================
// Full lifecycle so every screen is exercised: OFF -> heating -> ready -> brewing -> off.
enum DemoPhase { D_OFF, D_HEAT, D_BREW, D_POST };
static DemoPhase g_phase = D_OFF;
static uint32_t g_phaseStart = 0;
static uint32_t g_heatStartMs = 0;
static uint32_t g_brewLenMs = 25000;
static const int DEMO_COFFEE_HEAT_S = 25;
static const int DEMO_STEAM_HEAT_S = 45;

static void setStatus(const char *s) { strncpy(g_state.status, s, sizeof(g_state.status)); }

static void demoReset() {
    g_phase = D_OFF;
    g_phaseStart = millis();
    g_state.connected = false;
    g_state.brewing = false;
    g_state.coffeeReady = g_state.steamReady = false;
    g_state.shotsToday = 3;
    g_state.shotsTotal = 43;
    g_state.preInfusionOn = true;
    g_state.preInfusionSec = 4.0f;
    setStatus("Off");
}

static void demoPoll() {
    uint32_t now = millis();
    uint32_t el = now - g_phaseStart;

    if (g_phase != D_OFF) {
        float hs = (now - g_heatStartMs) / 1000.0f;
        g_state.coffeeReadyTotalSec = DEMO_COFFEE_HEAT_S;
        g_state.steamReadyTotalSec = DEMO_STEAM_HEAT_S;
        g_state.coffeeHeatFrac = min(1.0f, hs / DEMO_COFFEE_HEAT_S);
        g_state.steamHeatFrac = min(1.0f, hs / DEMO_STEAM_HEAT_S);
        g_state.coffeeReadyInSec = max(0, (int)(DEMO_COFFEE_HEAT_S - hs + 0.999f));
        g_state.steamReadyInSec = max(0, (int)(DEMO_STEAM_HEAT_S - hs + 0.999f));
        g_state.coffeeReady = (g_state.coffeeHeatFrac >= 1.0f);
        g_state.steamReady = (g_state.steamHeatFrac >= 1.0f);
    }

    switch (g_phase) {
        case D_OFF:
            g_state.connected = false;
            g_state.brewing = false;
            g_state.coffeeReady = g_state.steamReady = false;
            setStatus("Off");
            if (el > 9000) {
                g_phase = D_HEAT;
                g_phaseStart = now;
                g_heatStartMs = now;
                g_state.connected = true;
                setStatus("StandBy");
            }
            break;
        case D_HEAT:
            g_state.connected = true;
            g_state.brewing = false;
            setStatus("StandBy");
            if (g_state.coffeeReady &&
                (int)((now - g_heatStartMs) / 1000) > DEMO_COFFEE_HEAT_S + 2) {
                g_phase = D_BREW;
                g_phaseStart = now;
                g_brewLenMs = 20000 + (esp_random() % 22000);
                g_state.brewing = true;
                setStatus("Brewing");
            }
            break;
        case D_BREW:
            g_state.brewing = true;
            setStatus("Brewing");
            if (el > g_brewLenMs) {
                g_phase = D_POST;
                g_phaseStart = now;
                g_state.brewing = false;
                g_state.shotsToday++;
                g_state.shotsTotal++;
                setStatus("StandBy");
            }
            break;
        case D_POST:
            g_state.brewing = false;
            setStatus("StandBy");
            if (el > 20000) {
                g_phase = D_OFF;
                g_phaseStart = now;
            }
            break;
    }
}

static float demoElapsed() {
    return (g_phase == D_BREW) ? (millis() - g_phaseStart) / 1000.0f : 0;
}

// ============================ LIVE CLOUD CLIENT ==============================
#ifdef HAVE_SECRETS
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <WiFiManager.h>
#include <esp_wifi.h>

#include "lm_auth.h"
#include "mbedtls/base64.h"

#define LM_BASE "https://lion.lamarzocco.io/api/customer-app"
#define LM_HOST "lion.lamarzocco.io"
#define WIFI_PORTAL_AP "LaMarzocco-Display"
#define WS_RETRY_MS 25000  // after a websocket drop, run on REST this long before retrying
                           // (avoids fighting the official app for the single connection)

static String g_access, g_refresh;
static Preferences g_prefs;  // persists shot counts + the on-device installation key
static bool g_haveKey = false;    // an installation key is loaded/generated
static bool g_registered = false; // the key is registered with the cloud (/auth/init)
static time_t g_tokenExp = 0;
static bool g_liveStarted = false;
static bool g_backflushRequest = false;  // set by lmRequestBackflush(); dispatched in liveTask
static WiFiManager g_wm;

static void setError(const char *e) {
    lockState();
    strncpy(g_state.lastError, e, sizeof(g_state.lastError) - 1);
    g_state.lastError[sizeof(g_state.lastError) - 1] = 0;
    unlockState();
}

static void addSignedHeaders(HTTPClient &http) {
    LmSignedHeaders h;
    if (!lmAuthHeaders(h)) return;
    http.addHeader("X-App-Installation-Id", h.installId);
    http.addHeader("X-Timestamp", h.timestamp);
    http.addHeader("X-Nonce", h.nonce);
    http.addHeader("X-Request-Signature", h.signature);
}

// POST a token request (signin/refresh); fills g_access/g_refresh on success.
static bool postToken(const char *path, const String &body) {
    WiFiClientSecure client;
    client.setInsecure();  // TODO: pin the lamarzocco.io CA for full verification
    HTTPClient http;
    if (!http.begin(client, String(LM_BASE) + path)) return false;
    http.addHeader("Content-Type", "application/json");
    addSignedHeaders(http);
    int code = http.POST((uint8_t *)body.c_str(), body.length());
    bool ok = false;
    if (code == 200) {
        String resp = http.getString();  // read full body (more reliable than the stream)
        JsonDocument doc;
        DeserializationError jerr = deserializeJson(doc, resp);
        if (!jerr) {
            g_access = doc["accessToken"].as<String>();
            g_refresh = doc["refreshToken"].as<String>();
            g_tokenExp = time(nullptr) + 3600;
            ok = !g_access.isEmpty();
        }
        if (!ok) {
            char e[48];
            snprintf(e, sizeof(e), "auth %s: parse %s", path + 6, jerr ? jerr.c_str() : "no token");
            setError(e);
        }
    } else {
        char e[48];
        snprintf(e, sizeof(e), "auth %s: HTTP %d", path + 6, code);
        setError(e);
    }
    http.end();
    return ok;
}

static bool signIn() {
    JsonDocument d;
    d["username"] = LM_USERNAME;
    d["password"] = LM_PASSWORD;
    String body;
    serializeJson(d, body);
    bool ok = postToken("/auth/signin", body);
    LOGF("[live] sign-in %s\n", ok ? "OK" : "FAILED");
    return ok;
}

static bool refreshToken() {
    JsonDocument d;
    d["username"] = LM_USERNAME;
    d["refreshToken"] = g_refresh;
    String body;
    serializeJson(d, body);
    if (postToken("/auth/refreshtoken", body)) return true;
    return signIn();  // refresh failed -> full sign-in
}

static bool ensureToken() {
    time_t now = time(nullptr);
    if (g_access.isEmpty() || now >= g_tokenExp) return signIn();
    if (now >= g_tokenExp - 600) return refreshToken();  // refresh 10 min before expiry
    return true;
}

// Register this device's generated installation key with the cloud (one-time, POST /auth/init).
static bool registerClient() {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    if (!http.begin(client, String(LM_BASE) + "/auth/init")) return false;
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-App-Installation-Id", lmAuthInstallId());
    http.addHeader("X-Request-Proof", lmAuthProof(lmAuthBaseString()));
    JsonDocument d;
    d["pk"] = lmAuthPubKeyB64();
    String body;
    serializeJson(d, body);
    int code = http.POST((uint8_t *)body.c_str(), body.length());
    http.end();
    LOGF("[live] register /auth/init -> HTTP %d\n", code);
    if (code < 200 || code >= 300) {
        char e[48];
        snprintf(e, sizeof(e), "register HTTP %d", code);
        setError(e);
    }
    return code >= 200 && code < 300;
}

// Authenticated GET; returns the body, or empty on failure (clears token on 401).
static WiFiClientSecure g_tls;  // persistent: keep-alive avoids a TLS handshake every poll
static HTTPClient g_http;
static bool g_httpInit = false;

static String authedGet(const String &url) {
    if (!g_httpInit) {
        g_tls.setInsecure();
        g_http.setReuse(true);  // reuse the connection across polls
        g_httpInit = true;
    }
    if (!g_http.begin(g_tls, url)) return String();
    addSignedHeaders(g_http);
    g_http.addHeader("Authorization", "Bearer " + g_access);
    int code = g_http.GET();
    String body;
    if (code == 200)
        body = g_http.getString();
    else if (code == 401)
        g_access = "";  // force re-sign-in next time
    else {
        char e[48];
        snprintf(e, sizeof(e), "GET HTTP %d", code);
        setError(e);
    }
    g_http.end();  // with setReuse(true) this keeps the socket open
    return body;
}

// Authenticated POST of a JSON body (machine commands, e.g. backflush). Uses its own short-lived
// TLS client so it never interferes with the reused polling connection. Returns true on 2xx.
static bool authedPost(const String &url, const String &body) {
    WiFiClientSecure client;
    client.setInsecure();  // TODO: pin the lamarzocco.io CA
    HTTPClient http;
    if (!http.begin(client, url)) return false;
    http.addHeader("Content-Type", "application/json");
    addSignedHeaders(http);
    http.addHeader("Authorization", "Bearer " + g_access);
    int code = http.POST((uint8_t *)body.c_str(), body.length());
    http.end();
    if (code < 200 || code >= 300) {
        char e[48];
        snprintf(e, sizeof(e), "cmd HTTP %d", code);
        setError(e);
        return false;
    }
    return true;
}

// Fire the backflush cleaning command (POST .../command/CoffeeMachineBackFlushStartCleaning).
static void sendBackflush() {
    bool ok = authedPost(
        String(LM_BASE) + "/things/" + LM_SERIAL + "/command/CoffeeMachineBackFlushStartCleaning",
        "{\"enabled\":true}");
    LOGF("[live] backflush start -> %s\n", ok ? "OK" : "FAILED");
}

// Parse a dashboard payload (full REST response OR a websocket delta). Merge-style: only the
// fields actually present are updated, so partial websocket messages don't clobber state.
static void parseDashboard(const String &payload) {
    JsonDocument doc;
    if (deserializeJson(doc, payload) != DeserializationError::Ok) {
        setError("dashboard parse");
        return;
    }

    bool haveStatus = false, havePre = false, haveCoffee = false, haveSteam = false;
    bool haveBackflush = false;
    const char *status = "";
    uint64_t brewStartMs = 0;
    bool preOn = false;
    float preSec = 0;
    const char *coffStatus = "", *steamStatus = "";
    uint64_t coffReadyMs = 0, steamReadyMs = 0;
    const char *bfStatus = "";
    uint64_t bfLastCleanMs = 0;

    JsonArray widgets = doc["widgets"].as<JsonArray>();
    if (!widgets.isNull())
        for (JsonObject w : widgets) {
            const char *code = w["code"] | "";
            if (strcmp(code, "CMMachineStatus") == 0) {
                JsonObject o = w["output"];
                status = o["status"] | "";
                brewStartMs = o["brewingStartTime"] | (uint64_t)0;  // ms; null -> 0
                haveStatus = true;
            } else if (strcmp(code, "CMPreBrewing") == 0) {
                JsonObject o = w["output"];
                const char *mode = o["mode"] | "";
                preOn = (strcmp(mode, "Disabled") != 0);
                JsonObject times = o["times"];
                JsonArray list = (strcmp(mode, "PreInfusion") == 0)
                                     ? times["PreInfusion"].as<JsonArray>()
                                     : times["PreBrewing"].as<JsonArray>();
                if (!list.isNull() && list.size() > 0) preSec = list[0]["seconds"]["In"] | 0.0f;
                if (!preOn) preSec = 0;
                havePre = true;
            } else if (strcmp(code, "CMCoffeeBoiler") == 0) {
                coffStatus = w["output"]["status"] | "";
                coffReadyMs = w["output"]["readyStartTime"] | (uint64_t)0;
                haveCoffee = true;
            } else if (strcmp(code, "CMSteamBoilerLevel") == 0) {
                steamStatus = w["output"]["status"] | "";
                steamReadyMs = w["output"]["readyStartTime"] | (uint64_t)0;
                haveSteam = true;
            } else if (strcmp(code, "CMBackFlush") == 0) {
                bfStatus = w["output"]["status"] | "";
                bfLastCleanMs = w["output"]["lastCleaningStartTime"] | (uint64_t)0;
                haveBackflush = true;
            }
        }

    // Boiler status "Ready" / "HeatingUp" + readyStartTime (ETA) -> ready flag, countdown, and a
    // 0..1 heat progress. `total` is captured per heat cycle so the ring depletes from full.
    static int coffTotal = 0, steamTotal = 0;
    // Store the boiler's ready flag + ETA + captured heat-cycle total. The live countdown and
    // ring fraction are derived from the ETA each frame in lmState() (the cloud only pushes
    // updates every 5-30s, so we can't store a snapshot count).
    auto boiler = [](const char *st, uint64_t readyMs, int &total, bool &ready, uint64_t &etaMs,
                     int &totalOut) {
        time_t now = time(nullptr);
        ready = (strcmp(st, "Ready") == 0);
        bool heating = (strcmp(st, "HeatingUp") == 0);
        int remain = readyMs ? (int)((int64_t)(readyMs / 1000) - now) : 0;
        if (remain < 0) remain = 0;
        if (heating) {
            if (total <= 0 || remain > total) total = remain;  // full at start of the cycle
        } else {
            total = 0;
        }
        etaMs = heating ? readyMs : 0;
        totalOut = total;
    };

    lockState();
    g_state.networkReady = true;
    if (doc["connected"].is<bool>()) g_state.connected = doc["connected"];
    if (haveStatus) {
        strncpy(g_state.status, status, sizeof(g_state.status) - 1);
        g_state.status[sizeof(g_state.status) - 1] = 0;
        g_state.brewing = (strcmp(status, "Brewing") == 0);
        g_state.brewStartMs = brewStartMs;
        if (strcmp(status, "Off") == 0) {  // machine off -> boilers gone; reset readiness
            g_state.coffeeReady = g_state.steamReady = false;
            g_state.coffeeHeatFrac = g_state.steamHeatFrac = 0;
            coffTotal = steamTotal = 0;
        }
    }
    if (haveCoffee)
        boiler(coffStatus, coffReadyMs, coffTotal, g_state.coffeeReady, g_state.coffeeReadyAtMs,
               g_state.coffeeReadyTotalSec);
    if (haveSteam)
        boiler(steamStatus, steamReadyMs, steamTotal, g_state.steamReady, g_state.steamReadyAtMs,
               g_state.steamReadyTotalSec);
    if (havePre) {
        g_state.preInfusionOn = preOn;
        g_state.preInfusionSec = preSec;
    }
    if (haveBackflush) {
        g_state.backflushStatus = (strcmp(bfStatus, "Cleaning") == 0)    ? 2
                                  : (strcmp(bfStatus, "Requested") == 0) ? 1
                                                                         : 0;
        if (bfLastCleanMs) g_state.lastCleaningStartMs = bfLastCleanMs;
    }
    g_state.lastError[0] = 0;
    unlockState();

    if (haveStatus) {
        static char last[16] = "";
        if (strcmp(status, last) != 0) {
            LOGF("[live] status=%s connected=%d\n", status, (int)g_state.connected);
            strncpy(last, status, sizeof(last) - 1);
        }
    }
}

// Pull "shots today" (trend, days=1) and lifetime total (counter).
static void fetchStats() {
    String c = authedGet(String(LM_BASE) + "/things/" + LM_SERIAL +
                          "/stats/COFFEE_AND_FLUSH_COUNTER/1");
    if (c.length()) {
        JsonDocument doc;
        if (deserializeJson(doc, c) == DeserializationError::Ok) {
            int total = doc["output"]["totalCoffee"] | -1;
            if (total >= 0) {
                lockState();
                g_state.shotsTotal = total;
                unlockState();
                if (g_prefs.getInt("total", -1) != total) g_prefs.putInt("total", total);
            }
        }
    }
    String t = authedGet(String(LM_BASE) + "/things/" + LM_SERIAL +
                         "/stats/COFFEE_AND_FLUSH_TREND/1?days=1&timezone=" + LM_TIMEZONE);
    if (t.length()) {
        JsonDocument doc;
        if (deserializeJson(doc, t) == DeserializationError::Ok) {
            JsonArray arr = doc["output"]["coffees"].as<JsonArray>();
            int today = -1;
            for (JsonObject e : arr) today = e["value"] | today;  // last day = today
            if (today >= 0) {
                long curDay = (long)(time(nullptr) / 86400);  // epoch day
                lockState();
                g_state.shotsToday = today;
                g_state.shotsTodayDay = curDay;
                unlockState();
                if (g_prefs.getInt("today", -1) != today) g_prefs.putInt("today", today);
                if (g_prefs.getInt("tday", -1) != (int)curDay) g_prefs.putInt("tday", (int)curDay);
            }
        }
    }
}

// ---- Minimal websocket (STOMP over wss) over our proven WiFiClientSecure ----
static WiFiClientSecure g_wsTls;
static bool g_wsUp = false;
static uint32_t g_wsBackoffUntil = 0;

static String b64bytes(const uint8_t *d, size_t n) {
    size_t need = 0;
    mbedtls_base64_encode(nullptr, 0, &need, d, n);
    uint8_t buf[32];
    size_t olen = 0;
    if (need > sizeof(buf)) return String();
    mbedtls_base64_encode(buf, sizeof(buf), &olen, d, n);
    buf[olen] = 0;
    return String((char *)buf);
}

// Send one websocket frame (client->server frames must be masked).
static bool wsSendFrame(const String &payload, uint8_t finOpcode) {
    size_t n = payload.length();
    uint8_t hdr[8];
    int hi = 0;
    hdr[hi++] = finOpcode;
    if (n < 126) {
        hdr[hi++] = 0x80 | (uint8_t)n;
    } else if (n < 65536) {
        hdr[hi++] = 0x80 | 126;
        hdr[hi++] = (n >> 8) & 0xFF;
        hdr[hi++] = n & 0xFF;
    } else {
        return false;
    }
    uint8_t mask[4];
    for (int i = 0; i < 4; i++) mask[i] = esp_random() & 0xFF;
    if (g_wsTls.write(hdr, hi) != (int)hi) return false;
    if (g_wsTls.write(mask, 4) != 4) return false;
    const uint8_t *p = (const uint8_t *)payload.c_str();
    uint8_t buf[256];
    size_t off = 0;
    for (size_t i = 0; i < n; i++) {
        buf[off++] = p[i] ^ mask[i & 3];
        if (off == sizeof(buf)) {
            g_wsTls.write(buf, off);
            off = 0;
        }
    }
    if (off) g_wsTls.write(buf, off);
    return true;
}

static int wsReadByte(uint32_t toMs) {
    uint32_t t = millis();
    while (!g_wsTls.available()) {
        if (!g_wsTls.connected()) return -1;
        if (millis() - t > toMs) return -2;
        vTaskDelay(1);
    }
    return g_wsTls.read();
}

// Read one full websocket frame into `out`; returns false on disconnect/timeout.
static bool wsReadFrame(String &out, uint8_t &opcode, uint32_t toMs) {
    int b0 = wsReadByte(toMs);
    if (b0 < 0) return false;
    opcode = b0 & 0x0F;
    int b1 = wsReadByte(toMs);
    if (b1 < 0) return false;
    bool masked = b1 & 0x80;
    uint64_t len = b1 & 0x7F;
    if (len == 126) {
        int h = wsReadByte(toMs), l = wsReadByte(toMs);
        if (h < 0 || l < 0) return false;
        len = ((uint64_t)h << 8) | l;
    } else if (len == 127) {
        len = 0;
        for (int i = 0; i < 8; i++) {
            int x = wsReadByte(toMs);
            if (x < 0) return false;
            len = (len << 8) | x;
        }
    }
    uint8_t mask[4] = {0, 0, 0, 0};
    if (masked)
        for (int i = 0; i < 4; i++) mask[i] = wsReadByte(toMs);
    out = "";
    out.reserve(len + 1);
    for (uint64_t i = 0; i < len; i++) {
        int c = wsReadByte(toMs);
        if (c < 0) return false;
        if (masked) c ^= mask[i & 3];
        out += (char)c;
    }
    return true;
}

// HTTP upgrade + STOMP CONNECT + SUBSCRIBE. Returns true once subscribed.
static bool wsConnect() {
    if (!ensureToken()) return false;
    g_wsTls.setInsecure();
    g_wsTls.setTimeout(6000);  // ms — read timeout for the handshake response
    if (!g_wsTls.connect(LM_HOST, 443)) {
        setError("ws tcp");
        LOGLN("[ws] tcp connect failed");
        return false;
    }
    LmSignedHeaders h;
    if (!lmAuthHeaders(h)) {
        g_wsTls.stop();
        return false;
    }
    uint8_t keyb[16];
    for (int i = 0; i < 16; i++) keyb[i] = esp_random() & 0xFF;
    String req = String("GET /ws/connect HTTP/1.1\r\n") + "Host: " LM_HOST "\r\n" +
                 "Upgrade: websocket\r\nConnection: Upgrade\r\n" +
                 "Sec-WebSocket-Key: " + b64bytes(keyb, 16) + "\r\n" +
                 "Sec-WebSocket-Version: 13\r\n" + "X-App-Installation-Id: " + h.installId +
                 "\r\n" + "X-Timestamp: " + h.timestamp + "\r\n" + "X-Nonce: " + h.nonce + "\r\n" +
                 "X-Request-Signature: " + h.signature + "\r\n\r\n";
    g_wsTls.print(req);

    String statusLine = g_wsTls.readStringUntil('\n');
    if (statusLine.indexOf("101") < 0) {
        setError("ws handshake");
        LOGF("[ws] handshake not 101: '%s'\n", statusLine.c_str());
        g_wsTls.stop();
        return false;
    }
    while (true) {  // consume handshake headers up to the blank line
        String line = g_wsTls.readStringUntil('\n');
        if (line.length() <= 1) break;
    }

    String connectFrame = String("CONNECT\nhost:" LM_HOST
                                 "\naccept-version:1.2,1.1,1.0\nheart-beat:0,0\nAuthorization:Bearer ") +
                          g_access + "\n\n";
    connectFrame += '\0';
    wsSendFrame(connectFrame, 0x81);

    String body;
    uint8_t op;
    if (!wsReadFrame(body, op, 6000) || body.indexOf("CONNECTED") != 0) {
        setError("ws no connected");
        LOGF("[ws] no CONNECTED (op=%d): '%.40s'\n", op, body.c_str());
        g_wsTls.stop();
        return false;
    }

    char sid[40];
    snprintf(sid, sizeof(sid), "%08x%08x", (unsigned)esp_random(), (unsigned)esp_random());
    String sub = String("SUBSCRIBE\ndestination:/ws/sn/") + LM_SERIAL + "/dashboard\nack:auto\nid:" +
                 sid + "\ncontent-length:0\n\n";
    sub += '\0';
    wsSendFrame(sub, 0x81);
    LOGLN("[ws] connected + subscribed");
    return true;
}

// Process any pending websocket frames. Returns false on disconnect.
static bool wsService() {
    if (!g_wsTls.connected()) return false;
    while (g_wsTls.available()) {
        String payload;
        uint8_t op;
        if (!wsReadFrame(payload, op, 3000)) return false;
        if (op == 0x8) return false;             // close
        if (op == 0x9) {                         // ping -> pong
            wsSendFrame(payload, 0x8A);
            continue;
        }
        if (op != 0x1) continue;                 // only text (STOMP)
        if (payload.startsWith("MESSAGE")) {
            int sep = payload.indexOf("\n\n");
            if (sep < 0) continue;
            String json = payload.substring(sep + 2);
            int z = json.indexOf('\0');
            if (z >= 0) json = json.substring(0, z);
            if (json.length()) parseDashboard(json);
        }
    }
    return true;
}

static void liveTask(void *) {
    int statsCountdown = 0;
    for (;;) {
        g_wm.process();  // service the captive portal / DNS while it's open

        if (!g_demoEnabled) {
            bool wifiUp = (WiFi.status() == WL_CONNECTED);
            bool clockOk = (time(nullptr) > 1700000000);

            lockState();
            g_state.wifiPortal = g_wm.getConfigPortalActive();
            if (wifiUp)
                strncpy(g_state.ip, WiFi.localIP().toString().c_str(), sizeof(g_state.ip) - 1);
            else
                g_state.ip[0] = 0;
            unlockState();

            if (wifiUp && clockOk && g_haveKey && !g_registered) {
                // Register the on-device key with the cloud (once), before we can sign in.
                if (registerClient()) {
                    g_registered = true;
                    g_prefs.putBool("reg", true);
                }
            }
            if (wifiUp && clockOk && g_registered) {
                bool tok = ensureToken();
                lockState();
                g_state.signedIn = tok;
                unlockState();
                if (tok) {
                    // Outbound command (backflush): dispatch once when requested from the UI.
                    bool doBackflush = false;
                    lockState();
                    if (g_backflushRequest) {
                        g_backflushRequest = false;
                        doBackflush = true;
                    }
                    unlockState();
                    if (doBackflush) sendBackflush();

                    // Prefer the realtime websocket; fall back to REST polling otherwise.
                    if (!g_wsUp && millis() >= g_wsBackoffUntil) {
                        if (wsConnect())
                            g_wsUp = true;
                        else
                            g_wsBackoffUntil = millis() + 8000;  // brief retry on connect failure
                    }
                    if (g_wsUp) {
                        if (!wsService()) {  // disconnected (often the app took the connection)
                            LOGLN("[ws] disconnected -> REST fallback");
                            g_wsTls.stop();
                            g_wsUp = false;
                            g_wsBackoffUntil = millis() + WS_RETRY_MS;  // don't fight the app
                        }
                    } else {
                        String payload =
                            authedGet(String(LM_BASE) + "/things/" + LM_SERIAL + "/dashboard");
                        if (payload.length()) parseDashboard(payload);
                    }
                    // Stats are slow (extra requests) — only when NOT brewing and not over WS.
                    if (!g_wsUp && !g_state.brewing && statsCountdown-- <= 0) {
                        statsCountdown = 30;
                        fetchStats();
                    } else if (g_wsUp && statsCountdown-- <= 0) {
                        statsCountdown = 300;  // ~ every 300 loops over WS (loops are short)
                        if (!g_state.brewing) fetchStats();
                    }
                    lockState();
                    g_state.connMode = g_wsUp ? 1 : (g_state.networkReady ? 2 : 0);
                    unlockState();
                }
            } else {
                lockState();
                g_state.networkReady = false;
                g_state.connected = false;
                g_state.signedIn = false;
                g_state.connMode = 0;
                unlockState();
                setError(g_wm.getConfigPortalActive() ? "wifi setup"
                                                      : (wifiUp ? "clock..." : "wifi..."));
            }
        }
        // Websocket: service frames promptly. REST: fast while brewing, relaxed otherwise.
        uint32_t d = g_wsUp ? 30 : (g_state.brewing ? 600 : LIVE_POLL_INTERVAL_MS);
        vTaskDelay(pdMS_TO_TICKS(d));
    }
}

static void liveBegin() {
    // Load the last-synced shot counts so the stats screen shows real numbers even while
    // offline / before the first cloud sync.
    g_prefs.begin("lmstats", false);
    lockState();
    g_state.shotsTotal = g_prefs.getInt("total", 0);
    g_state.shotsToday = g_prefs.getInt("today", 0);
    g_state.shotsTodayDay = g_prefs.getInt("tday", 0);
    unlockState();

    // Installation key: load from flash if present, else generate one on-device (registered
    // with the cloud later, once online). This replaces the PC pre-flight — fully PC-free.
    String kid = g_prefs.getString("kid", "");
    if (kid.length() &&
        lmAuthBegin(kid.c_str(), g_prefs.getString("ksec", "").c_str(),
                    g_prefs.getString("kpriv", "").c_str())) {
        g_haveKey = true;
        g_registered = g_prefs.getBool("reg", true);
        LOGF("[live] loaded installation key from flash (registered=%d)\n", g_registered);
    } else if (lmAuthGenerate()) {
        g_prefs.putString("kid", lmAuthInstallId());
        g_prefs.putString("ksec", lmAuthSecretB64());
        g_prefs.putString("kpriv", lmAuthPrivKeyB64());
        g_prefs.putBool("reg", false);
        g_haveKey = true;
        g_registered = false;
        LOGF("[live] generated new installation key %s\n", lmAuthInstallId().c_str());
    } else {
        LOGLN("[live] installation key setup FAILED");
        setError("key gen failed");
    }

    WiFi.onEvent([](WiFiEvent_t e, WiFiEventInfo_t info) {
        if (e == ARDUINO_EVENT_WIFI_STA_DISCONNECTED)
            LOGF("[live] wifi disconnected, reason=%d\n",
                          info.wifi_sta_disconnected.reason);
        else if (e == ARDUINO_EVENT_WIFI_STA_GOT_IP)
            LOGF("[live] wifi got IP: %s\n", WiFi.localIP().toString().c_str());
    });
    WiFi.mode(WIFI_STA);

    // One-time seed: if no WiFi creds are stored yet, persist the ones from secrets.h so the
    // first boot at home connects automatically. After the user (re)configures via the portal,
    // those saved creds win and we never overwrite them.
    wifi_config_t cfg = {};
    esp_wifi_get_config(WIFI_IF_STA, &cfg);
    bool stored = cfg.sta.ssid[0] != 0;
    if (!stored && String(WIFI_SSID) != String("YOUR_WIFI_SSID")) {
        WiFi.persistent(true);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        LOGF("[live] seeded WiFi from secrets ('%s')\n", WIFI_SSID);
    }

    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);

    // WiFiManager: connect with stored creds, else open captive portal "LaMarzocco-Display".
    g_wm.setConfigPortalBlocking(false);  // non-blocking; serviced via g_wm.process()
    g_wm.setConfigPortalTimeout(0);       // keep the portal open until configured
    g_wm.setConnectTimeout(15);
    g_wm.setHostname("lamarzocco-display");
    bool ok = g_wm.autoConnect(WIFI_PORTAL_AP);
    LOGF("[live] wifi %s; portal %s\n", ok ? "connected" : "not yet",
                  g_wm.getConfigPortalActive() ? "OPEN (join " WIFI_PORTAL_AP ")" : "closed");

    // (installation key already loaded/generated above)

    // Poll on core 0 so TLS latency never stutters the UI rendering on core 1.
    xTaskCreatePinnedToCore(liveTask, "lm_live", 16384, nullptr, 1, nullptr, 0);
    g_liveStarted = true;
}
#endif  // HAVE_SECRETS

// ============================ PUBLIC API =====================================
void lmBegin() {
    g_mutex = xSemaphoreCreateMutex();
    demoReset();
#ifdef HAVE_SECRETS
    liveBegin();
#else
    g_state.networkReady = false;  // no creds compiled in -> LIVE unavailable
#endif
}

void lmPoll() {
    // LIVE polling runs on its own task; here we only advance the demo simulation.
    if (g_demoEnabled) {
        lockState();
        demoPoll();
        unlockState();
    }
}

float lmElapsedSeconds() {
    if (g_demoEnabled) return demoElapsed();
    // LIVE: elapsed since the machine's brew start (epoch ms), via NTP-synced clock.
    uint64_t start = g_state.brewStartMs;
    if (!g_state.brewing || start == 0) return 0;
    uint64_t now = epochMs();
    float e = (now > start) ? (now - start) / 1000.0f : 0;
    e -= LIVE_TIMER_OFFSET_S;  // calibration to match the official app
    return e < 0 ? 0 : e;
}

void lmSetDemo(bool enabled) {
    g_demoEnabled = enabled;
    lockState();
    if (enabled) {
        demoReset();
    } else {
        // Back to live: clear demo artifacts; the live task will repopulate.
        g_state.brewing = false;
        g_state.connected = false;
        strncpy(g_state.status, "Off", sizeof(g_state.status));
    }
    unlockState();
}

void lmFactoryReset() {
#ifdef HAVE_SECRETS
    g_wm.resetSettings();          // clear stored WiFi credentials
    g_prefs.clear();               // clear installation key + shot stats (+ future creds)
    WiFi.disconnect(true, true);   // erase persisted WiFi config
#endif
}

void lmRequestBackflush() {
    // Queue the command; the background task sends it once signed in. No-op in a demo-only build.
#ifdef HAVE_SECRETS
    lockState();
    g_backflushRequest = true;
    unlockState();
#endif
}
