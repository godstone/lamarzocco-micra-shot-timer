#include "touch.h"

#include <Arduino.h>
#include <Wire.h>

#include "config.h"
#include "display.h"  // displayRotation(): touch is remapped to match the screen
#include "pin_config.h"
#include "log.h"

#define FT3168_ADDR 0x38
#define FT_REG_FINGERNUM 0x02  // FocalTech: low nibble = number of active touch points
#define FT_REG_DEVICE_ID 0xA0  // expect 0x03 for FT3168

// Read one register byte using a STOP between write and read (FocalTech-friendly; a
// repeated-start can return stale zeros on these controllers). Returns -1 on failure.
static int readReg(uint8_t reg) {
    Wire.beginTransmission(FT3168_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(true) != 0) return -1;  // STOP
    if (Wire.requestFrom(FT3168_ADDR, 1) != 1) return -1;
    return Wire.read();
}

void touchBegin() {
    Wire.begin(IIC_SDA, IIC_SCL);
    Wire.setClock(400000);
    Wire.beginTransmission(FT3168_ADDR);
    bool present = (Wire.endTransmission() == 0);
    int id = readReg(FT_REG_DEVICE_ID);
    LOGF("[touch] FT3168 @0x38 present=%d id=0x%02X (expect 0x03)\n", present, id);
}

int touchCount() {
    int v = readReg(FT_REG_FINGERNUM);
    if (v < 0) return 0;
    int n = v & 0x0F;
    return (n > 5) ? 0 : n;  // sanity guard against bus glitches
}

bool touchPoint(int *x, int *y) {
    // This controller only supports single-register reads (block reads return nothing),
    // so read each register on its own — matching LilyGO's FT3x68 driver.
    int td = readReg(FT_REG_FINGERNUM);
    if (td < 0 || (td & 0x0F) < 1) return false;
    int xh = readReg(0x03), xl = readReg(0x04);
    int yh = readReg(0x05), yl = readReg(0x06);
    if (xh < 0 || xl < 0 || yh < 0 || yl < 0) return false;
    int rx = ((xh & 0x0F) << 8) | xl;
    int ry = ((yh & 0x0F) << 8) | yl;

    // The panel reports raw (rotation-0) coordinates; rotate them to match the on-screen
    // orientation (canvas->setRotation), so taps/swipes line up in every rotation.
    const int N = LCD_WIDTH - 1;  // square panel
    switch (displayRotation() & 3) {
        case 1: *x = ry;     *y = N - rx; break;
        case 2: *x = N - rx; *y = N - ry; break;
        case 3: *x = N - ry; *y = rx;     break;
        default: *x = rx;    *y = ry;     break;
    }
    return true;
}
