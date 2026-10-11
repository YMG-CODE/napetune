#pragma once
#include <Arduino.h>

bool nape_game_source_begin(void);
bool nape_game_source_ready(void);
uint32_t nape_game_source_motion_rx_count(void);
uint32_t nape_game_source_motion_age_ms(void);

enum NapeGameAction : uint8_t
{
    NAPE_ACTION_OFF = 0,
    NAPE_ACTION_MAP,
    NAPE_ACTION_RUN_TOGGLE,
    NAPE_ACTION_STRAFE_LEFT,
    NAPE_ACTION_STRAFE_RIGHT,
    NAPE_ACTION_FIRE,
    NAPE_ACTION_USE,
    NAPE_ACTION_WEAPON_NEXT,
    NAPE_ACTION_FORWARD,
    NAPE_ACTION_BACKWARD,
    NAPE_ACTION_TURN_LEFT,
    NAPE_ACTION_TURN_RIGHT,
    NAPE_ACTION_GUIDE_TOGGLE,
    NAPE_ACTION_HUD_TOGGLE,
    NAPE_ACTION_THROTTLE_UP,
    NAPE_ACTION_THROTTLE_DOWN,
    NAPE_ACTION_FLARE,
};

uint8_t nape_game_source_get_key_action(uint8_t index);
void nape_game_source_set_key_action(uint8_t index, uint8_t action);
uint8_t nape_game_source_get_mouse_action(uint8_t index);
void nape_game_source_set_mouse_action(uint8_t index, uint8_t action);
int8_t nape_game_source_get_orientation(void);
void nape_game_source_request_orientation(void);
void nape_game_source_request_layer(uint8_t layer);
bool nape_game_source_run_enabled(void);

struct NapeTuningSnapshot
{
    bool connected;
    bool syncing;
    bool runtime_valid;
    uint8_t current_layer;
    uint8_t angle_index;
    uint8_t dpi_stage;
    uint16_t dpi;
    bool custom_dpi_valid;
    uint16_t custom_dpi;
    bool polling_rate_valid;
    uint16_t polling_rate_hz;
    uint8_t dpi_cycle_valid_mask;
    uint16_t dpi_cycle[5];
    uint8_t keymap_valid_mask;
    uint16_t keymap[8][7];
    uint8_t tap_hold_valid[8];
    uint16_t tap_key[8][7];
    uint16_t hold_key[8][7];
    uint8_t wheel_valid[8];
    uint16_t wheel[8][2];
    uint8_t mount_mask;
    uint8_t config_if;
    uint8_t tx_block_reason;
    uint8_t query_kind;
    uint8_t query_layer;
    uint16_t last_report_len;
    uint32_t if3_tx_count;
    uint32_t if3_rx_count;
    uint32_t timeout_count;
    uint32_t last_report_age_ms;
    uint8_t last_report[32];
    uint8_t key_write_status;
};

void nape_tuning_request_sync(void);
void nape_tuning_request_polling_rate(void);
// Queue one VIA dynamic-keymap entry and verify it by reading the same entry
// back. status: 0=idle, 1=writing, 2=verifying, 3=saved, 4=failed.
bool nape_tuning_write_key(uint8_t layer, uint8_t key, uint16_t keycode);
bool nape_tuning_write_wheel(uint8_t layer, uint8_t slot, uint16_t keycode);
void nape_tuning_get_snapshot(NapeTuningSnapshot* out);
