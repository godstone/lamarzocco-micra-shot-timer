// Minimal FT3168 capacitive touch reader (I2C). We only need the finger count to detect a
// two-finger gesture that toggles develop mode.
#pragma once

void touchBegin();

// Number of fingers currently on the panel (0, 1, 2, ...). 0 if touch is unavailable.
int touchCount();

// First touch point. Returns true and fills x/y (panel coords) if a finger is down.
bool touchPoint(int *x, int *y);
