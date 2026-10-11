#pragma once
// Core2 M-BUS pin 20 = GPIO33 (stock); pin 22 = GPIO19 (Grove-free).
#ifndef NAPETUNE_USB_CS_PIN
#if defined(NAPETUNE_BOARD_CORES3_LITE)
#define NAPETUNE_USB_CS_PIN 7
#else
#define NAPETUNE_USB_CS_PIN 33
#endif
#endif

#include "napetune_board_config.h"
