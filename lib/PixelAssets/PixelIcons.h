#pragma once

#include <cstdint>

namespace PixelIcons {

struct Bitmap {
  const uint8_t* data;
  int width;
  int height;
};

// 电池图标，11x7像素，1-bit单色

constexpr uint8_t BatteryErrorData[] = {
    0b00001100, 0b00000000,
    0b01101101, 0b10000000,
    0b01001100, 0b11000000,
    0b01001100, 0b11000000,
    0b01000000, 0b11000000,
    0b01101101, 0b10000000,
    0b00001100, 0b00000000,
};
constexpr Bitmap BatteryError{BatteryErrorData, 11, 7};

constexpr uint8_t BatteryOfflineData[] = {
    0b00000000, 0b00000000,
    0b01010101, 0b01000000,
    0b00000000, 0b00100000,
    0b01000000, 0b00100000,
    0b00000000, 0b00100000,
    0b01010101, 0b01000000,
    0b00000000, 0b00000000,
};
constexpr Bitmap BatteryOffline{BatteryOfflineData, 11, 7};

constexpr uint8_t BatteryChargingData[] = {
    0b00000000, 0b00000000,
    0b01010001, 0b01000000,
    0b01010001, 0b01100000,
    0b01000000, 0b01100000,
    0b01010001, 0b01100000,
    0b01001110, 0b01000000,
    0b00000000, 0b00000000,
};
constexpr Bitmap BatteryCharging{BatteryChargingData, 11, 7};

constexpr uint8_t BatteryTemplateData[] = {
    0b00000000, 0b00000000,
    0b01111111, 0b11000000,
    0b01000000, 0b01100000,
    0b01000000, 0b01100000,
    0b01000000, 0b01100000,
    0b01111111, 0b11000000,
    0b00000000, 0b00000000,
};
constexpr Bitmap BatteryTemplate{BatteryTemplateData, 11, 7};

// 服务器状态

constexpr uint8_t ServerSyncData[] = {
    0b00000000, 0b00000010, 0b00100000,
    0b00011111, 0b11000111, 0b00100000,
    0b00100000, 0b00101010, 0b10100000,
    0b01000000, 0b00010010, 0b00100000,
    0b01100000, 0b00110010, 0b00100000,
    0b01011111, 0b11010010, 0b00100000,
    0b01100000, 0b00110010, 0b00100000,
    0b01011111, 0b11010010, 0b00100000,
    0b01100000, 0b00110010, 0b00100000,
    0b01011111, 0b11010010, 0b00100000,
    0b01100000, 0b00110010, 0b00100000,
    0b01011111, 0b11010010, 0b10101000,
    0b00100000, 0b00100010, 0b01110000,
    0b00011111, 0b11000010, 0b00100000,
};

constexpr Bitmap ServerSync{ServerSyncData, 22, 14};

constexpr uint8_t ServerOfflineData[] = {
    0b00000000, 0b00000000, 0b00000000,
    0b00011111, 0b11000010, 0b00100000,
    0b00100000, 0b00100010, 0b00100000,
    0b01000000, 0b00010111, 0b11110000,
    0b01100000, 0b00110111, 0b11110000,
    0b01011111, 0b11010011, 0b11100000,
    0b01100000, 0b00110001, 0b11000000,
    0b01011111, 0b11010000, 0b10011000,
    0b01100000, 0b00110000, 0b10011000,
    0b01011111, 0b11010000, 0b10011000,
    0b01100000, 0b00110000, 0b10011000,
    0b01011111, 0b11010000, 0b10000000,
    0b00100000, 0b00100000, 0b10011000,
    0b00011111, 0b11000000, 0b10011000,
};

constexpr Bitmap ServerOffline{ServerOfflineData, 22, 14};

// Add custom 1-bit icons here. Bits are read left-to-right, MSB first.
// Example:
// constexpr uint8_t Wifi8Data[] = {
//     0b00111100,
//     0b01000010,
//     0b10011001,
//     0b00100100,
//     0b00011000,
//     0b00000000,
//     0b00011000,
//     0b00011000,
// };
// constexpr Bitmap Wifi8{Wifi8Data, 8, 8};

}  // namespace PixelIcons
