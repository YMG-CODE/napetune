#include <Arduino.h>
#include <M5Unified.h>

#include "core2_bridge.h"
#include "nape_game_source.h"
#include "spi_guard.h"
#include "napetune_save.h"
#include "launcher/game_launcher.h"
#include "mecha/mecha_game.h"
#include "modes/mode_rooms.h"
#include "tuning/tuning_mode.h"

enum class GameMode
{
    LAUNCHER,
    TUNING_MODE,
    MECHA_PROTOCOL,
    IZ_LOUNGE,
    HANGAR
};

static GameMode s_gameMode = GameMode::LAUNCHER;
static bool s_bootReady = false;
static bool s_psramReady = false;
static bool s_restoreLayerAfterGame = false;
static uint8_t s_layerBeforeGame = 0;
static bool s_restoreLayerAfterHangar = false;
static uint8_t s_layerBeforeHangar = 0;


// ============================================================
// SETUP
// ============================================================

void setup()
{
    // One recursive mutex protects the physical SPI bus shared by
    // LCD, microSD and MAX3421E.
    napetune_spi_guard_init();

    // --------------------------------------------------------
    // Core2
    //
    // Display / framebuffer / SD initialization happens here.
    // SD remains available for future mode assets; it is optional at boot.
    // --------------------------------------------------------

    core2_platform_init();


    // --------------------------------------------------------
    // SD is optional for the current three-mode prototype.
    // --------------------------------------------------------

#if !defined(NAPETUNE_BOARD_CORES3_LITE)
    if (!core2_sd_ready())
    {
        Serial.println(
            "[BOOT] SD unavailable; three-mode prototype remains available"
        );
    }

    // Load only the compact versioned save slot. Story, map and sprite assets
    // stay on SD and are referenced by stable IDs/content-pack name.
    napetune_save_begin();
#endif


    // --------------------------------------------------------
    // PSRAM
    // --------------------------------------------------------

    const size_t psramSize =
        ESP.getPsramSize();

    s_psramReady = psramSize != 0;
    if (!s_psramReady)
    {
        Serial.println(
            "[BOOT] PSRAM unavailable; shooting disabled"
        );
    }

    Serial.printf(
        "[BOOT] PSRAM=%u MB\n",
        (unsigned)(
            psramSize /
            (1024 * 1024)
        )
    );

    Serial.printf(
        "[BOOT] heap free=%u psram free=%u\n",
        (unsigned)ESP.getFreeHeap(),
        (unsigned)ESP.getFreePsram()
    );


    // --------------------------------------------------------
    // Show the launcher before probing the optional USB host. A missing or
    // intermittently connected module must not leave a blank boot screen.
    s_gameMode = GameMode::LAUNCHER;
    s_bootReady = true;
    launcher_begin();

    // Nape 2.4 GHz dongle
    //
    // USBHost.task() runs in its own FreeRTOS task on Core 1.
    // --------------------------------------------------------

    bool napeOk =
        nape_game_source_begin();
#if defined(NAPETUNE_BOARD_CORES3_LITE)
    core2_sd_begin();
    napetune_save_begin();
#endif

    Serial.printf(
        "[BOOT] Nape GAME source=%s\n",
        napeOk
            ? "STARTED"
            : "FAILED"
    );


    if (!napeOk)
        Serial.println("[BOOT] Touch controls remain available without Nape USB");
}


// ============================================================
// LOOP
// ============================================================

void loop()
{
    if (!s_bootReady)
    {
        delay(50);
        return;
    }

    switch (s_gameMode)
    {
        case GameMode::LAUNCHER:
        {
            const LauncherSelection choice = launcher_update();
            if (choice == LauncherSelection::TUNING_MODE)
            {
                s_gameMode = GameMode::TUNING_MODE;
                tuning_mode_begin();
            }
            else if (choice == LauncherSelection::MECHA_PROTOCOL ||
                choice == LauncherSelection::SHOWCASE)
            {
                if (!s_psramReady)
                {
                    Serial.println("[LAUNCHER] MECHA needs PSRAM");
                    launcher_begin();
                    break;
                }
                s_gameMode = GameMode::MECHA_PROTOCOL;
                napetune_save_set_last_mode(1);
                napetune_save_checkpoint("enter_mecha");
                // GAME temporarily owns Layer 7. Preserve the NapePro layer
                // that was active before launch so normal use can resume when
                // the game exits. This costs only one byte plus one flag.
                NapeTuningSnapshot napeState = {};
                nape_tuning_get_snapshot(&napeState);
                s_restoreLayerAfterGame =
                    napeState.connected && napeState.runtime_valid;
                if (s_restoreLayerAfterGame)
                    s_layerBeforeGame = napeState.current_layer & 0x07;
                // Layer 7 is reserved for Napetune_GAME controls. The USB
                // task safely defers this request until Nape IF3 is online.
                nape_game_source_request_layer(7);
                if (choice == LauncherSelection::SHOWCASE)
                    mecha_game_begin_showcase();
                else
                    mecha_game_begin();
            }
            else if (choice == LauncherSelection::IZ_LOUNGE)
            {
                s_gameMode = GameMode::IZ_LOUNGE;
                napetune_save_set_last_mode(2);
                napetune_save_checkpoint("enter_lounge");
                mode_room_begin(false);
            }
            else if (choice == LauncherSelection::HANGAR)
            {
                s_gameMode = GameMode::HANGAR;
                napetune_save_set_last_mode(3);
                napetune_save_checkpoint("enter_hangar");
                NapeTuningSnapshot napeState = {};
                nape_tuning_get_snapshot(&napeState);
                s_restoreLayerAfterHangar =
                    napeState.connected && napeState.runtime_valid;
                if (s_restoreLayerAfterHangar)
                    s_layerBeforeHangar = napeState.current_layer & 0x07;
                nape_game_source_request_layer(7);
                mode_room_begin(true);
            }
            break;
        }

        case GameMode::TUNING_MODE:
            if (tuning_mode_update())
            {
                s_gameMode = GameMode::LAUNCHER;
                launcher_begin();
            }
            break;

        case GameMode::MECHA_PROTOCOL:
            if (mecha_game_update())
            {
                mecha_game_end();
                s_gameMode = GameMode::LAUNCHER;
                napetune_save_set_last_mode(0);
                napetune_save_checkpoint("leave_mecha");
                if (s_restoreLayerAfterGame)
                {
                    nape_game_source_request_layer(s_layerBeforeGame);
                    s_restoreLayerAfterGame = false;
                }
                launcher_begin();
            }
            break;

        case GameMode::IZ_LOUNGE:
        case GameMode::HANGAR:
            if (mode_room_update())
            {
                const bool leavingHangar = s_gameMode == GameMode::HANGAR;
                s_gameMode = GameMode::LAUNCHER;
                napetune_save_set_last_mode(0);
                napetune_save_checkpoint("leave_room");
                if (leavingHangar && s_restoreLayerAfterHangar)
                {
                    nape_game_source_request_layer(s_layerBeforeHangar);
                    s_restoreLayerAfterHangar = false;
                }
                launcher_begin();
            }
            break;
    }

    // MECHA has its own measured frame cost and does not need the launcher's
    // coarse 10 ms idle delay. Keep a one-tick yield so USB/RTOS work runs,
    // while avoiding an unconditional frame-rate penalty.
    delay(s_gameMode == GameMode::MECHA_PROTOCOL ? 1 : 10);
}
