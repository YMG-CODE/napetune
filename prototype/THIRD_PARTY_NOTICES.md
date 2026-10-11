# Third-party notices

Upstream copyright/license notices embedded in source files are retained.
The licenses below do not become YMGWorks licenses.

| Component | Version / source | Terms |
|---|---|---|
| M5Unified | 0.2.25, https://github.com/m5stack/M5Unified | MIT; LICENSES/M5Unified-MIT.txt |
| M5GFX | 0.2.32, https://github.com/m5stack/M5GFX | MIT and component notices; LICENSES/M5GFX-MIT.txt |
| M5 MAX3421E USB library | Vendored tuned copy, https://github.com/m5stack/M5-Max3421E-USBShield | MIT; lib/M5-Max3421E-USBShield/LICENSE |
| TinyUSB / Adafruit TinyUSB | Sources within the vendored USB library | MIT notices in each source file; retain those notices |
| ST STM32 FSDEV definitions | Vendored USB source portable/st/stm32_fsdev/fsdev_stm32.h | Original BSD-style notice preserved in that header; unused on the ESP32 target |
| Japanese IPA-derived font | M5GFX lgfxJapanGothic family | IPA Font License v1.0; LICENSES/IPA-Font-License-v1.0.txt |
| Adafruit GFX fonts | Within M5GFX | Original BSD-style font notice; LICENSES/Adafruit-GFX-Fonts.txt |
| Arduino-ESP32 | 2.0.17, installed by espressif32 6.12.0; https://github.com/espressif/arduino-esp32/tree/2.0.17 | LGPL-2.1-or-later for Arduino core portions; bundled SDK/components retain their own notices |
| ESP-IDF / ESP32 SDK | Arduino-ESP32 supplied SDK | Apache-2.0 and third-party component licenses; https://github.com/espressif/esp-idf |

PlatformIO downloads M5Unified, M5GFX and the Arduino/SDK dependencies; their sources are not repackaged here.
Do not apply the root MIT license to dependencies. SDK/component license files must be checked when redistributing them.

## Vendored USB changes

The USB library includes local MAX3421E IRQ servicing, SPI serialization/diagnostic and HID handling changes inherited from the NapeTune development copy.
The upstream copyright headers remain intact; these changes do not replace the upstream license.

## Source-only prototype release

This publication contains buildable source and creative assets, not firmware binaries.
For any later firmware release, include the applicable library license notices, exact corresponding LGPL library sources,
local patches and the build/relink material required by the applicable LGPL terms.
Creative-content permissions must not prevent modification/relinking of LGPL libraries for personal use.
See https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html and https://www.gnu.org/licenses/gpl-faq.html .

## Removed legacy files

PrBoom, PrBoom ESP32 compatibility code, WAD tables, old ESP-IDF component builds and the Freedoom WAD placeholder
are not part of this publication. No Freedoom WAD is included.
This is a review of the selected publication files and observed dependencies, not a claim about all upstream SDK files.
