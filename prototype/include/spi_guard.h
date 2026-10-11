#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void napetune_spi_guard_init(void);
void napetune_spi_acquire(void);
void napetune_spi_release(void);

#ifdef __cplusplus
}
#endif

#if defined(NAPETUNE_BOARD_CORES3_LITE)
extern "C" void napetune_lcd_acquire(void);
extern "C" void napetune_lcd_release(void);
#else
#define napetune_lcd_acquire napetune_spi_acquire
#define napetune_lcd_release napetune_spi_release
#endif
