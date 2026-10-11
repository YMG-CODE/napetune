#pragma once

void mecha_game_begin(bool demoMode = false);
void mecha_game_begin_showcase();
bool mecha_game_update();
void mecha_game_draw();
void mecha_game_end();
void mecha_game_request_model_capture(uint8_t mode);
bool mecha_game_get_hybrid_render();
void mecha_game_set_hybrid_render(bool enabled);
bool mecha_game_get_autopilot();
void mecha_game_set_autopilot(bool enabled);
