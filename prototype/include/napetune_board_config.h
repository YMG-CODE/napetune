#pragma once
// Shared board routing; Core2 USB CS defaults to GPIO19.
namespace napetune_board {
struct Pins { int sck, mosi, miso, sd_cs, glass_sda, glass_scl, usb_cs, usb_int; };
#if defined(NAPETUNE_BOARD_CORES3_LITE)
constexpr Pins pins{36, 37, 35, 4, 2, 1, 7, 10};
constexpr const char* name = "CoreS3 Lite";
constexpr bool usb_pins_verified = true; // Confirmed in standalone and coexistence diagnostics.
#else
#ifndef NAPETUNE_USB_CS_PIN
#define NAPETUNE_USB_CS_PIN 19
#endif
constexpr Pins pins{18, 23, 38, 4, 32, 33, NAPETUNE_USB_CS_PIN, 35};
constexpr const char* name = "Core2";
#endif
}
