#include <Arduino.h>

#include "game_input.h"
#include "core2_input_source.h"

// ============================================================
// GAME INPUT v0.6.8
// TIME-WINDOW VELOCITY
//
// Digital:
//   Stable Nape button state is edge-detected exactly once at poll time.
//   v0.7.4 removes the extra pressed/released edge latch that could
//   duplicate DOWN/UP events.
//
// Trackball:
//   Every HID packet contributes dx/dy only to the CURRENT time window.
//   At game_input_poll(), the window is consumed and reset.
//
//   output ~= (sum of dx/dy in window) * TARGET_MS / elapsed_ms
//
// Therefore:
//   - no reservoir/backlog
//   - no synthetic inertia
//   - no stale "last velocity"
//   - report bursts are normalized by actual elapsed time
// ============================================================

static portMUX_TYPE s_inputMux =
    portMUX_INITIALIZER_UNLOCKED;

// ---------- digital ----------
static volatile uint32_t s_napeButtons  = 0;
static uint32_t s_previousCombinedButtons = 0;

// ---------- motion window ----------
static volatile int32_t s_windowSumX = 0;
static volatile int32_t s_windowSumY = 0;
static volatile uint32_t s_windowPackets = 0;

static volatile uint32_t s_firstMotionUs = 0;
static volatile uint32_t s_lastMotionUs  = 0;

// ---------- wheel ----------
static volatile int32_t s_napeWheel = 0;

// These settings used to live in PrBoom's gamepad bridge. Keep them in the
// shared input layer so the MECHA controls survive removal of Freedoom.
static int s_tuning[9] = {100, 65, 110, 64, 16, 32, 75, 100, 100};

extern "C" int game_input_get_tuning(uint8_t index)
{
    return index < 9 ? s_tuning[index] : 0;
}

extern "C" void game_input_set_tuning(uint8_t index, int value)
{
    if (index >= 9) return;
    const int minimum = index <= 2 ? 10 : (index <= 5 ? 4 :
                        (index == 8 ? 40 : 20));
    const int maximum = index <= 2 || index == 6 || index == 7 ? 200 :
                        (index == 8 ? 180 : 120);
    if (value < minimum) value = minimum;
    if (value > maximum) value = maximum;
    s_tuning[index] = value;
}

extern "C" void game_input_adjust_tuning(uint8_t index, int delta)
{
    game_input_set_tuning(index, game_input_get_tuning(index) + delta);
}


// ============================================================
// TUNING
// ============================================================

// Normalize movement to roughly one Doom tic (~28.6ms).
static constexpr uint32_t NAPE_TARGET_WINDOW_US = 28571;

// If the only report in a window is extremely recent, don't divide
// by a tiny duration and explode the result.
static constexpr uint32_t NAPE_MIN_WINDOW_US = 8000;

// Anything older than this is considered a new gesture.
// This also guarantees "ball stopped = output zero".
static constexpr uint32_t NAPE_GESTURE_GAP_US = 70000;

// Emergency cap BEFORE the PrBoom response curve.
// This is deliberately much lower than earlier rawX=1202 bursts.
static constexpr int32_t NAPE_NORMALIZED_CLAMP_X = 120;
static constexpr int32_t NAPE_NORMALIZED_CLAMP_Y = 80;


static int32_t clamp32(
    int32_t v,
    int32_t lo,
    int32_t hi
)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}


static int16_t clamp16(int32_t v)
{
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}


static int8_t clamp8(int32_t v)
{
    if (v > 127) return 127;
    if (v < -128) return -128;
    return (int8_t)v;
}


// ============================================================
// POLL
// ============================================================

extern "C" void game_input_poll(
    GameInputState* out_state
)
{
    if (!out_state)
        return;

    const uint32_t core2Buttons =
        core2_input_source_poll();

    uint32_t napeButtons;
    int32_t sumX;
    int32_t sumY;
    uint32_t packets;
    uint32_t firstUs;
    uint32_t lastUs;

    int32_t wheel;

    portENTER_CRITICAL(&s_inputMux);

    napeButtons  = s_napeButtons;
    // Consume exactly one time window.
    sumX = s_windowSumX;
    sumY = s_windowSumY;
    packets = s_windowPackets;
    firstUs = s_firstMotionUs;
    lastUs  = s_lastMotionUs;

    s_windowSumX = 0;
    s_windowSumY = 0;
    s_windowPackets = 0;
    s_firstMotionUs = 0;
    s_lastMotionUs = 0;

    wheel = s_napeWheel;
    s_napeWheel = 0;

    portEXIT_CRITICAL(&s_inputMux);

    const uint32_t combined =
        core2Buttons | napeButtons;

    uint32_t pressed =
        combined & ~s_previousCombinedButtons;

    uint32_t released =
        s_previousCombinedButtons & ~combined;

    s_previousCombinedButtons =
        combined;


    int32_t normalizedX = 0;
    int32_t normalizedY = 0;

    if (
        packets > 0 &&
        firstUs != 0 &&
        lastUs != 0
    )
    {
        const uint32_t nowUs =
            micros();

        // If the last packet is already old, the user has stopped.
        // Do NOT replay old movement.
        const uint32_t ageUs =
            (uint32_t)(nowUs - lastUs);

        if (ageUs <= NAPE_GESTURE_GAP_US)
        {
            uint32_t spanUs =
                (uint32_t)(lastUs - firstUs);

            // One packet => spanUs can be zero.
            if (spanUs < NAPE_MIN_WINDOW_US)
                spanUs = NAPE_MIN_WINDOW_US;

            int64_t nx =
                ((int64_t)sumX *
                 (int64_t)NAPE_TARGET_WINDOW_US) /
                (int64_t)spanUs;

            int64_t ny =
                ((int64_t)sumY *
                 (int64_t)NAPE_TARGET_WINDOW_US) /
                (int64_t)spanUs;

            normalizedX =
                clamp32(
                    (int32_t)nx,
                    -NAPE_NORMALIZED_CLAMP_X,
                    NAPE_NORMALIZED_CLAMP_X
                );

            normalizedY =
                clamp32(
                    (int32_t)ny,
                    -NAPE_NORMALIZED_CLAMP_Y,
                    NAPE_NORMALIZED_CLAMP_Y
                );
        }
    }

    out_state->buttons  = combined;
    out_state->pressed  = pressed;
    out_state->released = released;

    out_state->aim_x =
        clamp16(normalizedX);

    out_state->aim_y =
        clamp16(normalizedY);

    out_state->wheel =
        clamp8(wheel);
}



// ============================================================
// BUTTONS
// ============================================================

extern "C" void game_input_set_nape_buttons(
    uint32_t buttons
)
{
    portENTER_CRITICAL(&s_inputMux);

    // State only. game_input_poll() owns edge generation.
    // This guarantees one DOWN/UP event per actual stable transition.
    s_napeButtons =
        buttons;

    portEXIT_CRITICAL(&s_inputMux);
}


// ============================================================
// MOTION
// ============================================================

extern "C" void game_input_add_nape_motion(
    int16_t dx,
    int16_t dy
)
{
    const uint32_t nowUs =
        micros();

    portENTER_CRITICAL(&s_inputMux);

    // If reports resumed after a pause, start a completely new window.
    if (
        s_lastMotionUs != 0 &&
        (uint32_t)(nowUs - s_lastMotionUs) >
            NAPE_GESTURE_GAP_US
    )
    {
        s_windowSumX = 0;
        s_windowSumY = 0;
        s_windowPackets = 0;
        s_firstMotionUs = 0;
    }

    if (s_windowPackets == 0)
    {
        s_firstMotionUs =
            nowUs;
    }

    s_windowSumX +=
        (int32_t)dx;

    s_windowSumY +=
        (int32_t)dy;

    ++s_windowPackets;

    s_lastMotionUs =
        nowUs;

    portEXIT_CRITICAL(&s_inputMux);
}


// ============================================================
// WHEEL
// ============================================================

extern "C" void game_input_add_nape_wheel(
    int8_t steps
)
{
    portENTER_CRITICAL(&s_inputMux);

    s_napeWheel +=
        steps;

    portEXIT_CRITICAL(&s_inputMux);
}


// ============================================================
// CLEAR
// ============================================================

extern "C" void game_input_clear_nape(void)
{
    portENTER_CRITICAL(&s_inputMux);

    s_napeButtons = 0;

    s_windowSumX = 0;
    s_windowSumY = 0;
    s_windowPackets = 0;
    s_firstMotionUs = 0;
    s_lastMotionUs = 0;

    s_napeWheel = 0;

    portEXIT_CRITICAL(&s_inputMux);
}
