#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "spi_guard.h"

static SemaphoreHandle_t s_spiMutex = nullptr;

extern "C" void napetune_spi_guard_init(void)
{
    if (!s_spiMutex)
    {
        s_spiMutex = xSemaphoreCreateRecursiveMutex();

        Serial.printf(
            "[SPI GUARD] init=%s\n",
            s_spiMutex ? "OK" : "FAILED"
        );
    }
}

extern "C" void napetune_spi_acquire(void)
{
    if (!s_spiMutex)
    {
        return;
    }

    xSemaphoreTakeRecursive(
        s_spiMutex,
        portMAX_DELAY
    );
}

extern "C" void napetune_spi_release(void)
{
    if (!s_spiMutex)
    {
        return;
    }

    xSemaphoreGiveRecursive(
        s_spiMutex
    );
}

// ------------------------------------------------------------------
// Linker wrapper for the M5 MAX3421E library.
//
// The library calls tuh_max3421_spi_cs_api(rhport, true) immediately
// before beginTransaction()/CS LOW and calls it with false at the end
// of every MAX3421E SPI transaction.
//
// --wrap redirects every external call to this function through here,
// including transfers performed by the library's "max3421 intr" task.
// ------------------------------------------------------------------
extern "C" void __real_tuh_max3421_spi_cs_api(
    uint8_t rhport,
    bool active
);

extern "C" void __wrap_tuh_max3421_spi_cs_api(
    uint8_t rhport,
    bool active
)
{
    if (active)
    {
        napetune_spi_acquire();
        __real_tuh_max3421_spi_cs_api(rhport, true);
    }
    else
    {
        __real_tuh_max3421_spi_cs_api(rhport, false);
        napetune_spi_release();
    }
}

#if defined(NAPETUNE_BOARD_CORES3_LITE)
#include <M5Unified.h>
static unsigned s_lcdDepth = 0;
extern "C" void napetune_lcd_acquire(void)
{
    napetune_spi_acquire();
    if (s_lcdDepth++ == 0) pinMode(35, OUTPUT);
}
extern "C" void napetune_lcd_release(void)
{
    if (--s_lcdDepth == 0) {
        M5.Display.waitDMA();
        pinMode(35, INPUT);
    }
    napetune_spi_release();
}
#endif
