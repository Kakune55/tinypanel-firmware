#pragma once

namespace BoardConfig {

// Display: ST7305 RLCD
constexpr int RlcdMosi = RLCD_MOSI;
constexpr int RlcdSclk = RLCD_SCLK;
constexpr int RlcdDc = RLCD_DC;
constexpr int RlcdCs = RLCD_CS;
constexpr int RlcdRst = RLCD_RST;

constexpr int RlcdWidth = RLCD_WIDTH;
constexpr int RlcdHeight = RLCD_HEIGHT;

// I2C: PCF85063 RTC + SHTC3
constexpr int I2cSda = BOARD_I2C_SDA;
constexpr int I2cScl = BOARD_I2C_SCL;

// Battery ADC
constexpr int BatteryAdcPin = BOARD_BAT_ADC;
constexpr float BatteryDividerRatio = BOARD_BAT_DIVIDER;
constexpr float BatteryVoltageCalibration = 1.01456f;
constexpr float BatteryVoltageOffset = 0.0f;

// Buttons
constexpr int ButtonKey = 18;   // KEY, active low
constexpr int ButtonBoot = 0;   // BOOT, active low, boot strapping pin

// MicroSD / TF card: SDMMC 1-bit mode
constexpr int SdmmcClk = 38;
constexpr int SdmmcCmd = 21;
constexpr int SdmmcD0 = 39;
constexpr int SdmmcBusWidth = 1;

}  // namespace BoardConfig
