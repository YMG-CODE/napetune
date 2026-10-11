#include <Arduino.h>
#include <M5_Max3421E_Usb.h>
#include "portable/analog/max3421/max3421_probe.h"
#include "arduino/max3421_irq_probe.h"

#include "nape_game_source.h"
#include "game_input.h"
#include "core2_bridge.h"
#include "napetune_pins.h"

// ============================================================
// Napetune Nape INPUT v7.12.6 RICH BEAM
//
// ・感度変更なし
// ・Time-Window変更なし
// ・armed=1 に対する Soft Kick なし
// ・USB強制リセットなし
//
// 観測するもの:
//   USB task loop
//   IF0～IF4 RX
//   last RX age
//   mount
//   armed
//   rearm fail
//   MAX3421E INT
//   heap
//   長時間停止後の自然復帰
// ============================================================

static M5_USBH_Host s_usbHost(
    &SPI,
    napetune_board::pins.sck, // SCK
    napetune_board::pins.mosi, // MOSI
    napetune_board::pins.miso, // MISO
    NAPETUNE_USB_CS_PIN, // CS: must match physical USB module switch
    napetune_board::pins.usb_int  // INT
);

static constexpr uint16_t NAPE_DONGLE_VID = 0x3434;
static constexpr uint16_t NAPE_DONGLE_PID = 0xD026;

static constexpr uint8_t MAX_IF = 5;
static constexpr int NAPE_INT_PIN = napetune_board::pins.usb_int;
static constexpr bool VERBOSE_DIAGNOSTICS = false;
static constexpr uint32_t HEALTH_LOG_INTERVAL_MS = 30000;


// ============================================================
// USB state
// ============================================================

static volatile uint8_t s_mountMask = 0;
static volatile bool s_ready = false;
static uint8_t s_dev = 0;

static volatile bool s_ifArmed[MAX_IF] =
{
    false, false, false, false, false
};

static volatile uint32_t s_rxCount[MAX_IF] =
{
    0, 0, 0, 0, 0
};

static volatile uint32_t s_lastRxMs[MAX_IF] =
{
    0, 0, 0, 0, 0
};

static volatile uint32_t s_totalRx = 0;
static volatile uint32_t s_lastAnyRxMs = 0;

static volatile uint32_t s_rearmFail = 0;
static volatile uint32_t s_rearmRecovered = 0;

static volatile uint32_t s_motionPackets = 0;
static volatile uint32_t s_buttonTransitions = 0;
static uint8_t s_if0MouseReportId = 0;
static bool s_if0ReportLayoutLogged = false;
static bool s_if2WheelPlusDown = false;
static bool s_if2WheelMinusDown = false;
static bool s_if2FlareKeyDown = false;
static uint8_t s_if2ConfigKeyMask = 0;
// Low 16 bits are enough for a short, wrap-safe post-key wheel guard.
// Packing the four key flags leaves DRAM headroom on the Core2 build.
static uint16_t s_lastConfigKeyTick = 0;
static constexpr uint16_t WHEEL_CHORD_GUARD_MS = 250;
static bool s_runToggle = false;
static volatile uint32_t s_auxGameButtons = 0;
static volatile uint8_t s_keyActions[4] = {
    NAPE_ACTION_MAP,
    NAPE_ACTION_RUN_TOGGLE,
    NAPE_ACTION_STRAFE_LEFT,
    NAPE_ACTION_STRAFE_RIGHT,
};
static volatile uint8_t s_mouseActions[2] = {
    NAPE_ACTION_FIRE,
    NAPE_ACTION_USE,
};
static volatile int8_t s_napeOrientation = -1;
static volatile bool s_orientationRequest = false;
static volatile bool s_orientationQueryInFlight = false;
static uint8_t s_orientationPacket[32] = {};
static uint32_t s_lastOrientationQueryAttemptMs = 0;
static uint8_t s_orientationQueryAttempts = 0;
static constexpr uint32_t ORIENTATION_QUERY_RETRY_MS = 1000;
static constexpr uint8_t ORIENTATION_QUERY_MAX_ATTEMPTS = 5;
static volatile bool s_layerRequest = false;
static volatile uint8_t s_requestedLayer = 7;
static volatile int8_t s_currentLayer = -1;
static uint8_t s_layerPacket[32] = {};
static uint32_t s_lastLayerAttemptMs = 0;
static uint8_t s_layerAttempts = 0;
static constexpr uint32_t LAYER_SET_RETRY_MS = 800;
static constexpr uint8_t LAYER_SET_MAX_ATTEMPTS = 5;

// Read-only data used by Tuning Mode. Configuration writes are deliberately
// not exposed here: the first integration only mirrors NapePro state.
static portMUX_TYPE s_tuningMux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_tuningSyncRequested = false;
static volatile bool s_tuningSyncing = false;
static volatile bool s_tuningDpiRefreshRequested = false;
static volatile bool s_tuningPollingRefreshRequested = false;
static bool s_tuningDpiSingleRefresh = false;
static volatile bool s_tuningRuntimeValid = false;
static volatile uint8_t s_tuningLayer = 0;
static volatile uint8_t s_tuningAngleIndex = 0;
static volatile uint8_t s_tuningDpiStage = 0xFF;
static volatile uint16_t s_tuningDpi = 0;
static volatile bool s_tuningCustomDpiValid = false;
static volatile uint16_t s_tuningCustomDpi = 0;
static volatile bool s_tuningPollingRateValid = false;
static volatile uint16_t s_tuningPollingRateHz = 0;
static volatile uint8_t s_tuningDpiCycleValidMask = 0;
static uint16_t s_tuningDpiCycle[5] = {};
static volatile uint8_t s_tuningKeymapValidMask = 0;
static uint16_t s_tuningKeymap[8][7] = {};
static uint8_t s_tuningTapHoldValid[8] = {};
static uint16_t s_tuningTapKey[8][7] = {};
static uint16_t s_tuningHoldKey[8][7] = {};
static uint8_t s_tuningWheelValid[8] = {};
static uint16_t s_tuningWheel[8][2] = {};
static uint8_t s_tuningPacket[32] = {};
static volatile bool s_keyWritePending = false;
static volatile bool s_keyWriteInFlight = false;
static volatile uint8_t s_keyWriteStatus = 0;
static uint8_t s_keyWriteLayer = 0;
static uint8_t s_keyWriteKey = 0;
static uint16_t s_keyWriteCode = 0;
static bool s_keyWriteIsWheel = false;
static uint8_t s_keyWritePacket[32] = {};

enum class TuningQuery : uint8_t
{
    NONE, CURRENT_LAYER, KEY_VERIFY, KEYMAP, TAP_HOLD, WHEEL_UP, WHEEL_DOWN, STAGE, DPI,
    CUSTOM_DPI, POLLING
};
static volatile TuningQuery s_tuningQuery = TuningQuery::NONE;
static volatile bool s_tuningQueryInFlight = false;
static uint8_t s_tuningQueryLayer = 0;
static uint8_t s_tuningQueryKey = 0;
static uint8_t s_tuningRetryCount = 0;
static uint32_t s_tuningQueryStartedMs = 0;
static constexpr uint32_t TUNING_QUERY_TIMEOUT_MS = 650;
static constexpr uint8_t TUNING_QUERY_MAX_RETRIES = 2;
static volatile uint32_t s_tuningIf3TxCount = 0;
static volatile uint32_t s_tuningIf3RxCount = 0;
static volatile uint32_t s_tuningTimeoutCount = 0;
static volatile uint32_t s_tuningLastReportMs = 0;
static volatile uint16_t s_tuningLastReportLen = 0;
static uint8_t s_tuningLastReport[32] = {};
static volatile uint8_t s_tuningConfigIf = 0xFF;
static volatile uint8_t s_tuningTxBlockReason = 0;

// Raw HID button state and debounced/stable game state.
static volatile uint8_t s_mouseButtons = 0;
static volatile uint8_t s_pendingButtons = 0;
static volatile uint8_t s_stableButtons = 0;
static volatile uint32_t s_pendingButtonSinceMs = 0;
static constexpr uint32_t BUTTON_DEBOUNCE_MS = 8;


// ============================================================
// Long-run diagnostics
// ============================================================

static volatile uint32_t s_usbTaskLoops = 0;

static volatile bool s_if0SleepSuspect = false;
static uint32_t s_lastSuspectLogMs = 0;
static int s_lastIntLevel = -1;
static bool s_lastObservedIf0Armed = false;

static uint32_t s_lastHcdHxfr[32] = {};
static uint32_t s_lastHcdDone[32] = {};
static uint32_t s_lastHcdNak[32] = {};
static uint32_t s_lastHcdRcv[32] = {};

static constexpr uint32_t IF0_SLEEP_SUSPECT_MS = 30000;
static constexpr uint32_t SUSPECT_LOG_INTERVAL_MS = 5000;

static uint32_t s_lastRetryMs[MAX_IF] =
{
    0, 0, 0, 0, 0
};

static constexpr uint32_t REARM_RETRY_MS = 50;

static constexpr uint32_t STALE_NOTICE_MS = 3000;

static uint32_t s_lastStaleNoticeMs = 0;

static bool s_if0WasStale = false;
static uint32_t s_if0StaleStartedMs = 0;
static uint32_t s_lastWakeStallLogMs = 0;

// v0.9.1: A quiet HID endpoint is normal while the user is not touching
// the Nape.  Therefore elapsed time since the last report must NEVER be
// sufficient by itself to reset the USB host.
//
// We only arm recovery for a short period after real IF0 activity.
// If IF0 then goes silent inside that period, it is treated as a possible
// transfer stall.  Once the activity window expires, silence is IDLE.
static volatile uint32_t s_lastIf0ActivityMs = 0;
static constexpr uint32_t IF0_ACTIVITY_WINDOW_MS = 2500;

// ============================================================
// v0.9.0 USB HOST RESET recovery
//
// v0.8 proved:
//   - all HID aborts return OK
//   - all HID rearms return OK
//   - but IF0 never delivers another report
//
// Therefore recovery moves BELOW the HID class.
//
// Sequence:
//   IF0 stale >= 6 sec
//     -> clear GAME INPUT
//     -> deinitialize TinyUSB HOST stack on rhport 0
//        (MAX3421 HCD deinit resets/powers down controller)
//     -> wait 250 ms
//     -> initialize TinyUSB HOST stack again
//     -> MAX3421 HCD initializes and performs bus reset
//     -> wait for real HID MOUNT callbacks / enumeration
//     -> wait for first real IF0 report
//
// IMPORTANT:
// We deliberately do NOT call s_usbHost.begin() again.
// M5_USBH_Host::begin() creates ESP32 MAX3421 interrupt task/semaphore.
// Calling it repeatedly would leak/duplicate that infrastructure.
// Instead we reuse the already-created M5 host wrapper and only cycle
// TinyUSB with tuh_deinit(0) / tuh_init(0).
// ============================================================

static constexpr uint32_t IF0_RECOVERY_TRIGGER_MS = 1200;
static constexpr uint32_t HOST_RESET_OFF_MS = 250;
static constexpr uint32_t HOST_RESET_ENUM_TIMEOUT_MS = 6000;
static constexpr uint32_t HOST_RESET_COOLDOWN_MS = 15000;

enum class If0RecoveryState : uint8_t
{
    IDLE = 0,
    HOST_OFF_WAIT,
    WAIT_ENUM,
    WAIT_IF0,
    COOLDOWN
};

static volatile If0RecoveryState s_if0RecoveryState =
    If0RecoveryState::IDLE;

static volatile uint32_t s_hostResetAttempts = 0;
static volatile uint32_t s_hostDeinitOk = 0;
static volatile uint32_t s_hostDeinitFail = 0;
static volatile uint32_t s_hostInitOk = 0;
static volatile uint32_t s_hostInitFail = 0;
static volatile uint32_t s_hostResetRecovered = 0;
static volatile uint32_t s_hostResetTimeout = 0;

static uint32_t s_if0RecoveryStateSinceMs = 0;
static uint32_t s_lastRecoveryAttemptMs = 0;
static uint32_t s_mountMaskAtResetStart = 0;

// ============================================================
// Helpers
// ============================================================

static uint32_t ageOf(
    uint8_t instance,
    uint32_t now
)
{
    if (
        instance >= MAX_IF ||
        s_lastRxMs[instance] == 0
    )
    {
        return 0;
    }

    return now - s_lastRxMs[instance];
}


static void logHcdProbe(bool start)
{
    Serial.println(
        start ? "[NAPE HCD PROBE START]" : "[NAPE HCD PROBE]"
    );

    max3421_probe_registers_t regs = {};
    if (tuh_max3421_probe_registers(0, &regs))
    {
        Serial.printf(
            "[NAPE MAX3421 REG] HIRQ=%02X HIEN=%02X HRSL=%02X "
            "HXFR=%02X MODE=%02X USBIRQ=%02X USBIEN=%02X "
            "PINCTL=%02X CPUCTL=%02X INT=%d\n",
            regs.hirq, regs.hien, regs.hrsl, regs.hxfr, regs.mode,
            regs.usbirq, regs.usbien, regs.pinctl, regs.cpuctl,
            digitalRead(NAPE_INT_PIN)
        );
    }

    uint32_t gpioIsr = 0;
    uint32_t semWake = 0;
    uint32_t lowService = 0;
    uint32_t handler = 0;
    uint32_t lastHandlerMs = 0;
    m5_max3421_irq_probe_snapshot(
        &gpioIsr, &semWake, &lowService, &handler, &lastHandlerMs
    );
    Serial.printf(
        "[NAPE IRQ SERVICE] gpioISR=%u semWake=%u lowService=%u "
        "handler=%u lastAge=%u\n",
        (unsigned)gpioIsr,
        (unsigned)semWake,
        (unsigned)lowService,
        (unsigned)handler,
        lastHandlerMs ? (unsigned)(millis() - lastHandlerMs) : 0
    );

    const uint8_t slotCount = tuh_max3421_probe_slot_count();
    for (uint8_t slot = 0; slot < slotCount && slot < 32; ++slot)
    {
        max3421_probe_snapshot_t p = {};
        if (!tuh_max3421_probe_snapshot(s_dev, slot, &p))
            continue;

        // HID endpoints are interrupt endpoints (xfer_type == 3).  Print
        // every IN endpoint so IF0 can be identified by submit_count ~= rx0+1.
        if (p.xfer_type != 3 || (p.ep_addr & 0x80) == 0)
            continue;

        const uint32_t dHxfr = start ? 0 : p.hxfr_count - s_lastHcdHxfr[slot];
        const uint32_t dDone = start ? 0 : p.hxfrdn_count - s_lastHcdDone[slot];
        const uint32_t dNak = start ? 0 : p.nak_count - s_lastHcdNak[slot];
        const uint32_t dRcv = start ? 0 : p.rcvdav_count - s_lastHcdRcv[slot];

        Serial.printf(
            "slot=%u ep=%02X intv=%u mps=%u state=%u busy=%u "
            "sub=%u hxfr=%u(+%u) done=%u(+%u) nak=%u(+%u) "
            "rcv=%u(+%u) ok=%u fail=%u hrsl=%02X frame=%u "
            "cur=%u/%02X hirq=%02X hien=%02X len=%u/%u tog=%u\n",
            p.slot, p.ep_addr, p.interval, p.packet_size,
            p.state, p.controller_busy,
            (unsigned)p.submit_count,
            (unsigned)p.hxfr_count, (unsigned)dHxfr,
            (unsigned)p.hxfrdn_count, (unsigned)dDone,
            (unsigned)p.nak_count, (unsigned)dNak,
            (unsigned)p.rcvdav_count, (unsigned)dRcv,
            (unsigned)p.success_count, (unsigned)p.fail_count,
            p.last_hrsl, p.frame_count, p.peraddr, p.hxfr,
            p.hirq, p.hien,
            p.xferred_len, p.total_len, p.data_toggle
        );

        s_lastHcdHxfr[slot] = p.hxfr_count;
        s_lastHcdDone[slot] = p.hxfrdn_count;
        s_lastHcdNak[slot] = p.nak_count;
        s_lastHcdRcv[slot] = p.rcvdav_count;
    }
}


static uint32_t actionToButton(uint8_t action)
{
    switch (action)
    {
        case NAPE_ACTION_MAP:          return GAME_INPUT_MAP;
        case NAPE_ACTION_STRAFE_LEFT:  return GAME_INPUT_STRAFE_LEFT;
        case NAPE_ACTION_STRAFE_RIGHT: return GAME_INPUT_STRAFE_RIGHT;
        case NAPE_ACTION_FIRE:         return GAME_INPUT_FIRE;
        case NAPE_ACTION_USE:          return GAME_INPUT_USE;
        case NAPE_ACTION_FORWARD:      return GAME_INPUT_FORWARD;
        case NAPE_ACTION_BACKWARD:     return GAME_INPUT_BACKWARD;
        case NAPE_ACTION_TURN_LEFT:    return GAME_INPUT_TURN_LEFT;
        case NAPE_ACTION_TURN_RIGHT:   return GAME_INPUT_TURN_RIGHT;
        case NAPE_ACTION_GUIDE_TOGGLE:return GAME_INPUT_VIEW_TOGGLE;
        case NAPE_ACTION_HUD_TOGGLE:  return GAME_INPUT_PAUSE;
        case NAPE_ACTION_THROTTLE_UP: return GAME_INPUT_FORWARD;
        case NAPE_ACTION_THROTTLE_DOWN:return GAME_INPUT_BACKWARD;
        case NAPE_ACTION_FLARE:        return GAME_INPUT_FLARE;
        default:                       return 0;
    }
}

static bool configKeyChordActive(uint32_t now)
{
    return s_if2ConfigKeyMask != 0 ||
        (s_lastConfigKeyTick != 0 &&
         (uint16_t)((uint16_t)now - s_lastConfigKeyTick) <= WHEEL_CHORD_GUARD_MS);
}

static void updateGameButtons(
    uint8_t buttons
)
{
    uint32_t mask = 0;

    if (buttons & 0x01) mask |= actionToButton(s_mouseActions[0]);
    if (buttons & 0x02) mask |= actionToButton(s_mouseActions[1]);

    mask |= s_auxGameButtons;

    game_input_set_nape_buttons(mask);
}


static void serviceStableButtons(uint32_t now)
{
    if (s_pendingButtons == s_stableButtons)
        return;

    if ((uint32_t)(now - s_pendingButtonSinceMs) < BUTTON_DEBOUNCE_MS)
        return;

    s_stableButtons = s_pendingButtons;
    ++s_buttonTransitions;

    if (VERBOSE_DIAGNOSTICS)
    {
        Serial.printf(
            "[NAPE BTN STABLE] raw=%02X FIRE=%u USE=%u\n",
            s_stableButtons,
            (s_stableButtons & 0x01) ? 1 : 0,
            (s_stableButtons & 0x02) ? 1 : 0
        );
    }

    updateGameButtons(s_stableButtons);
}


static const char* recoveryStateName()
{
    switch (s_if0RecoveryState)
    {
        case If0RecoveryState::IDLE:          return "IDLE";
        case If0RecoveryState::HOST_OFF_WAIT: return "HOST_OFF_WAIT";
        case If0RecoveryState::WAIT_ENUM:     return "WAIT_ENUM";
        case If0RecoveryState::WAIT_IF0:      return "WAIT_IF0";
        case If0RecoveryState::COOLDOWN:      return "COOLDOWN";
    }

    return "?";
}


static void clearNapeRuntimeState(uint32_t now)
{
    game_input_clear_nape();

    s_mouseButtons = 0;
    s_pendingButtons = 0;
    s_stableButtons = 0;
    s_auxGameButtons = 0;
    s_if2ConfigKeyMask = 0;
    s_lastConfigKeyTick = 0;
    s_if2WheelPlusDown = false;
    s_if2WheelMinusDown = false;
    s_if2FlareKeyDown = false;
    s_runToggle = false;
    s_pendingButtonSinceMs = now;
}


static void clearUsbRuntimeState()
{
    s_mountMask = 0;
    s_ready = false;
    s_dev = 0;

    for (uint8_t i = 0; i < MAX_IF; ++i)
    {
        s_ifArmed[i] = false;
        s_lastRetryMs[i] = 0;
    }
}


static bool submitReceive(
    uint8_t instance
)
{
    if (
        !s_dev ||
        instance >= MAX_IF
    )
    {
        return false;
    }

    const bool ok =
        tuh_hid_receive_report(
            s_dev,
            instance
        );

    if (ok)
    {
        s_ifArmed[instance] = true;
    }
    else
    {
        s_ifArmed[instance] = false;
        ++s_rearmFail;
    }

    return ok;
}


// ============================================================
// USB task
// ============================================================

static void finishTuningSync()
{
    s_tuningQuery = TuningQuery::NONE;
    s_tuningQueryInFlight = false;
    s_tuningSyncing = false;
    Serial.printf("[NAPE TUNING] sync complete maps=%02X dpi=%u\n",
                  s_tuningKeymapValidMask, (unsigned)s_tuningDpi);
}

static void advanceTuningQuery()
{
    s_tuningQueryInFlight = false;
    s_tuningRetryCount = 0;
    if (s_tuningQuery == TuningQuery::KEY_VERIFY)
    {
        if (s_keyWriteStatus == 2) s_keyWriteStatus = 4;
        finishTuningSync();
    }
    else if (s_tuningQuery == TuningQuery::CURRENT_LAYER)
    {
        s_tuningQueryLayer = 0;
        s_tuningQueryKey = 0;
        s_tuningQuery = TuningQuery::KEYMAP;
    }
    else if (s_tuningQuery == TuningQuery::KEYMAP)
    {
        if (s_tuningQueryLayer < 7) ++s_tuningQueryLayer;
        else
        {
            s_tuningQueryLayer = 0;
            s_tuningQueryKey = 0;
            // Read the always-visible HUD values before the expensive
            // 8-layer x 7-key TAP/HOLD table. If a later optional query times
            // out, WHEEL/DPI/CUSTOM DPI are already available on screen.
            s_tuningQuery = TuningQuery::WHEEL_UP;
        }
    }
    else if (s_tuningQuery == TuningQuery::TAP_HOLD)
    {
        if (s_tuningQueryKey < 6) ++s_tuningQueryKey;
        else if (s_tuningQueryLayer < 7)
        {
            ++s_tuningQueryLayer;
            s_tuningQueryKey = 0;
        }
        else
        {
            finishTuningSync();
        }
    }
    else if (s_tuningQuery == TuningQuery::WHEEL_UP)
    {
        s_tuningQuery = TuningQuery::WHEEL_DOWN;
    }
    else if (s_tuningQuery == TuningQuery::WHEEL_DOWN)
    {
        if (s_tuningQueryLayer < 7)
        {
            ++s_tuningQueryLayer;
            s_tuningQuery = TuningQuery::WHEEL_UP;
        }
        else s_tuningQuery = TuningQuery::STAGE;
    }
    else if (s_tuningQuery == TuningQuery::STAGE)
    {
        s_tuningQueryLayer = 0;
        s_tuningQueryKey = 0;
        s_tuningQuery = TuningQuery::DPI;
    }
    else if (s_tuningQuery == TuningQuery::DPI)
    {
        if (s_tuningDpiSingleRefresh) finishTuningSync();
        else if (s_tuningQueryLayer < 4) ++s_tuningQueryLayer;
        else s_tuningQuery = TuningQuery::CUSTOM_DPI;
    }
    else if (s_tuningQuery == TuningQuery::CUSTOM_DPI)
    {
        // Polling-rate live readback is not yet proven. Do not make the main
        // sync wait for it; continue with the optional TAP/HOLD table last.
        s_tuningQueryLayer = 0;
        s_tuningQueryKey = 0;
        s_tuningQuery = TuningQuery::TAP_HOLD;
    }
    else finishTuningSync();
}

static void serviceTuningQuery(uint32_t now)
{
    if (s_tuningSyncRequested)
    {
        s_tuningSyncRequested = false;
        s_tuningSyncing = true;
        // Launcher returns to the keymap screen by querying A3 first.  This
        // reads NapePro's selected layer instead of reusing a stale Core2 UI
        // value, then the ordinary snapshot queries follow.
        s_tuningQuery = TuningQuery::CURRENT_LAYER;
        s_tuningQueryLayer = 0;
        s_tuningRetryCount = 0;
        s_tuningQueryInFlight = false;
        s_tuningDpiSingleRefresh = false;
        // Keep the last complete snapshot visible while refreshing. New
        // responses replace it progressively; a transient timeout must not
        // turn every key into "---" or destroy the last known-good map.
    }
    else if (s_tuningDpiRefreshRequested && !s_tuningSyncing)
    {
        s_tuningDpiRefreshRequested = false;
        if (s_tuningDpiStage <= 4)
        {
            s_tuningSyncing = true;
            s_tuningDpiSingleRefresh = true;
            s_tuningQuery = TuningQuery::DPI;
            s_tuningQueryLayer = s_tuningDpiStage;
            s_tuningRetryCount = 0;
            s_tuningQueryInFlight = false;
        }
    }
    else if (s_tuningPollingRefreshRequested && !s_tuningSyncing)
    {
        s_tuningPollingRefreshRequested = false;
        s_tuningSyncing = true;
        s_tuningDpiSingleRefresh = false;
        s_tuningQuery = TuningQuery::POLLING;
        s_tuningRetryCount = 0;
        s_tuningQueryInFlight = false;
    }
    if (!s_tuningSyncing || s_tuningQuery == TuningQuery::NONE) return;
    if (s_tuningConfigIf >= MAX_IF || !s_dev ||
        !(s_mountMask & (1u << s_tuningConfigIf)))
    {
        s_tuningTxBlockReason = 1;
        return;
    }

    if (s_tuningQueryInFlight)
    {
        if (now - s_tuningQueryStartedMs < TUNING_QUERY_TIMEOUT_MS) return;
        s_tuningQueryInFlight = false;
        if (++s_tuningRetryCount > TUNING_QUERY_MAX_RETRIES)
        {
            ++s_tuningTimeoutCount;
            Serial.printf("[NAPE TUNING RX TIMEOUT] query=%u layer=%u count=%u\n",
                          (unsigned)s_tuningQuery,
                          (unsigned)s_tuningQueryLayer,
                          (unsigned)s_tuningTimeoutCount);
            advanceTuningQuery();
            return;
        }
    }

    // Never compete with the proven game-layer and orientation transactions.
    if (s_layerRequest)
    {
        s_tuningTxBlockReason = 2;
        return;
    }
    if (s_orientationQueryInFlight)
    {
        s_tuningTxBlockReason = 3;
        return;
    }
    s_tuningTxBlockReason = 0;

    memset(s_tuningPacket, 0, sizeof(s_tuningPacket));
    if (s_tuningQuery == TuningQuery::CURRENT_LAYER)
    {
        // Explicit current/default layer query captured from Launcher:
        // OUT A3 00 ... -> IN A3 [layer+1] 00 ...
        s_tuningPacket[0] = 0xA3;
    }
    else if (s_tuningQuery == TuningQuery::KEY_VERIFY)
    {
        const uint16_t offset =
            0x0E * (s_keyWriteLayer + 1) + s_keyWriteKey * 2;
        s_tuningPacket[0] = 0x12;
        s_tuningPacket[1] = (uint8_t)(offset >> 8);
        s_tuningPacket[2] = (uint8_t)offset;
        s_tuningPacket[3] = 0x02;
    }
    else if (s_tuningQuery == TuningQuery::KEYMAP)
    {
        const uint16_t offset = 0x0E * (s_tuningQueryLayer + 1);
        s_tuningPacket[0] = 0x12;
        s_tuningPacket[1] = (uint8_t)(offset >> 8);
        s_tuningPacket[2] = (uint8_t)offset;
        s_tuningPacket[3] = 0x0E;
    }
    else if (s_tuningQuery == TuningQuery::TAP_HOLD)
    {
        // Launcher capture: A7 26 [layer+1] [angle] [key] reads the two
        // little-endian keycodes stored by A7 25: TAP first, HOLD second.
        // The captured Layer 6 / 0-degree / M2 request was 07 00 05.
        s_tuningPacket[0] = 0xA7;
        s_tuningPacket[1] = 0x26;
        s_tuningPacket[2] = (uint8_t)(s_tuningQueryLayer + 1);
        s_tuningPacket[3] = 0x00;
        s_tuningPacket[4] = s_tuningQueryKey;
    }
    else if (s_tuningQuery == TuningQuery::WHEEL_UP ||
             s_tuningQuery == TuningQuery::WHEEL_DOWN)
    {
        // Launcher capture: 14 [Layer+1] 00 [0=UP,1=DOWN] 00 00...
        s_tuningPacket[0] = 0x14;
        s_tuningPacket[1] = (uint8_t)(s_tuningQueryLayer + 1);
        s_tuningPacket[3] =
            s_tuningQuery == TuningQuery::WHEEL_DOWN ? 1 : 0;
    }
    else if (s_tuningQuery == TuningQuery::STAGE)
    {
        s_tuningPacket[0] = 0xA7;
        s_tuningPacket[1] = 0x21;
    }
    else if (s_tuningQuery == TuningQuery::DPI)
    {
        s_tuningPacket[0] = 0xA7;
        s_tuningPacket[1] = 0x24;
        s_tuningPacket[2] = s_tuningQueryLayer;
    }
    else if (s_tuningQuery == TuningQuery::CUSTOM_DPI)
    {
        // Capture confirms 0x37 is SET_CUSTOM_DPI. Its adjacent read-only
        // partner is 0x36; never send 0x37 from Tuning Mode.
        s_tuningPacket[0] = 0xA7;
        s_tuningPacket[1] = 0x36;
    }
    else
    {
        // Read-only runtime/base-state request. 0x0E is the setter and is
        // deliberately never emitted by Tuning Mode.
        s_tuningPacket[0] = 0xA7;
        s_tuningPacket[1] = 0x0D;
    }

    // Wireshark/USBPcap confirmation (NapePro launcher):
    //   bmRequestType=0x21, SET_REPORT, wValue=0x0200,
    //   wIndex=3, wLength=32. The response then arrives on interrupt IN 0x83.
    // The Launcher sends the runtime/polling request over interrupt OUT.
    // Other proven configuration reads retain their SET_REPORT transport.
    // A3 is proven on interrupt OUT. Runtime A7 0D is accepted through
    // SET_REPORT as well, which is substantially more reliable on MAX3421
    // and benefits from the completion callback's interrupt-IN rearm.
    const bool useInterruptOut =
        s_tuningQuery == TuningQuery::CURRENT_LAYER;
    const bool sent = useInterruptOut
        ? (tuh_hid_send_ready(s_dev, s_tuningConfigIf) &&
           tuh_hid_send_report(s_dev, s_tuningConfigIf, 0,
                               s_tuningPacket, sizeof(s_tuningPacket)))
        : tuh_hid_set_report(
              s_dev, s_tuningConfigIf, 0, HID_REPORT_TYPE_OUTPUT,
              s_tuningPacket, sizeof(s_tuningPacket));

    if (sent)
    {
        ++s_tuningIf3TxCount;
        s_tuningQueryInFlight = true;
        s_tuningQueryStartedMs = now;
        Serial.printf("[NAPE TUNING SET_REPORT] n=%u if=%u query=%u layer=%u raw=",
                      (unsigned)s_tuningIf3TxCount,
                      (unsigned)s_tuningConfigIf,
                      (unsigned)s_tuningQuery,
                      (unsigned)s_tuningQueryLayer);
        for (uint8_t i = 0; i < 8; ++i)
            Serial.printf("%02X%s", s_tuningPacket[i], i == 7 ? "\n" : " ");
    }
    else
    {
        s_tuningTxBlockReason = 5;
    }
}

static void napeUsbTask(void*)
{
    Serial.println(
        "[NAPE GAME] USB task started v7.12.6 RICH BEAM"
    );

    uint32_t lastHealthMs = millis();

    for (;;)
    {
        ++s_usbTaskLoops;

        int intLevel = digitalRead(NAPE_INT_PIN);
        if (s_lastIntLevel < 0)
        {
            s_lastIntLevel = intLevel;
        }
        else if (intLevel != s_lastIntLevel)
        {
            const uint32_t edgeNow = millis();
            if (s_if0SleepSuspect && VERBOSE_DIAGNOSTICS)
            {
                Serial.printf(
                    "[NAPE INT EDGE] %d->%d t=%u rx0=%u age0=%u armed=%u suspect=1\n",
                    s_lastIntLevel,
                    intLevel,
                    (unsigned)edgeNow,
                    (unsigned)s_rxCount[0],
                    (unsigned)ageOf(0, edgeNow),
                    s_ifArmed[0] ? 1 : 0
                );
            }
            s_lastIntLevel = intLevel;
        }

        s_usbHost.task(
            1,
            false
        );

        const uint32_t now = millis();

        // VIA dynamic-keymap single-key write. NapePro treats the bulk-buffer
        // command (0x13) as a volatile edit, so use 0x05: this follows the
        // firmware's persistent per-key path. Nape exposes one 7-key matrix
        // row and reserves protocol layer 0; UI layers 0..7 map to 1..8.
        if (s_keyWritePending && !s_keyWriteInFlight &&
            s_tuningConfigIf < MAX_IF && s_dev &&
            (s_mountMask & (1u << s_tuningConfigIf)))
        {
            const uint16_t offset = s_keyWriteIsWheel ? 0 :
                (uint16_t)(0x0E * (s_keyWriteLayer + 1) + s_keyWriteKey * 2);
            memset(s_keyWritePacket, 0, sizeof(s_keyWritePacket));
            if (s_keyWriteIsWheel)
            {
                // VIA/Nape encoder assignment companion to read command 0x14.
                s_keyWritePacket[0] = 0x15;
                s_keyWritePacket[1] = (uint8_t)(s_keyWriteLayer + 1);
                s_keyWritePacket[2] = 0x00;
                s_keyWritePacket[3] = s_keyWriteKey; // 0=+, 1=-
                s_keyWritePacket[4] = (uint8_t)(s_keyWriteCode >> 8);
                s_keyWritePacket[5] = (uint8_t)s_keyWriteCode;
            }
            else
            {
                s_keyWritePacket[0] = 0x05;
                s_keyWritePacket[1] = (uint8_t)(s_keyWriteLayer + 1);
                s_keyWritePacket[2] = 0x00; // matrix row
                s_keyWritePacket[3] = s_keyWriteKey; // matrix column
                s_keyWritePacket[4] = (uint8_t)(s_keyWriteCode >> 8);
                s_keyWritePacket[5] = (uint8_t)s_keyWriteCode;
            }
            if (tuh_hid_set_report(
                    s_dev, s_tuningConfigIf, 0, HID_REPORT_TYPE_OUTPUT,
                    s_keyWritePacket, sizeof(s_keyWritePacket)))
            {
                s_keyWritePending = false;
                s_keyWriteInFlight = true;
                s_keyWriteStatus = 1;
                Serial.printf("[NAPE %s WRITE] L%u K%u code=%04X offset=%04X\n",
                              s_keyWriteIsWheel ? "WHEEL 15" : "KEY 05",
                              s_keyWriteLayer, s_keyWriteKey,
                              s_keyWriteCode, offset);
            }
        }

        // Layer selection uses the Raw-HID configuration collection. The PC
        // Launcher uses interrupt OUT, but MAX3421 can leave that OUT lane
        // unavailable while interrupt IN is armed. Configuration SET_REPORT
        // is reliable on this host stack and the device accepts the same
        // 32-byte A7 2D payload there; its completion callback also rearms IN.
        if (s_layerRequest && s_layerAttempts >= LAYER_SET_MAX_ATTEMPTS &&
            now - s_lastLayerAttemptMs >= LAYER_SET_RETRY_MS)
        {
            Serial.printf("[NAPE LAYER] no confirmation for layer=%u\n",
                          s_requestedLayer);
            s_layerRequest = false;
        }
        if (
            s_layerRequest &&
            s_dev &&
            s_tuningConfigIf < MAX_IF &&
            (s_mountMask & (1u << s_tuningConfigIf)) &&
            now - s_lastLayerAttemptMs >= LAYER_SET_RETRY_MS
        )
        {
            memset(s_layerPacket, 0, sizeof(s_layerPacket));
            s_layerPacket[0] = 0xA7; // KC_MISC_CMD_GROUP
            s_layerPacket[1] = 0x2D; // NAPE_SET_LAYER
            // NapePro's wire format is one-based (01..08), while the UI and
            // internal state use zero-based layer indices (0..7).
            s_layerPacket[2] = (uint8_t)(s_requestedLayer + 1u);
            const bool sent = tuh_hid_set_report(
                s_dev, s_tuningConfigIf, 0, HID_REPORT_TYPE_OUTPUT,
                s_layerPacket, sizeof(s_layerPacket)
            );
            s_lastLayerAttemptMs = now;
            if (sent)
            {
                ++s_layerAttempts;
                Serial.printf("[NAPE LAYER] request=%u attempt=%u\n",
                              s_requestedLayer, s_layerAttempts);
            }
        }

        if (s_orientationRequest && s_orientationQueryInFlight &&
            now - s_lastOrientationQueryAttemptMs >= ORIENTATION_QUERY_RETRY_MS)
        {
            // A lost IF3 reply must not block the complete Tuning sync.
            s_orientationQueryInFlight = false;
            if (s_orientationQueryAttempts >= ORIENTATION_QUERY_MAX_ATTEMPTS)
            {
                s_orientationRequest = false;
                Serial.println("[NAPE ORI QUERY] response timeout; continue sync");
            }
        }

        if (
            s_orientationRequest &&
            !s_layerRequest &&
            !s_orientationQueryInFlight &&
            s_dev &&
            (s_mountMask & (1u << 3)) &&
            now - s_lastOrientationQueryAttemptMs >= ORIENTATION_QUERY_RETRY_MS &&
            tuh_hid_send_ready(s_dev, 3)
        )
        {
            memset(s_orientationPacket, 0, sizeof(s_orientationPacket));
            s_orientationPacket[0] = 0xA7;
            s_orientationPacket[1] = 0x20; // NAPE_GET_ORI (read only)
            const bool sent = tuh_hid_send_report(
                s_dev, 3, 0, s_orientationPacket, sizeof(s_orientationPacket)
            );
            s_lastOrientationQueryAttemptMs = now;
            ++s_orientationQueryAttempts;
            s_orientationQueryInFlight = sent;
            if (sent)
            {
                Serial.printf("[NAPE ORI QUERY] IF3 queued attempt=%u\n",
                              s_orientationQueryAttempts);
            }
            else if (s_orientationQueryAttempts >= ORIENTATION_QUERY_MAX_ATTEMPTS)
            {
                Serial.printf("[NAPE ORI QUERY] IF3 unavailable; stop after %u attempts\n",
                              s_orientationQueryAttempts);
                s_orientationRequest = false;
            }
        }

        serviceTuningQuery(now);

        // Sample again immediately after TinyUSB processing so an INT
        // transition caused while task() ran is still observable.
        intLevel = digitalRead(NAPE_INT_PIN);
        if (intLevel != s_lastIntLevel)
        {
            if (s_if0SleepSuspect && VERBOSE_DIAGNOSTICS)
            {
                Serial.printf(
                    "[NAPE INT EDGE] %d->%d t=%u rx0=%u age0=%u armed=%u suspect=1\n",
                    s_lastIntLevel,
                    intLevel,
                    (unsigned)now,
                    (unsigned)s_rxCount[0],
                    (unsigned)ageOf(0, now),
                    s_ifArmed[0] ? 1 : 0
                );
            }
            s_lastIntLevel = intLevel;
        }

        const bool if0Armed = s_ifArmed[0];
        if (if0Armed != s_lastObservedIf0Armed)
        {
            if (VERBOSE_DIAGNOSTICS)
            {
                Serial.printf(
                    "[NAPE IF0 ARM EDGE] %u->%u t=%u rx0=%u age0=%u\n",
                    s_lastObservedIf0Armed ? 1 : 0,
                    if0Armed ? 1 : 0,
                    (unsigned)now,
                    (unsigned)s_rxCount[0],
                    (unsigned)ageOf(0, now)
                );
            }
            s_lastObservedIf0Armed = if0Armed;
        }

        // Commit a raw button change only after it remained stable
        // for a short debounce interval. No HID/config writes occur.
        serviceStableButtons(now);


        // ----------------------------------------------------
        // 本当に unarmed のIFだけ低頻度で再試行
        // ----------------------------------------------------

        if (s_dev)
        {
            for (
                uint8_t i = 0;
                i < MAX_IF;
                ++i
            )
            {
                // IF0 recovery owns all IF0 re-arm attempts while
                // its state machine is active. This prevents the old:
                //
                //   generic rearm OK
                //        ->
                //   delayed recovery rearm FAIL
                //
                // race.
                if (
                    (s_mountMask & (1u << i)) &&
                    !s_ifArmed[i] &&
                    i != 0 &&
                    now - s_lastRetryMs[i] >=
                        REARM_RETRY_MS
                )
                {
                    s_lastRetryMs[i] = now;

                    if (submitReceive(i))
                    {
                        ++s_rearmRecovered;

                        Serial.printf(
                            "[NAPE RECOVERY] IF%u rearm=OK "
                            "recovered=%u\n",
                            i,
                            (unsigned)s_rearmRecovered
                        );
                    }
                }
            }
        }


        // ----------------------------------------------------
        // v0.9.2 IF0 observation only
        //
        // Quiet while armed is a valid HID idle state.
        // Do NOT call tuh_deinit()/tuh_init() automatically.
        //
        // If IF0 ever becomes genuinely unarmed while still mounted,
        // only log it here.  We intentionally avoid forcing a receive
        // submission until we have proved that this state correlates
        // with the real long-run failure.
        // ----------------------------------------------------

        static uint32_t lastIf0ObserveMs = 0;

        if (
            s_ready &&
            (s_mountMask & 0x01) &&
            now - lastIf0ObserveMs >= 3000
        )
        {
            lastIf0ObserveMs = now;

            if (!s_ifArmed[0])
            {
                Serial.printf(
                    "[NAPE IF0 UNARMED] rx=%u age=%ums INT=%d "
                    "(OBSERVE ONLY / NO HOST RESET)\n",
                    (unsigned)s_rxCount[0],
                    (unsigned)ageOf(0, now),
                    digitalRead(NAPE_INT_PIN)
                );
            }
            else if (
                VERBOSE_DIAGNOSTICS &&
                s_lastRxMs[0] != 0 &&
                ageOf(0, now) >= 5000
            )
            {
                Serial.printf(
                    "[NAPE IDLE] IF0 quiet age=%ums armed=1 INT=%d "
                    "-> NORMAL / NO RESET\n",
                    (unsigned)ageOf(0, now),
                    digitalRead(NAPE_INT_PIN)
                );
            }
        }


        // ----------------------------------------------------
        // WAKE PROBE: observation only.  Never submit, abort, reset, or
        // reinitialize USB merely because IF0 has been quiet.
        // ----------------------------------------------------

        if (
            !s_if0SleepSuspect &&
            s_ready &&
            (s_mountMask & 0x01) &&
            s_lastRxMs[0] != 0 &&
            ageOf(0, now) >= IF0_SLEEP_SUSPECT_MS
        )
        {
            s_if0SleepSuspect = true;
            s_lastSuspectLogMs = now;

            // If USB stopped while FIRE/USE was down, the release report can
            // no longer arrive. Release only the game-side latched buttons;
            // this does not alter USB or endpoint state.
            if (s_stableButtons != 0 || s_pendingButtons != 0)
            {
                Serial.printf(
                    "[NAPE SAFETY RELEASE] buttons=%02X age=%u\n",
                    s_stableButtons,
                    (unsigned)ageOf(0, now)
                );
                s_mouseButtons = 0;
                s_pendingButtons = 0;
                s_stableButtons = 0;
                s_pendingButtonSinceMs = now;
                updateGameButtons(0);
            }
            Serial.printf(
                "[NAPE WAKE PROBE] SLEEP SUSPECT START age=%u rx=%u armed=%u INT=%d\n",
                (unsigned)ageOf(0, now),
                (unsigned)s_rxCount[0],
                s_ifArmed[0] ? 1 : 0,
                intLevel
            );

            if (VERBOSE_DIAGNOSTICS)
            {
                const bool tuhReady =
                    tuh_hid_receive_ready(s_dev, 0);
                Serial.println("[NAPE ENDPOINT PROBE START]");
                Serial.printf("appArmed=%u\n", s_ifArmed[0] ? 1 : 0);
                Serial.printf("tuhReady=%u\n", tuhReady ? 1 : 0);
                Serial.printf("age=%u\n", (unsigned)ageOf(0, now));
                Serial.printf("rx0=%u\n", (unsigned)s_rxCount[0]);
                Serial.printf("INT=%d\n", intLevel);
                logHcdProbe(true);
            }
        }

        if (
            s_if0SleepSuspect &&
            now - s_lastSuspectLogMs >= SUSPECT_LOG_INTERVAL_MS
        )
        {
            s_lastSuspectLogMs = now;
            if (VERBOSE_DIAGNOSTICS)
            {
                const bool tuhReady =
                    tuh_hid_receive_ready(s_dev, 0);
                Serial.println("[NAPE ENDPOINT PROBE]");
                Serial.printf("age=%u\n", (unsigned)ageOf(0, now));
                Serial.printf("rx0=%u\n", (unsigned)s_rxCount[0]);
                Serial.printf("appArmed=%u\n", s_ifArmed[0] ? 1 : 0);
                Serial.printf("tuhReady=%u\n", tuhReady ? 1 : 0);
                Serial.printf("INT=%d\n", intLevel);
                Serial.printf("USBLOOP=%u\n", (unsigned)s_usbTaskLoops);
                logHcdProbe(false);
            }
        }

        // ----------------------------------------------------
        // Confirmed wake-stall recovery
        //
        // Silence alone is normal and never triggers this path.  After the
        // endpoint has entered the sleep-suspect state, a sustained active-low
        // MAX3421E interrupt without an IF0 callback proves that fresh USB work
        // is pending but the old Interrupt-IN transfer did not complete.
        // ----------------------------------------------------
        if (s_if0RecoveryState == If0RecoveryState::IDLE)
        {
            if (s_if0SleepSuspect && intLevel == LOW)
            {
                if (!s_if0WasStale)
                {
                    s_if0WasStale = true;
                    s_if0StaleStartedMs = now;
                    // A bouncing INT can begin many short confirmation
                    // windows. Keep recovery behavior unchanged, but avoid
                    // serial bursts that stall the render loop.
                    if (now - s_lastWakeStallLogMs >= 5000)
                    {
                        s_lastWakeStallLogMs = now;
                        Serial.printf("[NAPE WAKE STALL] INT LOW start age=%ums armed=%u\n",
                                      (unsigned)ageOf(0, now),
                                      s_ifArmed[0] ? 1 : 0);
                    }
                }
                else if (
                    now - s_if0StaleStartedMs >= IF0_RECOVERY_TRIGGER_MS &&
                    now - s_lastRecoveryAttemptMs >= HOST_RESET_COOLDOWN_MS
                )
                {
                    s_lastRecoveryAttemptMs = now;
                    ++s_hostResetAttempts;
                    clearNapeRuntimeState(now);
                    s_if0RecoveryState = If0RecoveryState::HOST_OFF_WAIT;
                    s_if0RecoveryStateSinceMs = now;
                    s_mountMaskAtResetStart = s_mountMask;

                    Serial.printf("[NAPE HOST RESET] START try=%u age=%ums INT=%d mount=0x%02X\n",
                                  (unsigned)s_hostResetAttempts,
                                  (unsigned)ageOf(0, now), intLevel,
                                  (unsigned)s_mountMaskAtResetStart);

                    const bool deinitOk = tuh_deinit(0);
                    if (deinitOk) ++s_hostDeinitOk;
                    else ++s_hostDeinitFail;
                    clearUsbRuntimeState();
                    Serial.printf("[NAPE HOST RESET] DEINIT=%s wait=%ums\n",
                                  deinitOk ? "OK" : "FAIL",
                                  (unsigned)HOST_RESET_OFF_MS);
                }
            }
            else if (s_if0WasStale)
            {
                // INT returned high before the confirmation window: it was a
                // transient edge, so leave the host untouched.
                s_if0WasStale = false;
                s_if0StaleStartedMs = 0;
            }
        }
        else if (s_if0RecoveryState == If0RecoveryState::HOST_OFF_WAIT)
        {
            if (now - s_if0RecoveryStateSinceMs >= HOST_RESET_OFF_MS)
            {
                const bool initOk = tuh_init(0);
                if (initOk) ++s_hostInitOk;
                else ++s_hostInitFail;
                s_if0RecoveryState = initOk
                    ? If0RecoveryState::WAIT_ENUM
                    : If0RecoveryState::COOLDOWN;
                s_if0RecoveryStateSinceMs = now;
                Serial.printf("[NAPE HOST RESET] REINIT=%s -> %s\n",
                              initOk ? "OK" : "FAIL", recoveryStateName());
            }
        }
        else if (
            s_if0RecoveryState == If0RecoveryState::WAIT_ENUM ||
            s_if0RecoveryState == If0RecoveryState::WAIT_IF0
        )
        {
            if (s_ready && s_if0RecoveryState == If0RecoveryState::WAIT_ENUM)
            {
                s_if0RecoveryState = If0RecoveryState::WAIT_IF0;
                s_if0RecoveryStateSinceMs = now;
                Serial.println("[NAPE HOST RESET] ENUM OK; WAIT IF0");
            }
            else if (now - s_if0RecoveryStateSinceMs >= HOST_RESET_ENUM_TIMEOUT_MS)
            {
                ++s_hostResetTimeout;
                s_if0RecoveryState = If0RecoveryState::COOLDOWN;
                s_if0RecoveryStateSinceMs = now;
                Serial.println("[NAPE HOST RESET] TIMEOUT -> COOLDOWN");
            }
        }
        else if (s_if0RecoveryState == If0RecoveryState::COOLDOWN)
        {
            if (now - s_if0RecoveryStateSinceMs >= HOST_RESET_COOLDOWN_MS)
            {
                s_if0RecoveryState = If0RecoveryState::IDLE;
                s_if0RecoveryStateSinceMs = 0;
                s_if0WasStale = false;
                s_if0StaleStartedMs = 0;
                s_if0SleepSuspect = false;
                Serial.println("[NAPE HOST RESET] COOLDOWN COMPLETE");
            }
        }


        // ----------------------------------------------------
        // Long-run Health
        //
        // 2秒周期にしてSerial負荷も少し下げる
        // ----------------------------------------------------

        if (
            now - lastHealthMs >=
            HEALTH_LOG_INTERVAL_MS
        )
        {
            lastHealthMs = now;

            Serial.println();
            Serial.println(
                "---------- NAPE LONG-RUN HEALTH ----------"
            );

            Serial.printf(
                "uptime=%lus USBLOOP=%u "
                "ready=%u mount=0x%02X "
                "dev=%u INT=%d\n",
                (unsigned long)(now / 1000),
                (unsigned)s_usbTaskLoops,
                s_ready ? 1 : 0,
                s_mountMask,
                s_dev,
                digitalRead(NAPE_INT_PIN)
            );

            Serial.printf(
                "RX total=%u "
                "IF0=%u IF1=%u IF2=%u IF3=%u IF4=%u\n",
                (unsigned)s_totalRx,
                (unsigned)s_rxCount[0],
                (unsigned)s_rxCount[1],
                (unsigned)s_rxCount[2],
                (unsigned)s_rxCount[3],
                (unsigned)s_rxCount[4]
            );

            Serial.printf(
                "AGE IF0=%ums IF1=%ums IF2=%ums "
                "IF3=%ums IF4=%ums ANY=%ums\n",
                (unsigned)ageOf(0, now),
                (unsigned)ageOf(1, now),
                (unsigned)ageOf(2, now),
                (unsigned)ageOf(3, now),
                (unsigned)ageOf(4, now),
                s_lastAnyRxMs
                    ? (unsigned)(
                        now - s_lastAnyRxMs
                      )
                    : 0
            );

            Serial.printf(
                "ARM IF0=%u IF1=%u IF2=%u "
                "IF3=%u IF4=%u\n",
                s_ifArmed[0] ? 1 : 0,
                s_ifArmed[1] ? 1 : 0,
                s_ifArmed[2] ? 1 : 0,
                s_ifArmed[3] ? 1 : 0,
                s_ifArmed[4] ? 1 : 0
            );

            Serial.printf(
                "motion=%u btn=%u "
                "rearmFail=%u recovered=%u\n",
                (unsigned)s_motionPackets,
                (unsigned)s_buttonTransitions,
                (unsigned)s_rearmFail,
                (unsigned)s_rearmRecovered
            );

            Serial.printf(
                "ACTIVITY IF0 age=%ums window=%ums\n",
                s_lastIf0ActivityMs
                    ? (unsigned)(now - s_lastIf0ActivityMs)
                    : 0,
                (unsigned)IF0_ACTIVITY_WINDOW_MS
            );

            Serial.printf(
                "HOSTRST state=%s try=%u deinitOK=%u deinitFAIL=%u "
                "initOK=%u initFAIL=%u recovered=%u timeout=%u\n",
                recoveryStateName(),
                (unsigned)s_hostResetAttempts,
                (unsigned)s_hostDeinitOk,
                (unsigned)s_hostDeinitFail,
                (unsigned)s_hostInitOk,
                (unsigned)s_hostInitFail,
                (unsigned)s_hostResetRecovered,
                (unsigned)s_hostResetTimeout
            );

            Serial.printf(
                "heap=%u minHeap=%u "
                "psram=%u\n",
                (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMinFreeHeap(),
                (unsigned)ESP.getFreePsram()
            );

            Serial.println(
                "------------------------------------------"
            );
        }


        // USB taskが他処理を占有しないようにする
        vTaskDelay(
            pdMS_TO_TICKS(1)
        );
    }
}


// ============================================================
// Public API
// ============================================================

bool nape_game_source_begin(void)
{
    Serial.println();
    Serial.println(
        "========================================"
    );
    Serial.println(
        "NAPETUNE NAPE INPUT v7.12.6"
    );
    Serial.println(
        "BATTERY MENUS / RICH BEAM"
    );
    Serial.println(
        "========================================"
    );

    game_input_clear_nape();

    s_mountMask = 0;
    s_ready = false;
    s_dev = 0;

    s_totalRx = 0;
    s_lastAnyRxMs = 0;

    s_rearmFail = 0;
    s_rearmRecovered = 0;

    s_motionPackets = 0;
    s_buttonTransitions = 0;
    s_if0MouseReportId = 0;
    s_if0ReportLayoutLogged = false;
    s_if2WheelPlusDown = false;
    s_if2WheelMinusDown = false;
    s_if2FlareKeyDown = false;
    s_if2ConfigKeyMask = 0;
    s_lastConfigKeyTick = 0;
    s_runToggle = false;
    s_auxGameButtons = 0;

    s_mouseButtons = 0;
    s_pendingButtons = 0;
    s_stableButtons = 0;
    s_pendingButtonSinceMs = 0;

    s_usbTaskLoops = 0;
    s_if0SleepSuspect = false;
    s_lastSuspectLogMs = 0;
    s_lastIntLevel = -1;
    s_lastObservedIf0Armed = false;

    s_if0WasStale = false;
    s_if0StaleStartedMs = 0;
    s_lastStaleNoticeMs = 0;
    s_lastIf0ActivityMs = 0;

    s_hostResetAttempts = 0;
    s_hostDeinitOk = 0;
    s_hostDeinitFail = 0;
    s_hostInitOk = 0;
    s_hostInitFail = 0;
    s_hostResetRecovered = 0;
    s_hostResetTimeout = 0;
    s_mountMaskAtResetStart = 0;

    s_if0RecoveryState =
        If0RecoveryState::IDLE;
    s_if0RecoveryStateSinceMs = 0;
    s_lastRecoveryAttemptMs = 0;

    for (
        uint8_t i = 0;
        i < MAX_IF;
        ++i
    )
    {
        s_ifArmed[i] = false;
        s_rxCount[i] = 0;
        s_lastRxMs[i] = 0;
        s_lastRetryMs[i] = 0;
    }


    // Boot mouse protocol contains only buttons/X/Y. Report protocol keeps
    // the Nape wheel byte, while preserving the same interrupt-IN path.
    tuh_hid_set_default_protocol(HID_PROTOCOL_REPORT);

    if (!s_usbHost.begin(0))
    {
        Serial.println(
            "[NAPE GAME] USB MODULE INIT ERROR"
        );

        return false;
    }

    Serial.println(
        "[NAPE GAME] USB MODULE INIT OK"
    );


    BaseType_t rc =
        xTaskCreatePinnedToCore(
            napeUsbTask,
            "NapeUSB",
            6144,
            nullptr,
            1,
            nullptr,
            1
        );


    if (rc != pdPASS)
    {
        Serial.println(
            "[NAPE GAME] USB task create FAILED"
        );

        return false;
    }

    Serial.println(
        "[NAPE GAME] USB task created"
    );

    return true;
}


bool nape_game_source_ready(void)
{
    return s_ready;
}

uint32_t nape_game_source_motion_rx_count(void)
{
    return s_rxCount[0];
}

uint32_t nape_game_source_motion_age_ms(void)
{
    const uint32_t last = s_lastRxMs[0];
    return last ? millis() - last : UINT32_MAX;
}

uint8_t nape_game_source_get_key_action(uint8_t index)
{
    return index < 4 ? s_keyActions[index] : NAPE_ACTION_OFF;
}

void nape_game_source_set_key_action(uint8_t index, uint8_t action)
{
    if (index >= 4 || action > NAPE_ACTION_FLARE) return;
    s_keyActions[index] = action;
}

uint8_t nape_game_source_get_mouse_action(uint8_t index)
{
    return index < 2 ? s_mouseActions[index] : NAPE_ACTION_OFF;
}

void nape_game_source_set_mouse_action(uint8_t index, uint8_t action)
{
    if (index >= 2 || action > NAPE_ACTION_FLARE) return;
    s_mouseActions[index] = action;
    updateGameButtons(s_stableButtons);
}

int8_t nape_game_source_get_orientation(void)
{
    return s_napeOrientation;
}

void nape_game_source_request_orientation(void)
{
    s_napeOrientation = -1;
    s_orientationRequest = true;
    s_orientationQueryInFlight = false;
    s_lastOrientationQueryAttemptMs = millis() - ORIENTATION_QUERY_RETRY_MS;
    s_orientationQueryAttempts = 0;
}

void nape_game_source_request_layer(uint8_t layer)
{
    if (layer > 7) return;
    // Give SET_LAYER exclusive ownership of the config lane. Without this,
    // a periodic polling/full-sync request can remain in flight and consume
    // the response window; the UI then reaches its 4.5 s confirmation timeout
    // and appears to jump back to the old layer.
    s_tuningSyncRequested = false;
    s_tuningDpiRefreshRequested = false;
    s_tuningPollingRefreshRequested = false;
    s_tuningSyncing = false;
    s_tuningQuery = TuningQuery::NONE;
    s_tuningQueryInFlight = false;
    s_requestedLayer = layer;
    s_layerAttempts = 0;
    s_lastLayerAttemptMs = millis() - LAYER_SET_RETRY_MS;
    s_layerRequest = true;
    Serial.printf("[NAPE LAYER] queued layer=%u\n", layer);
}

bool nape_game_source_run_enabled(void)
{
    return s_runToggle;
}

void nape_tuning_request_sync(void)
{
    // Tuning Mode owns IF3 while its read-only snapshot is running. Drop only
    // stale pending transactions left by the launcher/game transition; the
    // already-applied runtime layer and all normal input paths are untouched.
    s_layerRequest = false;
    // A3 current-layer replies are zero-filled after the layer byte, so they
    // cannot refresh ANGLE. Read A7/20 first; serviceTuningQuery waits until
    // this short transaction finishes before starting the full snapshot.
    s_napeOrientation = -1;
    s_orientationRequest = true;
    s_orientationQueryInFlight = false;
    s_lastOrientationQueryAttemptMs = millis() - ORIENTATION_QUERY_RETRY_MS;
    s_orientationQueryAttempts = 0;
    s_tuningTxBlockReason = 0;
    s_tuningSyncRequested = true;
}

void nape_tuning_request_polling_rate(void)
{
    s_tuningPollingRefreshRequested = true;
}

bool nape_tuning_write_key(uint8_t layer, uint8_t key, uint16_t keycode)
{
    if (layer > 7 || key > 6 || !s_dev || s_tuningConfigIf >= MAX_IF)
        return false;
    s_tuningSyncRequested = false;
    s_tuningDpiRefreshRequested = false;
    s_tuningPollingRefreshRequested = false;
    s_tuningSyncing = false;
    s_tuningQuery = TuningQuery::NONE;
    s_tuningQueryInFlight = false;
    s_keyWriteLayer = layer;
    s_keyWriteKey = key;
    s_keyWriteCode = keycode;
    s_keyWriteIsWheel = false;
    s_keyWriteStatus = 1;
    s_keyWritePending = true;
    s_keyWriteInFlight = false;
    return true;
}

bool nape_tuning_write_wheel(uint8_t layer, uint8_t slot, uint16_t keycode)
{
    if (layer > 7 || slot > 1 || !s_dev || s_tuningConfigIf >= MAX_IF ||
        s_keyWritePending || s_keyWriteInFlight)
        return false;
    s_tuningSyncRequested = false;
    s_tuningSyncing = false;
    s_tuningQuery = TuningQuery::NONE;
    s_tuningQueryInFlight = false;
    s_keyWriteLayer = layer;
    s_keyWriteKey = slot;
    s_keyWriteCode = keycode;
    s_keyWriteIsWheel = true;
    s_keyWriteStatus = 1;
    s_keyWritePending = true;
    s_keyWriteInFlight = false;
    return true;
}

void nape_tuning_get_snapshot(NapeTuningSnapshot* out)
{
    if (!out) return;
    portENTER_CRITICAL(&s_tuningMux);
    out->connected = s_dev != 0 && (s_mountMask & (1u << 3));
    out->syncing = s_tuningSyncing;
    out->runtime_valid = s_tuningRuntimeValid;
    out->current_layer = s_tuningLayer;
    out->angle_index = s_tuningAngleIndex;
    out->dpi_stage = s_tuningDpiStage;
    out->dpi = s_tuningDpi;
    out->custom_dpi_valid = s_tuningCustomDpiValid;
    out->custom_dpi = s_tuningCustomDpi;
    out->polling_rate_valid = s_tuningPollingRateValid;
    out->polling_rate_hz = s_tuningPollingRateHz;
    out->dpi_cycle_valid_mask = s_tuningDpiCycleValidMask;
    memcpy(out->dpi_cycle, s_tuningDpiCycle, sizeof(s_tuningDpiCycle));
    out->keymap_valid_mask = s_tuningKeymapValidMask;
    memcpy(out->keymap, s_tuningKeymap, sizeof(s_tuningKeymap));
    memcpy(out->tap_hold_valid, s_tuningTapHoldValid,
           sizeof(s_tuningTapHoldValid));
    memcpy(out->tap_key, s_tuningTapKey, sizeof(s_tuningTapKey));
    memcpy(out->hold_key, s_tuningHoldKey, sizeof(s_tuningHoldKey));
    memcpy(out->wheel_valid, s_tuningWheelValid, sizeof(s_tuningWheelValid));
    memcpy(out->wheel, s_tuningWheel, sizeof(s_tuningWheel));
    out->mount_mask = s_mountMask;
    out->config_if = s_tuningConfigIf;
    out->tx_block_reason = s_tuningTxBlockReason;
    out->query_kind = (uint8_t)s_tuningQuery;
    out->query_layer = s_tuningQueryLayer;
    out->last_report_len = s_tuningLastReportLen;
    out->if3_tx_count = s_tuningIf3TxCount;
    out->if3_rx_count = s_tuningIf3RxCount;
    out->timeout_count = s_tuningTimeoutCount;
    out->last_report_age_ms = s_tuningLastReportMs
        ? millis() - s_tuningLastReportMs : UINT32_MAX;
    memcpy(out->last_report, s_tuningLastReport, sizeof(s_tuningLastReport));
    out->key_write_status = s_keyWriteStatus;
    portEXIT_CRITICAL(&s_tuningMux);
}

// ============================================================
// TinyUSB callbacks
// ============================================================

void tuh_hid_mount_cb(
    uint8_t devAddr,
    uint8_t instance,
    uint8_t const* descReport,
    uint16_t descLen
)
{
    uint16_t vid = 0;
    uint16_t pid = 0;

    tuh_vid_pid_get(
        devAddr,
        &vid,
        &pid
    );


    Serial.printf(
        "[NAPE MOUNT] DEV=%u IF=%u "
        "VID:PID=%04X:%04X DESC=%u\n",
        devAddr,
        instance,
        vid,
        pid,
        descLen
    );


    if (
        vid != NAPE_DONGLE_VID ||
        pid != NAPE_DONGLE_PID
    )
    {
        return;
    }


    s_dev = devAddr;


    if (instance < MAX_IF)
    {
        if (descReport && descLen)
        {
            tuh_hid_report_info_t reports[8] = {};
            const uint8_t count = tuh_hid_parse_report_descriptor(
                reports, 8, descReport, descLen
            );
            for (uint8_t i = 0; i < count; ++i)
            {
                Serial.printf(
                    "[NAPE HID DESC] IF%u reportId=%u page=%04X usage=%04X\n",
                    instance,
                    reports[i].report_id,
                    reports[i].usage_page,
                    reports[i].usage
                );
                if (reports[i].usage_page == 0xFF60 &&
                    reports[i].usage == 0x0061)
                {
                    s_tuningConfigIf = instance;
                    Serial.printf("[NAPE TUNING] CONFIG IF DETECTED=%u\n",
                                  (unsigned)instance);
                }
                if (
                    instance == 0 &&
                    reports[i].usage_page == 0x01 &&
                    reports[i].usage == 0x02
                )
                {
                    s_if0MouseReportId = reports[i].report_id;
                    break;
                }
            }
            if (instance == 0)
            {
                Serial.printf(
                    "[NAPE MOUSE] reportProtocol=1 reportId=%u\n",
                    s_if0MouseReportId
                );
            }
        }

        s_mountMask |=
            (uint8_t)(
                1u << instance
            );

        s_ifArmed[instance] =
            false;


        const bool ok =
            submitReceive(
                instance
            );


        Serial.printf(
            "[NAPE MOUNT] RX START IF%u=%s "
            "mask=%02X\n",
            instance,
            ok ? "OK" : "FAIL",
            s_mountMask
        );
    }


    if (
        (s_mountMask & 0x1F) ==
        0x1F
    )
    {
        if (!s_ready)
        {
            s_ready = true;

            Serial.println(
                "[NAPE GAME] DONGLE ONLINE"
            );
        }
    }


    (void)descReport;
}


void tuh_hid_umount_cb(
    uint8_t devAddr,
    uint8_t instance
)
{
    if (devAddr != s_dev)
    {
        return;
    }


    Serial.printf(
        "[NAPE UNMOUNT] DEV=%u IF=%u "
        "maskBefore=%02X\n",
        devAddr,
        instance,
        s_mountMask
    );


    if (instance < MAX_IF)
    {
        s_mountMask &=
            (uint8_t)~(
                1u << instance
            );

        s_ifArmed[instance] =
            false;

        s_lastRetryMs[instance] =
            0;

        if (instance == 0)
        {
            s_if0SleepSuspect = false;
            s_lastSuspectLogMs = 0;
        }
        else if (instance == 2)
        {
            s_if2WheelPlusDown = false;
            s_if2WheelMinusDown = false;
            s_if2FlareKeyDown = false;
            s_if2ConfigKeyMask = 0;
            s_lastConfigKeyTick = 0;
            s_runToggle = false;
            s_auxGameButtons = 0;
            updateGameButtons(s_stableButtons);
        }
    }


    s_ready =
        false;

    s_mouseButtons = 0;
    s_pendingButtons = 0;
    s_stableButtons = 0;
    s_pendingButtonSinceMs = 0;

    // A physical detach during normal operation returns to IDLE.
    // During a deliberate HOST RESET, however, unmount callbacks are
    // expected and must NOT cancel the reset state machine.
    if (s_if0RecoveryState == If0RecoveryState::IDLE)
    {
        s_if0RecoveryState =
            If0RecoveryState::IDLE;
        s_if0RecoveryStateSinceMs = 0;
    }

    game_input_clear_nape();


    Serial.printf(
        "[NAPE UNMOUNT] maskAfter=%02X\n",
        s_mountMask
    );


    if (s_mountMask == 0)
    {
        s_dev = 0;
    }
}


// ============================================================
// HID receive
// ============================================================

void tuh_hid_report_received_cb(
    uint8_t devAddr,
    uint8_t instance,
    uint8_t const* report,
    uint16_t len
)
{
    if (devAddr != s_dev)
    {
        return;
    }


    const uint32_t now =
        millis();
    const bool explicitCurrentLayerResponse =
        instance == s_tuningConfigIf && report && len >= 2 &&
        report[0] == 0xA3 && s_tuningSyncing &&
        s_tuningQuery == TuningQuery::CURRENT_LAYER;

    if (instance == s_tuningConfigIf && report && len > 0)
    {
        portENTER_CRITICAL(&s_tuningMux);
        ++s_tuningIf3RxCount;
        s_tuningLastReportMs = now;
        s_tuningLastReportLen = len;
        const uint16_t copyLen = len < sizeof(s_tuningLastReport)
            ? len : sizeof(s_tuningLastReport);
        memcpy(s_tuningLastReport, report, copyLen);
        if (copyLen < sizeof(s_tuningLastReport))
            memset(s_tuningLastReport + copyLen, 0,
                   sizeof(s_tuningLastReport) - copyLen);
        portEXIT_CRITICAL(&s_tuningMux);

        Serial.printf("[NAPE TUNING RX] n=%u len=%u raw=",
                      (unsigned)s_tuningIf3RxCount, (unsigned)len);
        const uint16_t dumpLen = len < 32 ? len : 32;
        for (uint16_t i = 0; i < dumpLen; ++i)
            Serial.printf("%02X%s", report[i], i + 1 == dumpLen ? "\n" : " ");
    }

    const bool wakeProbeHit =
        instance == 0 && s_if0SleepSuspect;
    const uint32_t ageBefore =
        wakeProbeHit ? ageOf(0, now) : 0;
    const bool armedBefore =
        wakeProbeHit ? s_ifArmed[0] : false;

    // Nape can route its two wheel directions through IF2 as configurable
    // keyboard usages. Resolve each report against the WHEEL+/WHEEL- values
    // read from the active layer, preserving direction instead of forcing
    // every detent to +1. F8 (HID usage 0x41) is reserved for FLARE.
    if (instance == 2 && report && len == 9 && report[0] == 0x01)
    {
        bool wheelPlusDown = false;
        bool wheelMinusDown = false;
        bool flareKeyDown = false;
        bool configKeyDown[4] = {};
        const uint8_t activeLayer = s_currentLayer >= 0 && s_currentLayer < 8
            ? (uint8_t)s_currentLayer : 7;
        const bool wheelPairValid =
            (s_tuningWheelValid[activeLayer] & 0x03) == 0x03 &&
            (s_tuningWheel[activeLayer][0] & 0xFF00) == 0 &&
            (s_tuningWheel[activeLayer][1] & 0xFF00) == 0 &&
            s_tuningWheel[activeLayer][0] != s_tuningWheel[activeLayer][1];
        const uint8_t wheelPlusUsage = wheelPairValid
            ? (uint8_t)(s_tuningWheel[activeLayer][0] & 0xFF) : 0x00;
        const uint8_t wheelMinusUsage = wheelPairValid
            ? (uint8_t)(s_tuningWheel[activeLayer][1] & 0xFF) : 0x00;
        for (uint8_t i = 3; i < 9; ++i)
        {
            const uint8_t key = report[i];
            // Layer 7 game defaults: F6=next, F7=previous. Recognize these
            // directly even before a full Tuning sync has completed. B keeps
            // backward compatibility with the former one-way assignment.
            if (key == 0x3F || key == 0x05 ||
                (key != 0 && key == wheelPlusUsage)) wheelPlusDown = true;
            if (key == 0x40 ||
                (key != 0 && key == wheelMinusUsage)) wheelMinusDown = true;
            if (key == 0x41) flareKeyDown = true;
            else if (key >= 0x42 && key <= 0x45)
                configKeyDown[key - 0x42] = true;
        }

        uint8_t configKeyMask = 0;
        for (uint8_t i = 0; i < 4; ++i)
            if (configKeyDown[i]) configKeyMask |= (uint8_t)(1u << i);
        if (configKeyMask || s_if2ConfigKeyMask)
            s_lastConfigKeyTick = (uint16_t)now;
        if (wheelPlusDown && !s_if2WheelPlusDown)
        {
            if (!configKeyChordActive(now))
            {
                if (core2_config_is_active()) core2_config_nape_wheel(1);
                else game_input_add_nape_wheel(1);
                if (VERBOSE_DIAGNOSTICS)
                    Serial.printf("[NAPE WHEEL] IF2 key=%02X step=1\n",
                                  wheelPlusUsage);
            }
        }
        if (wheelMinusDown && !s_if2WheelMinusDown)
        {
            if (!configKeyChordActive(now))
            {
                if (core2_config_is_active()) core2_config_nape_wheel(-1);
                else game_input_add_nape_wheel(-1);
                if (VERBOSE_DIAGNOSTICS)
                    Serial.printf("[NAPE WHEEL] IF2 key=%02X step=-1\n",
                                  wheelMinusUsage);
            }
        }
        s_if2WheelPlusDown = wheelPlusDown;
        s_if2WheelMinusDown = wheelMinusDown;
        s_if2FlareKeyDown = flareKeyDown;

        for (uint8_t i = 0; i < 4; ++i)
        {
            if (
                s_keyActions[i] == NAPE_ACTION_RUN_TOGGLE &&
                configKeyDown[i] && !(s_if2ConfigKeyMask & (1u << i))
            )
            {
                s_runToggle = !s_runToggle;
                Serial.printf("[NAPE GAME KEY] RUN toggle=%u\n", s_runToggle ? 1 : 0);
            }
            if (
                s_keyActions[i] == NAPE_ACTION_WEAPON_NEXT &&
                configKeyDown[i] && !(s_if2ConfigKeyMask & (1u << i))
            )
                game_input_add_nape_wheel(1);
        }

        uint32_t aux = 0;
        if (s_runToggle) aux |= GAME_INPUT_RUN;
        if (s_if2FlareKeyDown) aux |= GAME_INPUT_FLARE;
        for (uint8_t i = 0; i < 4; ++i)
        {
            if (!configKeyDown[i]) continue;
            aux |= actionToButton(s_keyActions[i]);
        }

        if (aux != s_auxGameButtons)
        {
            s_auxGameButtons = aux;
            updateGameButtons(s_stableButtons);
        }

        s_if2ConfigKeyMask = configKeyMask;
    }

    if (instance == 3 && report && len >= 2)
    {
        // Nape's asynchronous state monitor confirms the actual layer. Some
        // firmware revisions instead echo SET_LAYER, so accept either form.
        if (report[0] == 0xA3 && len >= 3)
        {
            s_currentLayer = report[1] >= 1 && report[1] <= 8
                ? (int8_t)((report[1] - 1) & 0x07)
                : s_currentLayer;
            const uint8_t newDpiStage = len >= 4 ? report[3] : 0xFF;
            portENTER_CRITICAL(&s_tuningMux);
            s_tuningLayer = (uint8_t)s_currentLayer;
            // The explicit A3 query response contains the selected layer, but
            // its remaining zero-filled bytes are not an angle/DPI snapshot.
            if (!explicitCurrentLayerResponse)
            {
                s_tuningAngleIndex = report[2] & 0x07;
                if (newDpiStage <= 4)
                {
                    s_tuningDpiStage = newDpiStage;
                    if (s_tuningDpiCycleValidMask & (1u << newDpiStage))
                        s_tuningDpi = s_tuningDpiCycle[newDpiStage];
                    // Keep the last known live value until A7 0D/A7 23
                    // supplies the new one. Clearing here made the HUD show
                    // "----" immediately after a DPI-cycle key press.
                }
            }
            s_tuningRuntimeValid = true;
            portEXIT_CRITICAL(&s_tuningMux);
            // A normal A3 state notification already selects the cached DPI
            // value for the new stage above. Do not immediately issue A7/24
            // and A7/0D follow-up requests here: some NapePro firmware emits
            // another A3 notification for those config transactions, creating
            // a refresh loop (rapid SYNC/flicker and apparently wild values).
            // A7/23 remains the authoritative asynchronous live-DPI update,
            // while an explicit/full SYNC refreshes the complete cycle table.
            if (s_layerRequest && s_currentLayer == (int8_t)s_requestedLayer)
            {
                s_layerRequest = false;
                Serial.printf("[NAPE LAYER] active=%u confirmed=STATE\n",
                              (unsigned)s_currentLayer);
            }
            if (explicitCurrentLayerResponse)
            {
                Serial.printf("[NAPE TUNING] current layer=%u read by A3\n",
                              (unsigned)s_currentLayer);
                advanceTuningQuery();
            }
        }
        else if (report[0] == 0xA7 && report[1] == 0x2D)
        {
            // The response is A7 2D 00; byte 2 is a status byte, not a layer
            // number. The selected layer is the request target. This command
            // changes the active layer, but persistence has not been proven.
            const uint8_t confirmedLayer = s_requestedLayer;
            s_currentLayer = (int8_t)confirmedLayer;
            portENTER_CRITICAL(&s_tuningMux);
            s_tuningLayer = confirmedLayer;
            s_tuningRuntimeValid = true;
            portEXIT_CRITICAL(&s_tuningMux);
            s_layerRequest = false;
            Serial.printf("[NAPE LAYER] active=%u confirmed=SET\n",
                          (unsigned)s_currentLayer);
        }
    }

    // The A7/0D response is a configuration bundle, not a live runtime
    // snapshot. In particular, bytes 3..4 commonly contain the stored/default
    // value 1000, so copying them into s_tuningDpi makes a correctly observed
    // live DPI jump back to 1000 at the next periodic sync. Byte 6 is likewise
    // not proven to be the current polling-rate stage. Use this response only
    // to complete the query until a focused capture identifies the actual live
    // polling field/report. Live DPI remains sourced from A7/23 below.
    if (report && len >= 7 && report[0] == 0xA7 && report[1] == 0x0D)
    {
        if (s_tuningSyncing && s_tuningQuery == TuningQuery::POLLING)
            advanceTuningQuery();
    }

    const bool dpiConfigReply = report && len >= 2 &&
        report[0] == 0xA7 &&
        (report[1] == 0x21 || report[1] == 0x24 || report[1] == 0x36);
    // DPI configuration replies can return on a sibling raw-HID IN endpoint
    // even though their SET_REPORT request was sent through the config IF.
    // Accept these exact read-only signatures globally; keep every other
    // config response restricted to the discovered config interface.
    if (((instance == s_tuningConfigIf || instance == 3) || dpiConfigReply) &&
        report && len >= 3)
    {
        if (!explicitCurrentLayerResponse &&
            report[0] == 0xA3 && report[1] >= 1 && report[1] <= 8)
        {
            portENTER_CRITICAL(&s_tuningMux);
            s_tuningLayer = (report[1] - 1) & 0x07;
            s_tuningAngleIndex = report[2] & 0x07;
            s_tuningDpiStage = report[3];
            s_tuningRuntimeValid = true;
            portEXIT_CRITICAL(&s_tuningMux);
        }
        // Single-entry read-back after a key write.
        else if (len >= 6 && report[0] == 0x12 && report[3] == 0x02 &&
                 s_tuningSyncing &&
                 s_tuningQuery == TuningQuery::KEY_VERIFY)
        {
            const uint16_t offset =
                ((uint16_t)report[1] << 8) | report[2];
            const uint16_t expectedOffset =
                0x0E * (s_keyWriteLayer + 1) + s_keyWriteKey * 2;
            if (offset == expectedOffset)
            {
                const uint16_t actual =
                    ((uint16_t)report[4] << 8) | report[5];
                portENTER_CRITICAL(&s_tuningMux);
                s_tuningKeymap[s_keyWriteLayer][s_keyWriteKey] = actual;
                s_tuningKeymapValidMask |=
                    (uint8_t)(1u << s_keyWriteLayer);
                s_keyWriteStatus = actual == s_keyWriteCode ? 3 : 4;
                portEXIT_CRITICAL(&s_tuningMux);
                Serial.printf("[NAPE KEY VERIFY] expected=%04X actual=%04X %s\n",
                              s_keyWriteCode, actual,
                              actual == s_keyWriteCode ? "SAVED" : "FAILED");
                finishTuningSync();
            }
        }
        // VIA dynamic keymap response: seven 16-bit assignments per layer.
        else if (len >= 18 && report[0] == 0x12 && report[1] == 0x00 &&
            report[3] == 0x0E)
        {
            const uint8_t offset = report[2];
            if (offset >= 0x0E && offset <= 0x70 && offset % 0x0E == 0)
            {
                const uint8_t layer = (offset / 0x0E) - 1;
                portENTER_CRITICAL(&s_tuningMux);
                for (uint8_t key = 0; key < 7; ++key)
                {
                    const uint8_t p = 4 + key * 2;
                    s_tuningKeymap[layer][key] =
                        ((uint16_t)report[p] << 8) | report[p + 1];
                }
                s_tuningKeymapValidMask |= (uint8_t)(1u << layer);
                portEXIT_CRITICAL(&s_tuningMux);
                if (s_tuningSyncing &&
                    s_tuningQuery == TuningQuery::KEYMAP &&
                    layer == s_tuningQueryLayer)
                    advanceTuningQuery();
            }
        }
        else if (len >= 6 && report[0] == 0x14 &&
                 report[1] >= 1 && report[1] <= 8 && report[3] <= 1)
        {
            const uint8_t layer = report[1] - 1;
            const uint8_t slot = report[3];
            portENTER_CRITICAL(&s_tuningMux);
            // Wheel assignments use the same 16-bit big-endian keycodes as
            // the dynamic keymap. Keeping only report[5] turned 0x7E25 and
            // 0x7E26 (NEXT/PREV DPI) into the ordinary HID keys 8 and 9.
            s_tuningWheel[layer][slot] =
                ((uint16_t)report[4] << 8) | report[5];
            s_tuningWheelValid[layer] |= (uint8_t)(1u << slot);
            portEXIT_CRITICAL(&s_tuningMux);
            if (s_keyWriteStatus == 2 && s_keyWriteIsWheel &&
                layer == s_keyWriteLayer && slot == s_keyWriteKey)
            {
                s_keyWriteStatus =
                    s_tuningWheel[layer][slot] == s_keyWriteCode ? 3 : 4;
                finishTuningSync();
            }
            if (s_tuningSyncing && layer == s_tuningQueryLayer &&
                ((s_tuningQuery == TuningQuery::WHEEL_UP && slot == 0) ||
                 (s_tuningQuery == TuningQuery::WHEEL_DOWN && slot == 1)))
                advanceTuningQuery();
        }
        else if (len >= 9 && report[0] == 0xA7 && report[1] == 0x26 &&
                 report[2] >= 1 && report[2] <= 8 &&
                 report[3] == 0x00 && report[4] < 7)
        {
            const uint8_t layer = report[2] - 1;
            const uint8_t key = report[4];
            const uint16_t tap =
                (uint16_t)report[5] | ((uint16_t)report[6] << 8);
            const uint16_t hold =
                (uint16_t)report[7] | ((uint16_t)report[8] << 8);
            portENTER_CRITICAL(&s_tuningMux);
            s_tuningTapKey[layer][key] = tap;
            s_tuningHoldKey[layer][key] = hold;
            s_tuningTapHoldValid[layer] |= (uint8_t)(1u << key);
            portEXIT_CRITICAL(&s_tuningMux);
            if (s_tuningSyncing && s_tuningQuery == TuningQuery::TAP_HOLD &&
                layer == s_tuningQueryLayer && key == s_tuningQueryKey)
                advanceTuningQuery();
        }
        else if (len >= 3 && report[0] == 0xA7 && report[1] == 0x21)
        {
            // The captured response stores the active cycle in byte 2. The
            // remaining report bytes are zero-padded and must not be treated
            // as a leading status/alternate stage field.
            const uint8_t stage = report[2];
            if (stage <= 4) s_tuningDpiStage = stage;
            if (s_tuningSyncing && s_tuningQuery == TuningQuery::STAGE)
                advanceTuningQuery();
        }
        else if (len >= 4 && report[0] == 0xA7 && report[1] == 0x24)
        {
            // A7/24 exists in both [lo hi] and [stage lo hi] layouts.
            uint16_t dpi = 0;
            if (len >= 5 && report[2] <= 4)
                dpi = (uint16_t)report[3] | ((uint16_t)report[4] << 8);
            if (dpi < 50 || dpi > 30000)
                dpi = (uint16_t)report[2] | ((uint16_t)report[3] << 8);
            if (s_tuningSyncing && s_tuningQuery == TuningQuery::DPI &&
                s_tuningQueryLayer <= 4)
            {
                if (dpi >= 50 && dpi <= 30000)
                {
                    portENTER_CRITICAL(&s_tuningMux);
                    s_tuningDpiCycle[s_tuningQueryLayer] = dpi;
                    s_tuningDpiCycleValidMask |=
                        (uint8_t)(1u << s_tuningQueryLayer);
                    if (s_tuningDpiStage == s_tuningQueryLayer)
                        s_tuningDpi = dpi;
                    portEXIT_CRITICAL(&s_tuningMux);
                    advanceTuningQuery();
                }
                // A zero response is common while the device's config lane
                // is still starting. Keep the query in flight so the normal
                // timeout path retries it instead of accepting zero.
            }
        }
        else if (len >= 4 && report[0] == 0xA7 && report[1] == 0x36)
        {
            // A7/36 likewise may include a leading status byte.
            uint16_t dpi = 0;
            if (len >= 5 && report[2] <= 4)
                dpi = (uint16_t)report[3] | ((uint16_t)report[4] << 8);
            if (dpi < 50 || dpi > 30000)
                dpi = (uint16_t)report[2] | ((uint16_t)report[3] << 8);
            if (dpi >= 50 && dpi <= 30000)
            {
                portENTER_CRITICAL(&s_tuningMux);
                s_tuningCustomDpi = dpi;
                s_tuningCustomDpiValid = true;
                portEXIT_CRITICAL(&s_tuningMux);
            }
            if (s_tuningSyncing &&
                s_tuningQuery == TuningQuery::CUSTOM_DPI)
                advanceTuningQuery();
        }
        else if (len >= 5 && report[0] == 0xA7 && report[1] == 0x23)
        {
            const uint16_t dpi =
                (uint16_t)report[3] | ((uint16_t)report[4] << 8);
            if (dpi > 0)
            {
                portENTER_CRITICAL(&s_tuningMux);
                s_tuningDpi = dpi;
                if (s_tuningDpiStage <= 4)
                {
                    s_tuningDpiCycle[s_tuningDpiStage] = dpi;
                    s_tuningDpiCycleValidMask |=
                        (uint8_t)(1u << s_tuningDpiStage);
                }
                portEXIT_CRITICAL(&s_tuningMux);
            }
        }
    }

    if (instance == 3 && report && len >= 3 && s_orientationRequest)
    {
        Serial.printf("[NAPE ORI RESPONSE] len=%u raw=", len);
        const uint16_t dumpLen = len < 16 ? len : 16;
        for (uint16_t i = 0; i < dumpLen; ++i)
            Serial.printf("%02X%s", report[i], i + 1 < dumpLen ? " " : "");
        Serial.println();

        if (report[0] == 0xA7 && report[1] == 0x20)
        {
            const uint8_t angleIndex = report[2] & 0x07;
            s_napeOrientation = angleIndex;
            portENTER_CRITICAL(&s_tuningMux);
            s_tuningAngleIndex = angleIndex;
            s_tuningRuntimeValid = true;
            portEXIT_CRITICAL(&s_tuningMux);
            s_orientationRequest = false;
            s_orientationQueryInFlight = false;
            Serial.printf("[NAPE TUNING] ANGLE refreshed=%u (%u deg)\n",
                          (unsigned)angleIndex,
                          (unsigned)angleIndex * 45u);
        }
    }

    if (wakeProbeHit)
    {
        const uint8_t raw0 = (report && len > 0) ? report[0] : 0;
        const uint8_t raw1 = (report && len > 1) ? report[1] : 0;
        const uint8_t raw2 = (report && len > 2) ? report[2] : 0;

        Serial.println(
            "[NAPE WAKE PROBE] IF0 CALLBACK AFTER SLEEP"
        );
        Serial.printf("t=%u\n", (unsigned)now);
        Serial.printf("len=%u\n", (unsigned)len);
        Serial.printf(
            "raw=%02X %02X %02X\n",
            raw0,
            raw1,
            raw2
        );
        Serial.printf(
            "ageBefore=%u\narmedBefore=%u\nINT=%d\n",
            (unsigned)ageBefore,
            armedBefore ? 1 : 0,
            digitalRead(NAPE_INT_PIN)
        );
    }


    ++s_totalRx;

    s_lastAnyRxMs =
        now;


    if (instance < MAX_IF)
    {
        ++s_rxCount[instance];

        s_lastRxMs[instance] =
            now;

        // このcallbackを発生させたtransferは完了
        s_ifArmed[instance] =
            false;
    }

    if (wakeProbeHit)
    {
        Serial.printf(
            "[NAPE WAKE PROBE] WAKE SUCCESS rx0=%u\n",
            (unsigned)s_rxCount[0]
        );
        s_if0SleepSuspect = false;
        s_lastSuspectLogMs = 0;
    }


    // --------------------------------------------------------
    // HOST RESET recovery proof
    //
    // Do this independently from s_if0WasStale because host deinit may
    // cause unmount callbacks which clear ordinary stale bookkeeping.
    // --------------------------------------------------------

    if (
        instance == 0 &&
        (s_if0RecoveryState == If0RecoveryState::WAIT_ENUM ||
         s_if0RecoveryState == If0RecoveryState::WAIT_IF0 ||
         s_if0RecoveryState == If0RecoveryState::COOLDOWN)
    )
    {
        ++s_hostResetRecovered;

        Serial.printf(
            "[NAPE HOST RESET] FIRST IF0 REPORT "
            "rx=%u -> RECOVERED count=%u\n",
            (unsigned)s_rxCount[0],
            (unsigned)s_hostResetRecovered
        );

        s_if0RecoveryState =
            If0RecoveryState::IDLE;
        s_if0RecoveryStateSinceMs = 0;

        s_if0WasStale = false;
        s_if0StaleStartedMs = 0;
        s_lastStaleNoticeMs = 0;
    }


    // --------------------------------------------------------
    // 長時間staleから自然復帰した場合
    // --------------------------------------------------------

    if (
        instance == 0 &&
        s_if0WasStale
    )
    {
        const uint32_t staleDuration =
            now - s_if0StaleStartedMs;


        Serial.printf(
            "[NAPE RESUME] "
            "IF0 resumed automatically after %ums "
            "rx=%u INT=%d\n",
            (unsigned)staleDuration,
            (unsigned)s_rxCount[0],
            digitalRead(NAPE_INT_PIN)
        );


        s_if0WasStale =
            false;

        s_if0StaleStartedMs =
            0;

        s_lastStaleNoticeMs =
            0;

        // After a HOST RESET, a real IF0 report is the final proof
        // that enumeration + interrupt IN traffic are both alive again.
        if (s_if0RecoveryState != If0RecoveryState::IDLE)
        {
            ++s_hostResetRecovered;

            Serial.printf(
                "[NAPE HOST RESET] FIRST IF0 REPORT "
                "-> RECOVERED count=%u\n",
                (unsigned)s_hostResetRecovered
            );
        }

        s_if0RecoveryState =
            If0RecoveryState::IDLE;
        s_if0RecoveryStateSinceMs = 0;
    }


    // --------------------------------------------------------
    // IF0 mouse report
    //
    // [buttons][dx][dy]
    // --------------------------------------------------------

    if (
        instance == 0 &&
        report &&
        len >= 3
    )
    {
        const uint8_t offset =
            (s_if0MouseReportId != 0 && report[0] == s_if0MouseReportId)
                ? 1 : 0;

        if (len < (uint16_t)(offset + 3))
            goto rearm_report;

        const uint8_t buttons =
            report[offset + 0];

        // A real IF0 report proves that the endpoint is alive.
        // Arm the short recovery observation window from this activity.
        s_lastIf0ActivityMs = now;

        const uint16_t payloadLen = len - offset;
        int16_t dx = 0;
        int16_t dy = 0;
        int8_t wheel = 0;
        int8_t horizontalWheel = 0;

        // Nape Report Protocol uses a high-resolution mouse body:
        //   buttons, X(LE16), Y(LE16), vertical wheel, horizontal wheel
        // Boot Protocol used the old 3-byte buttons/X8/Y8 body.
        if (payloadLen >= 6)
        {
            dx = (int16_t)(
                (uint16_t)report[offset + 1] |
                ((uint16_t)report[offset + 2] << 8)
            );
            dy = (int16_t)(
                (uint16_t)report[offset + 3] |
                ((uint16_t)report[offset + 4] << 8)
            );
            wheel = (int8_t)report[offset + 5];
            if (payloadLen >= 7)
                horizontalWheel = (int8_t)report[offset + 6];
            // The Nape dial can arrive on the horizontal-scroll axis. Accept
            // it only in a motion-free packet so ball + 01-04 chords cannot
            // accidentally cycle weapons.
            if (wheel == 0 && horizontalWheel != 0 &&
                abs((int)dx) <= 2 && abs((int)dy) <= 2)
                wheel = horizontalWheel;
        }
        else
        {
            dx = (int8_t)report[offset + 1];
            dy = (int8_t)report[offset + 2];
            wheel = (payloadLen >= 4)
                ? (int8_t)report[offset + 3]
                : 0;
        }

        if (!s_if0ReportLayoutLogged)
        {
            s_if0ReportLayoutLogged = true;
            Serial.printf(
                "[NAPE MOUSE REPORT] len=%u payload=%u id=%u "
                "raw=%02X %02X %02X %02X %02X %02X %02X %02X\n",
                (unsigned)len,
                (unsigned)payloadLen,
                (unsigned)s_if0MouseReportId,
                len > 0 ? report[0] : 0,
                len > 1 ? report[1] : 0,
                len > 2 ? report[2] : 0,
                len > 3 ? report[3] : 0,
                len > 4 ? report[4] : 0,
                len > 5 ? report[5] : 0,
                len > 6 ? report[6] : 0,
                len > 7 ? report[7] : 0
            );
        }


        if (buttons != s_mouseButtons)
        {
            s_mouseButtons = buttons;
            if (core2_glass2_dialogue_active())
            {
                core2_glass2_nape_buttons(buttons);
                s_pendingButtons = 0;
                s_stableButtons = 0;
                updateGameButtons(0);
            }
            else if (core2_config_is_active())
            {
                core2_config_nape_buttons(buttons);
                s_pendingButtons = 0;
                s_stableButtons = 0;
                updateGameButtons(0);
            }
            else
            {
                s_pendingButtons = buttons;
                s_pendingButtonSinceMs = now;
            }

            if (VERBOSE_DIAGNOSTICS)
            {
                Serial.printf(
                    "[NAPE BTN RAW] %02X\n",
                    buttons
                );
            }
        }


        if (
            dx != 0 ||
            dy != 0
        )
        {
            ++s_motionPackets;


            game_input_add_nape_motion(
                dx,
                dy
            );

            // v0.7.6b proof-of-path: rate-limited confirmation that
            // IF0 motion is actually forwarded to the PrBoom bridge.
            static uint32_t s_lastBridgeProofMs = 0;
            if (VERBOSE_DIAGNOSTICS && now - s_lastBridgeProofMs >= 1000)
            {
                s_lastBridgeProofMs = now;
                Serial.printf(
                    "[NAPE BRIDGE] IF0 -> GAME INPUT dx=%d dy=%d\n",
                    (int)dx,
                    (int)dy
                );
            }
        }

        if (wheel != 0)
        {
            if (!configKeyChordActive(now))
            {
                if (core2_config_is_active()) core2_config_nape_wheel(wheel);
                else game_input_add_nape_wheel(wheel);
                if (VERBOSE_DIAGNOSTICS)
                    Serial.printf("[NAPE WHEEL] IF0 steps=%d\n", (int)wheel);
            }
        }
    }


    // --------------------------------------------------------
    // 受信完了後に1回だけrearm
    // --------------------------------------------------------

rearm_report:
    if (instance < MAX_IF)
    {
        if (!submitReceive(instance))
        {
            Serial.printf(
                "[NAPE REARM FAIL] "
                "DEV=%u IF=%u count=%u\n",
                devAddr,
                instance,
                (unsigned)s_rearmFail
            );
        }
    }
}


// ============================================================
// Unused output callbacks
// ============================================================

void tuh_hid_set_report_complete_cb(
    uint8_t devAddr,
    uint8_t instance,
    uint8_t,
    uint8_t,
    uint16_t len
)
{
    if (devAddr == s_dev && instance == s_tuningConfigIf)
    {
        Serial.printf("[NAPE TUNING SET COMPLETE] len=%u\n", (unsigned)len);

        // MAX3421/TinyUSB can leave the Raw-HID interrupt IN transfer idle
        // after the interface control transfer. Mark it unarmed and submit a
        // fresh receive immediately; the device keeps the 32-byte response
        // pending on endpoint 0x83 until the host polls it.
        s_ifArmed[instance] = false;
        const bool armed = submitReceive(instance);
        Serial.printf("[NAPE TUNING RX REARM] IF%u=%s\n",
                      (unsigned)instance, armed ? "OK" : "DEFER");

        if (s_keyWriteInFlight)
        {
            s_keyWriteInFlight = false;
            s_keyWriteStatus = 2;
            s_tuningSyncing = true;
            s_tuningQuery = s_keyWriteIsWheel
                ? (s_keyWriteKey == 0 ? TuningQuery::WHEEL_UP
                                      : TuningQuery::WHEEL_DOWN)
                : TuningQuery::KEY_VERIFY;
            s_tuningQueryLayer = s_keyWriteLayer;
            s_tuningQueryInFlight = false;
            s_tuningRetryCount = 0;
            Serial.printf("[NAPE %s WRITE] SET complete; verify queued\n",
                          s_keyWriteIsWheel ? "WHEEL" : "KEY");
        }
    }
}


void tuh_hid_report_sent_cb(
    uint8_t,
    uint8_t,
    uint8_t const*,
    uint16_t
)
{
}
