#include <Arduino.h>
#include <M5Unified.h>
#include <esp_heap_caps.h>

#include "game_input.h"
#include "core2_bridge.h"
#include "core2_input_source.h"

// ============================================================
// Core2 temporary control source
//
// y=0..59,   x<230  FORWARD
// y=60..119, x<230  BACKWARD
// y=0..119,  x>=230 USE
// y=120..179 left   MAP
// y=120..179 right  WEAPON NEXT
// y>=180             reserved for Core2 A/B/C strip
//
// A = TURN LEFT
// B = FIRE (+ short ENTER pulse)
// C = TURN RIGHT
// ============================================================

// Time the four parts of the input poll without changing their scheduling.
// One summary per ten seconds keeps serial output out of the hot path.
static void recordInputPollTiming(uint32_t m5Us, uint32_t configUs,
                                  uint32_t glassUs, uint32_t touchUs)
{
    struct InputPollStats
    {
        uint32_t sinceMs;
        uint32_t samples;
        uint64_t sums[4];
        uint32_t peaks[4];
    };
    // Volatile keeps the timing work present in the no-log A/B build.
    static volatile InputPollStats* stats = nullptr;
    if (!stats)
    {
        stats = static_cast<InputPollStats*>(
            heap_caps_calloc(1, sizeof(InputPollStats), MALLOC_CAP_SPIRAM));
        if (!stats) return;
    }
    const uint32_t times[4] = {m5Us, configUs, glassUs, touchUs};
    for (int i = 0; i < 4; ++i)
    {
        stats->sums[i] += times[i];
        if (times[i] > stats->peaks[i]) stats->peaks[i] = times[i];
    }
    ++stats->samples;
    const uint32_t now = millis();
    if (!stats->sinceMs) stats->sinceMs = now;
    if (now - stats->sinceMs < 10000) return;
#if CORE2_INPUT_PERF_LOG
    Serial.printf("[CORE2 INPUT PERF] samples=%u m5=%u/%u config=%u/%u glass=%u/%u touch=%u/%u us(avg/max)\n",
                  (unsigned)stats->samples,
                  (unsigned)(stats->sums[0] / stats->samples), (unsigned)stats->peaks[0],
                  (unsigned)(stats->sums[1] / stats->samples), (unsigned)stats->peaks[1],
                  (unsigned)(stats->sums[2] / stats->samples), (unsigned)stats->peaks[2],
                  (unsigned)(stats->sums[3] / stats->samples), (unsigned)stats->peaks[3]);
#endif
    stats->sinceMs = now;
    stats->samples = 0;
    for (int i = 0; i < 4; ++i) { stats->sums[i] = 0; stats->peaks[i] = 0; }
}

extern "C" uint32_t core2_input_source_poll(void)
{
    const uint32_t pollStartUs = micros();
    M5.update();
    const uint32_t m5DoneUs = micros();
    core2_config_service();
    const uint32_t configDoneUs = micros();
    core2_glass2_service();
    const uint32_t glassDoneUs = micros();
    const auto finishPoll = [&](uint32_t mask) -> uint32_t {
        recordInputPollTiming(m5DoneUs - pollStartUs,
                              configDoneUs - m5DoneUs,
                              glassDoneUs - configDoneUs,
                              micros() - glassDoneUs);
        return mask;
    };

    uint32_t mask = 0;

    // Core2 A/B/C are touch zones, so simultaneous A+C is not reliable.
    // Long-hold the battery area at the upper-right instead.
    static uint32_t configHoldSince = 0;
    static bool configHoldLatched = false;
    static bool configHotspotTracking = false;
    static bool demoTouchWasDown = false;
    static bool menuHotspotTracking = false;
    const bool touchDownNow = M5.Touch.getCount() > 0;
    bool configHotspotDown = false;
    int touchX = -1;
    int touchY = -1;
    if (touchDownNow)
    {
        auto t = M5.Touch.getDetail(0);
        touchX = t.x;
        touchY = t.y;
        configHotspotDown = t.isPressed() && t.x >= 255 && t.y < 50;
    }

    if (configHotspotDown)
    {
        if (!configHotspotTracking)
        {
            configHotspotTracking = true;
            configHoldSince = millis();
            configHoldLatched = false;
        }
        if (!configHoldLatched && millis() - configHoldSince >= 1000)
        {
            configHoldLatched = true;
            core2_config_toggle();
        }
    }
    else
    {
        if (configHotspotTracking && !configHoldLatched)
            core2_reticle_toggle();
        configHotspotTracking = false;
        configHoldSince = 0;
        configHoldLatched = false;
    }

    if (core2_config_is_active())
    {
        demoTouchWasDown = touchDownNow;
        static bool touchWasDown = false;
        const bool touchDown = touchDownNow;
        if (M5.BtnA.wasPressed())
            core2_config_move(-1);
        if (M5.BtnB.wasPressed())
            core2_config_move(1);

        // y>=200 is the Core2 A/B/C strip. It is handled exclusively
        // by the button edge checks above to prevent one press from also
        // being interpreted as a screen tap.
        if (
            touchDown && !touchWasDown && !configHotspotDown &&
            touchY >= 0 && touchY < 200
        )
        {
            core2_config_touch(touchX, touchY);
        }
        touchWasDown = touchDown;
        return finishPoll(0);
    }

    // Reserve the battery icon hotspot exclusively for opening config.
    if (configHotspotDown)
    {
        demoTouchWasDown = touchDownNow;
        return finishPoll(0);
    }

    if (core2_glass2_demo_playing())
    {
        const bool pageTap = touchDownNow && touchY >= 0 && touchY < 200;
        const bool menuHotspotDown = touchDownNow &&
            touchX >= 0 && touchX <= 64 && touchY >= 0 && touchY <= 49;

        // The upper-left corner has two deliberate gestures in MECHA:
        // a short tap changes the GLASS2 page, while a 1.5 s hold opens the
        // menu-return confirmation. Delay only this corner until release so
        // beginning a long hold cannot also change the GLASS2 page.
        if (menuHotspotDown && !demoTouchWasDown)
            menuHotspotTracking = true;
        if (!touchDownNow && menuHotspotTracking)
        {
            core2_glass2_toggle_demo_page();
            menuHotspotTracking = false;
        }
        else if (pageTap && !demoTouchWasDown && !menuHotspotDown)
            core2_glass2_toggle_demo_page();
        demoTouchWasDown = touchDownNow;
        if (pageTap) return finishPoll(0);
    }
    else
    {
        demoTouchWasDown = touchDownNow;
        menuHotspotTracking = false;
    }

    if (M5.BtnA.isPressed())
    {
        mask |= GAME_INPUT_TURN_LEFT;
    }

    if (M5.BtnC.isPressed())
    {
        mask |= GAME_INPUT_TURN_RIGHT;
    }

    if (M5.BtnB.isPressed())
    {
        mask |= GAME_INPUT_FIRE;
    }

    // B also provides a short menu ENTER pulse on the press edge.
    static uint32_t enterPulseUntil = 0;

    if (M5.BtnB.wasPressed())
    {
        enterPulseUntil = millis() + 90;
    }

    if ((int32_t)(enterPulseUntil - millis()) > 0)
    {
        mask |= GAME_INPUT_ENTER;
    }

    if (M5.Touch.getCount() > 0)
    {
        auto t = M5.Touch.getDetail(0);

        if (t.isPressed())
        {
            if (t.y < 120)
            {
                if (t.x >= 230)
                {
                    mask |= GAME_INPUT_USE;
                }
                else if (t.y < 60)
                {
                    mask |= GAME_INPUT_FORWARD;
                }
                else
                {
                    mask |= GAME_INPUT_BACKWARD;
                }
            }
            else if (t.y < 180)
            {
                if (t.x < 160)
                {
                    mask |= GAME_INPUT_MAP;
                }
                else
                {
                    mask |= GAME_INPUT_WEAPON_NEXT;
                }
            }

            // y >= 180 is reserved for A/B/C.
        }
    }

    return finishPoll(mask);
}
