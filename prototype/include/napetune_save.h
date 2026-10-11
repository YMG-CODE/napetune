#pragma once

#include <Arduino.h>

// Versioned, fixed-size save data. Keep story/map content on SD and store only
// stable IDs and player state here so future content packs do not invalidate a
// save slot.
struct NapetuneSaveData
{
    char magic[8];
    uint16_t format_version;
    uint16_t struct_size;
    uint32_t sequence;
    uint32_t crc32;

    char content_pack[24];
    uint16_t chapter_id;
    uint16_t scene_id;
    uint8_t last_mode;
    uint8_t settings_flags;
    uint16_t reserved0;

    uint32_t launch_count;
    uint32_t credits;
    uint32_t play_seconds;
    uint8_t event_flags[64];
    uint8_t inventory[48];
    uint8_t relationship[16];

    uint16_t arm_part;
    uint16_t leg_part;
    uint16_t thruster_part;
    uint16_t weapon_slot[2];
    uint8_t reserved[112];
};

bool napetune_save_begin();
bool napetune_save_available();
bool napetune_save_dirty();
const NapetuneSaveData& napetune_save_data();

void napetune_save_set_last_mode(uint8_t mode);
void napetune_save_set_story_position(
    const char* content_pack,
    uint16_t chapter_id,
    uint16_t scene_id
);
void napetune_save_set_event_flag(uint16_t flag_id, bool enabled);

// Performs a small, synchronous, atomic SD write. Call only at checkpoints,
// menus, or mode transitions--never from the measured gameplay frame path.
bool napetune_save_checkpoint(const char* reason);

