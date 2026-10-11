#pragma once

#include <stdint.h>

// FLARE is a defensive system on the fixed F8 input, not a selectable weapon.
constexpr uint8_t GAME_WEAPON_COUNT = 5;

enum : uint8_t
{
    PROGRESS_CYAN_THRUSTER_OWNED = 1u << 0,
    PROGRESS_CYAN_THRUSTER_EQUIPPED = 1u << 1,
};

uint8_t game_progress_load();
bool game_progress_save(uint8_t flags);

// Versioned loadout. Part IDs are deliberately small so additional variants
// can be added without tying the save format to artwork or stat constants.
struct GameLoadout
{
    uint8_t version;
    uint8_t arms;       // 0 standard, 1 assault
    uint8_t legs;       // 0 standard, 1 agile
    uint8_t thruster;   // 0 standard, 1 cyan (requires Iz's gift)
    uint8_t weapon1;    // 0 barrage, 1 missile, 2 beam, 3 multi, 4 blade
    uint8_t weapon2;
};

GameLoadout game_loadout_load();
bool game_loadout_save(const GameLoadout& loadout);
