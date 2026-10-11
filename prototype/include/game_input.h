#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum
{
    GAME_INPUT_FORWARD      = 1u << 0,
    GAME_INPUT_BACKWARD     = 1u << 1,
    GAME_INPUT_TURN_LEFT    = 1u << 2,
    GAME_INPUT_TURN_RIGHT   = 1u << 3,
    GAME_INPUT_STRAFE_LEFT  = 1u << 4,
    GAME_INPUT_STRAFE_RIGHT = 1u << 5,

    GAME_INPUT_FIRE         = 1u << 6,
    GAME_INPUT_USE          = 1u << 7,
    GAME_INPUT_WEAPON_NEXT  = 1u << 8,
    GAME_INPUT_MAP          = 1u << 9,
    GAME_INPUT_ENTER        = 1u << 10,
    GAME_INPUT_RUN          = 1u << 11,
    GAME_INPUT_VIEW_TOGGLE  = 1u << 12,
    GAME_INPUT_PAUSE        = 1u << 13,
    GAME_INPUT_FLARE        = 1u << 14,
};

typedef struct
{
    uint32_t buttons;

    // Latched digital edges since previous poll.
    uint32_t pressed;
    uint32_t released;

    // For Nape these are now time-normalized motion samples,
    // not an unbounded accumulated backlog.
    int16_t aim_x;
    int16_t aim_y;

    int8_t wheel;
} GameInputState;

void game_input_poll(GameInputState* out_state);

void game_input_set_nape_buttons(uint32_t buttons);
void game_input_add_nape_motion(int16_t dx, int16_t dy);
void game_input_add_nape_wheel(int8_t steps);
void game_input_clear_nape(void);

int game_input_get_tuning(uint8_t index);
void game_input_set_tuning(uint8_t index, int value);
void game_input_adjust_tuning(uint8_t index, int delta);

#ifdef __cplusplus
}
#endif
