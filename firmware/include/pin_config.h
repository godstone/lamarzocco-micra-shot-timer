// Pin map for LilyGO T-Display-S3-AMOLED 1.43" (product code H741).
// Values taken from the official repo:
//   Xinyuan-LilyGO/T-Display-S3-AMOLED-1.43-1.75 -> components/private_library/pin_config.h
#pragma once

// ---- AMOLED (QSPI) ----
// NOTE: this unit is the CO5300 panel variant (LilyGO panel code DO0143FMST10), confirmed by
// bring-up: SH8601 left the screen dark, CO5300 (with col-offset 6) works. Driver is selected
// in display.cpp via USE_CO5300 (default 1). The other 1.43" panel (DO0143FAT01) uses SH8601.
#define LCD_SDIO0 11
#define LCD_SDIO1 13
#define LCD_SDIO2 14
#define LCD_SDIO3 15
#define LCD_SCLK 12
#define LCD_CS 10
#define LCD_RST 17
#define LCD_EN 16  // panel power enable

// Panel reports 473x467 (usable area 466x466 round).
#define LCD_WIDTH 466
#define LCD_HEIGHT 466

// ---- I2C (FT3168 touch, PCF8563 RTC, SY6970 charger) ----
#define IIC_SDA 7
#define IIC_SCL 6
#define TP_INT 9
#define PCF8563_INT 9

// ---- Battery ----
#define BATTERY_VOLTAGE_ADC_DATA 4

// ---- microSD ----
#define SD_CS 38
#define SD_MOSI 39
#define SD_MISO 40
#define SD_SCLK 41
