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

static void pushRow(const char *s, int n) {
    portENTER_CRITICAL(&g_mux);
    memcpy(g_buf[g_head], s, n);
    g_buf[g_head][n] = 0;
    g_head = (g_head + 1) % BOOTLOG_LINES;
    if (g_used < BOOTLOG_LINES) g_used++;
    portEXIT_CRITICAL(&g_mux);
}

void bootlogAdd(const char *line) {
    if (!g_active || !line) return;
    while (*line == '\n' || *line == '\r') line++;  // strip the leading blank of banners

    // One log line -> up to 3 console rows: wrap at the last space that fits (so an IP or
    // status word is never split mid-value); continuation rows are indented two columns.
    char row[BOOTLOG_COLS + 1];
    int rows = 0;
    const char *p = line;
    while (*p && *p != '\n' && *p != '\r' && rows < 3) {
        int indent = rows ? 2 : 0;
        int cap = BOOTLOG_COLS - indent;
        int len = 0;
        while (len < cap && p[len] && p[len] != '\n' && p[len] != '\r') len++;
        int take = len, skip = 0;
        if (len == cap && p[len] && p[len] != '\n' && p[len] != '\r') {
            for (int i = len; i > cap / 2; i--)
                if (p[i - 1] == ' ') {
                    take = i - 1;
                    skip = 1;
                    break;
                }
        }
        if (take == 0) break;
        memset(row, ' ', indent);
        memcpy(row + indent, p, take);
        pushRow(row, indent + take);
        p += take + skip;
        rows++;
    }
    if (!rows) return;
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
