// Verbose logging, gated by DEBUG_LOG (config.h). Flip DEBUG_LOG to 1 to re-enable.
#pragma once

#include <Arduino.h>

#include "config.h"

#if DEBUG_LOG
#define LOGF(...) Serial.printf(__VA_ARGS__)
#define LOGLN(x) Serial.println(x)
#else
#define LOGF(...) \
    do {          \
    } while (0)
#define LOGLN(x) \
    do {         \
    } while (0)
#endif
