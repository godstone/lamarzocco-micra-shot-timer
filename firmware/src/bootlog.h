// On-screen boot log. When the persisted toggle is ON, startup log lines (LOGF/LOGLN) are
// mirrored to the display so WiFi/cloud issues can be debugged without a computer attached.
// The console shows until tapped (or BOOTLOG_HOLD_MS); the toggle itself stays on across
// boots until it's switched off on the dev actions page.
#pragma once

#include <cstddef>

#define BOOTLOG_LINES 14  // console keeps the most recent lines
#define BOOTLOG_COLS 27   // longer lines are truncated to fit the round panel

void bootlogInit();               // load the persisted toggle; call FIRST in setup()
bool bootlogEnabled();            // persisted toggle (shown on the dev actions page)
void bootlogSetEnabled(bool on);  // persist; takes effect on the next boot

bool bootlogActive();    // console is capturing/showing this boot
void bootlogDismiss();   // leave the console for this boot (toggle stays as-is)
bool bootlogTakeDirty(); // true once after new lines arrived (clears the flag)

// Append one line (no-op unless active). Thread-safe; called from the LOG macros.
void bootlogAdd(const char *line);

// While true (single-threaded boot phase, UI core only) bootlogAdd() redraws immediately.
void bootlogSetInline(bool on);

// Copy the current lines (oldest first) into out; returns the count. For the renderer.
int bootlogGetLines(char out[][BOOTLOG_COLS + 1], int max);
