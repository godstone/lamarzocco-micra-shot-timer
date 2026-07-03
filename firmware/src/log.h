// Verbose logging, gated by DEBUG_LOG (config.h). Flip DEBUG_LOG to 1 to re-enable.
// Lines also feed the on-screen boot-log console (bootlog.h) while it's capturing.
#pragma once

#include <Arduino.h>

#include "bootlog.h"
#include "config.h"

#if DEBUG_LOG
#define LOGF(...)                                 \
    do {                                          \
        char _lb[120];                            \
        snprintf(_lb, sizeof(_lb), __VA_ARGS__);  \
        Serial.print(_lb);                        \
        bootlogAdd(_lb);                          \
    } while (0)
#define LOGLN(x)            \
    do {                    \
        Serial.println(x);  \
        bootlogAdd(x);      \
    } while (0)
#else
#define LOGF(...) \
    do {          \
    } while (0)
#define LOGLN(x) \
    do {         \
    } while (0)
#endif
