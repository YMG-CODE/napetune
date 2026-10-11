#include <Arduino.h>
#include <Preferences.h>

#include "progression/game_progress.h"

uint8_t game_progress_load()
{
    Preferences store;
    if (!store.begin("game_prog", true))
    {
        Serial.println("[PROGRESS] read unavailable");
        return 0;
    }
    const uint8_t flags = store.getUChar("flags", 0);
    store.end();
    return flags & (PROGRESS_CYAN_THRUSTER_OWNED |
                    PROGRESS_CYAN_THRUSTER_EQUIPPED);
}

bool game_progress_save(uint8_t flags)
{
    Preferences store;
    if (!store.begin("game_prog", false))
    {
        Serial.println("[PROGRESS] write unavailable");
        return false;
    }
    const uint8_t valid = flags & (PROGRESS_CYAN_THRUSTER_OWNED |
                                   PROGRESS_CYAN_THRUSTER_EQUIPPED);
    const bool ok = store.putUChar("flags", valid) == 1;
    store.end();
    Serial.printf("[PROGRESS] save flags=0x%02X %s\n", valid,
                  ok ? "OK" : "FAILED");
    return ok;
}

GameLoadout game_loadout_load()
{
    static_assert(sizeof(GameLoadout) == 6, "Loadout save layout changed");
    const uint8_t progress = game_progress_load();
    GameLoadout result = {1, 0, 0,
        (uint8_t)((progress & PROGRESS_CYAN_THRUSTER_EQUIPPED) ? 1 : 0),
        0, 1};
    Preferences store;
    if (store.begin("game_prog", true))
    {
        GameLoadout saved = {};
        if (store.getBytesLength("loadout") == sizeof(saved) &&
            store.getBytes("loadout", &saved, sizeof(saved)) == sizeof(saved) &&
            saved.version == 1 && saved.arms <= 1 && saved.legs <= 1 &&
            saved.thruster <= 1 && saved.weapon1 < GAME_WEAPON_COUNT &&
            saved.weapon2 < GAME_WEAPON_COUNT &&
            saved.weapon1 != saved.weapon2)
            result = saved;
        store.end();
    }
    if (!(progress & PROGRESS_CYAN_THRUSTER_OWNED)) result.thruster = 0;
    return result;
}

bool game_loadout_save(const GameLoadout& loadout)
{
    if (loadout.version != 1 || loadout.arms > 1 || loadout.legs > 1 ||
        loadout.thruster > 1 || loadout.weapon1 >= GAME_WEAPON_COUNT ||
        loadout.weapon2 >= GAME_WEAPON_COUNT || loadout.weapon1 == loadout.weapon2)
        return false;
    const uint8_t progress = game_progress_load();
    if (loadout.thruster && !(progress & PROGRESS_CYAN_THRUSTER_OWNED))
        return false;
    Preferences store;
    if (!store.begin("game_prog", false)) return false;
    const bool ok = store.putBytes("loadout", &loadout, sizeof(loadout)) ==
                    sizeof(loadout);
    store.end();
    Serial.printf("[LOADOUT] save arm=%u leg=%u thr=%u w=%u,%u %s\n",
        loadout.arms, loadout.legs, loadout.thruster,
        loadout.weapon1, loadout.weapon2, ok ? "OK" : "FAILED");
    return ok;
}
