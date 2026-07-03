#include "bootlog.h"

#include <Arduino.h>
#include <Preferences.h>

#include "display.h"

// NOTE: no LOG macros in here — log.h feeds bootlogAdd(), which would recurse.

static char g_buf[BOOTLOG_LINES][BOOTLOG_COLS + 1];
static int g_head = 0;  // next slot to write
static int g_used = 0;
static bool g_enabled = false;
static bool g_active = false;
static bool g_inline = false;
static volatile bool g_dirty = false;
static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;

void bootlogInit() {
    Preferences p;
    p.begin("bootlog", true);
    g_enabled = p.getBool("on", false);
    p.end();
    g_active = g_enabled;  // capture from the very first LOG line
}

bool bootlogEnabled() { return g_enabled; }

void bootlogSetEnabled(bool on) {
    g_enabled = on;
    Preferences p;
    p.begin("bootlog", false);
    p.putBool("on", on);
    p.end();
}

bool bootlogActive() { return g_active; }
void bootlogDismiss() { g_active = false; }

bool bootlogTakeDirty() {
    bool d = g_dirty;
    g_dirty = false;
    return d;
}

void bootlogSetInline(bool on) { g_inline = on; }

void bootlogAdd(const char *line) {
    if (!g_active || !line) return;
    while (*line == '\n' || *line == '\r') line++;  // strip the leading blank of banners
    if (!*line) return;

    char tmp[BOOTLOG_COLS + 1];
    int n = 0;
    for (; n < BOOTLOG_COLS && line[n] && line[n] != '\n' && line[n] != '\r'; n++) tmp[n] = line[n];
    tmp[n] = 0;

    portENTER_CRITICAL(&g_mux);
    memcpy(g_buf[g_head], tmp, n + 1);
    g_head = (g_head + 1) % BOOTLOG_LINES;
    if (g_used < BOOTLOG_LINES) g_used++;
    portEXIT_CRITICAL(&g_mux);
    g_dirty = true;

    // Immediate redraw during the single-threaded boot phase. liveTask (core 0) must never
    // touch the canvas — its lines land via the dirty flag and the UI loop instead.
    if (g_inline && xPortGetCoreID() == 1) {
        displayBootlog();
        g_dirty = false;
    }
}

int bootlogGetLines(char out[][BOOTLOG_COLS + 1], int max) {
    portENTER_CRITICAL(&g_mux);
    int n = g_used < max ? g_used : max;
    int start = (g_head - g_used + BOOTLOG_LINES) % BOOTLOG_LINES;
    for (int i = 0; i < n; i++) memcpy(out[i], g_buf[(start + i) % BOOTLOG_LINES], BOOTLOG_COLS + 1);
    portEXIT_CRITICAL(&g_mux);
    return n;
}
