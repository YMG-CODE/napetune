#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void core2_platform_init(void);
void core2_video_send(
    const uint8_t* indexed,
    const uint16_t* palette,
    int width,
    int height
);
void core2_video_wait_finish(void);
int core2_sd_ready(void);
void core2_sd_begin(void);
int core2_config_is_active(void);
void core2_config_toggle(void);
void core2_config_touch(int x, int y);
void core2_config_nape_wheel(int steps);
void core2_config_nape_buttons(uint8_t buttons);
void core2_config_move(int direction);
void core2_config_service(void);
void core2_config_render(void);
void core2_reticle_toggle(void);
void core2_draw_battery_indicator(void);
void core2_glass2_service(void);
void core2_glass2_message(const char* english, const char* japanese);
void core2_glass2_paged_message(const char* english, const char* japanese);
// Persistent latest-wins information for menu cursor context. Unlike normal
// timed messages, a new context replaces the previous one instead of queuing.
void core2_glass2_context(
    const char* area,
    const char* english,
    const char* japanese
);
void core2_glass2_pilot_launch_message(void);
void core2_glass2_demo_unlock_message(void);
void core2_glass2_demo_success_message(void);
void core2_glass2_lounge_dialogue(void);
void core2_glass2_lounge_dialogue_event(int event_index);
void core2_glass2_lounge_sleep_message(void);
int core2_glass2_lounge_can_wake_event01(void);
int core2_glass2_ready(void);
int core2_glass2_intro_complete(void);
void core2_glass2_set_demo_playing(int playing);
int core2_glass2_demo_playing(void);
void core2_glass2_set_demo_impulse_unlocked(int unlocked);
void core2_glass2_toggle_demo_page(void);
int core2_glass2_dialogue_active(void);
int core2_glass2_current_speaker_is_izu(void);
void core2_glass2_nape_buttons(uint8_t buttons);
void core2_glass2_local_buttons(uint8_t buttons);
uint8_t core2_glass2_lounge_expression(void);

#ifdef __cplusplus
}
#endif
