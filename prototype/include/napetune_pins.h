#pragma once
// Core2 uses M-BUS pin 22 = GPIO19 for USB CS.
#ifndef NAPETUNE_USB_CS_PIN
#if defined(NAPETUNE_BOARD_CORES3_LITE)
#define NAPETUNE_USB_CS_PIN 7
#else
#define NAPETUNE_USB_CS_PIN 19
#endif
#endif

#include "napetune_board_config.h"
