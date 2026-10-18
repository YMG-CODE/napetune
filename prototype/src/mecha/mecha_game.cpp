// Software logic: MIT. Embedded creative content: see CONTENT_LICENSE.md and CONTENT_MANIFEST.json.
#include <Arduino.h>
#include <M5Unified.h>
#include <SD.h>
#include <esp_system.h>
#include <string.h>

#include "core2_bridge.h"
#include "game_input.h"
#include "napetune_save.h"
#include "spi_guard.h"
#include "mecha/mecha_game.h"
#include "mecha/mecha_hybrid_sprite.h"
#include "mecha/mecha_hybrid_flight_sprite.h"
#include "progression/game_progress.h"

extern const uint8_t demo_prologue_qr_start[] asm("_binary_src_assets_demo_prologue_qr_132x132_png_start");
extern const uint8_t demo_prologue_qr_end[] asm("_binary_src_assets_demo_prologue_qr_132x132_png_end");
extern const uint8_t demo_terminalis_kv_start[] asm("_binary_src_assets_demo_terminalis_kv_320x240_png_start");
extern const uint8_t demo_terminalis_kv_end[] asm("_binary_src_assets_demo_terminalis_kv_320x240_png_end");
extern const uint8_t iz_comms_face_start[] asm("_binary_src_assets_iz_comms_face_152x157_png_start");
extern const uint8_t iz_comms_face_end[] asm("_binary_src_assets_iz_comms_face_152x157_png_end");

namespace
{
constexpr int W = 256;
constexpr int H = 192;
constexpr int VIEW_X = 32;
constexpr int VIEW_Y = 24;
constexpr int CX = W / 2;
constexpr int HORIZON_Y = 90;
constexpr float PROJECTION = 116.0f;
constexpr float AUTO_ROUTE_CAMERA = 0.28f;
constexpr float MIN_ALTITUDE_M = 120.0f;
constexpr float MAX_ALTITUDE_M = 6000.0f;
constexpr float ALTITUDE_TO_WORLD = 0.0025f;
constexpr float GROUND_LOCK_MAX_ALTITUDE_M = 2200.0f;
constexpr float AIR_LOCK_MIN_ALTITUDE_M = 1700.0f;
constexpr float TARGET_LOCK_MAX_RANGE = 38.0f;
constexpr float FLIGHT_FORM_HUD_SPEED_THRESHOLD = 160.0f;
constexpr uint16_t HUD_BG = 0x00E0;
constexpr uint32_t REAR_MISSILE_APPROACH_MS = 5200;
constexpr uint32_t REAR_MISSILE_ESCAPE_MS = 1100;
constexpr uint32_t REAR_MISSILE_COOLDOWN_MS = 15000;
constexpr uint32_t FLARE_COOLDOWN_MS = 2100;
// Exhibition tuning: keep collision feedback, but temporarily disable the
// finite-hull system. Set true when final HP balancing begins.
constexpr bool PLAYER_HULL_ENABLED = false;

M5Canvas s_frame(&M5.Display);
M5Canvas s_hybridNormalCanvas(&s_frame);
M5Canvas s_hybridFlightCanvas(&s_frame);
M5Canvas s_bossCanvas(&s_frame);
bool s_frameReady = false;
bool s_hybridCanvasesReady = false;
bool s_bossCanvasReady = false;
constexpr int SIN_LUT_SIZE = 256;
float s_sinLut[SIN_LUT_SIZE] = {};
bool s_sinLutReady = false;
bool s_boostActive = false;
bool s_cyanThrusterEquipped = false;
GameLoadout s_loadout = {};
bool s_demoMode = false;
bool s_storyMode = true;
bool s_showcaseMode = false;
bool s_showcaseTouchTracking = false;
int s_showcaseTouchStartX = 0;
int s_showcaseTouchStartY = 0;
int s_showcaseTouchLastX = 0;
int s_showcaseTouchLastY = 0;
volatile uint8_t s_modelCaptureRequest = 0;
bool s_modelCaptureClean = false;
bool s_hybridRenderEnabled = true;
bool s_autoPilotEnabled = true;
bool s_autoPilotWasDriving = false;
bool s_autoPilotFireDown = false;
bool s_autoPilotUseDown = false;
int8_t s_autoPilotRollDirection = 1;
uint32_t s_autoPilotManualUntilMs = 0;
uint32_t s_autoPilotLastWeaponMs = 0;
uint32_t s_autoPilotDemoStartMs = 0;
int s_autoPilotSavedThrottle = 100;
uint32_t s_gameStartedMs = 0;
bool s_introCommsTriggered = false;
bool s_demoGuideVisible = false;
bool s_demoGuideDismissed = false;
enum class DemoStoryPhase : uint8_t {
    COMBAT, UNLOCK_CINEMATIC, UNLOCK_GUIDE, IMPULSE_REVEAL, ESCAPE,
    FAIL_GRACE, SUCCESS_COMMS, SUCCESS, FAILED, KV_CARD, END_CARD
};
DemoStoryPhase s_demoStoryPhase = DemoStoryPhase::COMBAT;
uint8_t s_demoKills = 0;
uint32_t s_demoPhaseStartedMs = 0;
uint8_t s_demoCinematicPage = 255;
uint8_t s_demoLastCountdown = 255;
float s_demoExitTravel = 0.0f;
bool s_demoRevealFlareLaunched = false;
bool s_demoRevealMissileAlerted = false;
uint32_t s_demoRevealTransformStartMs = 0;
uint8_t s_demoKvLastFadeStep = 255;
constexpr uint8_t DEMO_KILLS_TO_UNLOCK = 40;
constexpr uint32_t DEMO_ESCAPE_MS = 30000;
constexpr float DEMO_EXIT_DISTANCE = 760.0f;
constexpr uint32_t EXIT_HOLD_MS = 1500;
constexpr int EXIT_HOTSPOT_MAX_X = 64;
constexpr int EXIT_HOTSPOT_MAX_Y = 49;
bool s_exitHoldTracking = false;
uint32_t s_exitHoldStartedMs = 0;
bool s_exitConfirmVisible = false;
bool s_exitConfirmArmed = false;
bool s_exitConfirmTouchWasDown = false;
uint8_t s_autoPilotThrottleStage = 255;
uint32_t s_weaponBannerUntilMs = 0;
float s_shipX = 0.0f, s_shipY = 0.0f;
float s_shipVx = 0.0f, s_shipVy = 0.0f;
float s_travel = 0.0f;

constexpr char STORY_SAVE_PACK[] = "MECHA_STORY";
constexpr uint16_t STORY_SAVE_CHAPTER = 1;
constexpr float STORY_CHECKPOINTS[] = {
    0.0f, 30.0f, 65.0f, 90.0f, 126.0f, 161.0f, 196.0f, 220.0f
};
constexpr uint16_t STORY_CHECKPOINT_COUNT =
    sizeof(STORY_CHECKPOINTS) / sizeof(STORY_CHECKPOINTS[0]);
float s_cameraX = 0.0f, s_cameraY = 0.0f;
float s_cameraBank = 0.0f, s_cameraPitch = 0.0f;
float s_routeBank = 0.0f, s_routePitch = 0.0f;
float s_routeLeadX = 0.0f, s_routeLeadY = 0.0f;
float s_courseBank = 0.0f, s_coursePitch = 0.0f;
float s_courseLoopSin = 0.0f, s_courseLoopCos = 1.0f;
// Stabilized chase-camera contribution used only by aerobatic actions.
// The craft still performs the full roll; the world follows by a restrained
// amount and eases back to level so the HUD remains a stable flight reference.
float s_actionCameraRoll = 0.0f;
int s_courseHeadingOffset = 0;
const char* s_courseSection = "STRAIGHT";
float s_fovScale = 1.0f, s_boostKick = 0.0f, s_boostVisual = 0.0f;
float s_barrelSin = 0.0f, s_barrelCos = 1.0f, s_rollAngleDeg = 0.0f;
float s_rollThrust = 0.0f;
float s_transformBlend = 0.0f; // 0=humanoid combat, 1=high-speed flight
float s_poseYaw = 0.0f, s_posePitch = 0.0f;
float s_forwardLean = 0.0f;
uint32_t s_rollStartMs = 0, s_rollCooldownUntilMs = 0;
int s_rollDirection = 1;
bool s_rollActive = false;
bool s_useHoldPending = false, s_manualLoopActive = false;
uint32_t s_useHoldStartMs = 0, s_manualLoopStartMs = 0;
uint32_t s_manualLoopCooldownUntilMs = 0;
float s_loopBaseShipY = 0.0f, s_loopBaseAltitudeM = 1200.0f;
int s_loopDirection = 1; // +1=upward, -1=downward
bool s_routeGuideEnabled = false;
bool s_tunnelRunActive = false;
constexpr bool TUNNEL_TEST_ENABLED = false;
bool s_flightHudEnabled = true;
float s_hudSpeed = 120.0f, s_hudAltitude = 1200.0f;
float s_altitudeM = 1200.0f;
float s_throttlePercent = 100.0f;
float s_effectiveThrottlePercent = 100.0f;
float s_currentSpeed = 12.0f;
uint32_t s_lastExternalHudMs = 0;
bool s_externalHudDirty = true;
float s_freeAimX = CX, s_freeAimY = HORIZON_Y;
float s_altitudeControlAuthority = 0.0f;
float s_verticalInputEnvelope = 0.0f;
int s_lockedTarget = -1, s_lockedTargetX = CX, s_lockedTargetY = HORIZON_Y;
bool s_lockedTargetGround = false;
float s_lockedTargetRange = 0.0f;
bool s_lockedTargetHitEligible = false;
int s_lockedAimErrorPx = 999;
int s_lockCandidate = -1, s_lockCandidateX = CX, s_lockCandidateY = HORIZON_Y;
bool s_lockCandidateGround = false;
float s_lockCandidateRange = 0.0f;
uint8_t s_weapon = 0;
int s_missileAmmo = 8, s_beamAmmo = 3;
uint32_t s_weaponEffectStartMs = 0, s_lastWeaponShotMs = 0;
enum class BeamState : uint8_t { IDLE, CHARGING, READY, FIRING, COOLDOWN };
BeamState s_beamState = BeamState::IDLE;
uint32_t s_beamStateSinceMs = 0, s_lastBeamDamageMs = 0;
uint32_t s_beamMessageUntilMs = 0;
uint32_t s_playerHitConfirmUntilMs = 0;
int s_playerHitConfirmX = CX, s_playerHitConfirmY = HORIZON_Y;
int s_playerHitConfirmDamage = 0, s_playerHitConfirmTarget = -1;
bool s_bladeCharging = false;
bool s_bladeDualMode = false;
bool s_bladeSwingCharged = false;
bool s_bladeSwingDual = false;
uint8_t s_bladeSwingStage = 0;
uint8_t s_bladeComboStep = 0;
uint32_t s_bladeChargeStartMs = 0;
uint32_t s_bladeDualUntilMs = 0;
uint32_t s_bladeSwingStartMs = 0;
uint32_t s_bladeLastReleaseMs = 0;
int16_t s_bladeDashTargetX = -1;
int16_t s_bladeDashTargetY = HORIZON_Y;
constexpr uint32_t BEAM_CHARGE_MS = 750;
constexpr uint32_t BEAM_FIRE_MS = 2400;
constexpr uint32_t BEAM_COOLDOWN_MS = 450;
constexpr uint32_t BEAM_IGNITION_MS = 140;
constexpr uint32_t MISSILE_FLIGHT_MS = 760;
constexpr uint32_t MISSILE_EFFECT_MS = 1460;
constexpr uint32_t MISSILE_BLAST_MS = MISSILE_EFFECT_MS - MISSILE_FLIGHT_MS;
constexpr uint32_t CANNON_EFFECT_MS = 340;
constexpr uint32_t BLADE_CHARGE_MS = 450;
constexpr uint32_t BLADE_DUAL_DEPLOY_MS = 700;
constexpr uint32_t BLADE_DUAL_DURATION_MS = 8000;
constexpr uint32_t BLADE_SWING_MS = 220;
constexpr uint32_t BLADE_CHARGED_SWING_MS = 310;
constexpr uint32_t BLADE_QUICK_SWING_MS = 150;
constexpr uint32_t BLADE_COMBO_RESET_MS = 620;
constexpr float BLADE_COMBO_SPEED_SCALE = 0.30f;
constexpr int MISSION_TARGET = 2;
constexpr int INTERCEPTOR_TARGET = 4;
constexpr int TARGET_COUNT = 9;
constexpr int NORMAL_TARGET_HP = 4;
constexpr int BOSS_TARGET_HP = 28;
int s_missionTargetHp = BOSS_TARGET_HP;
bool s_missionComplete = false;
int s_targetHp[TARGET_COUNT] = { 4, 4, BOSS_TARGET_HP, 4, 4, 4, 4, 4, 4 };
uint32_t s_targetDestroyedUntilMs[TARGET_COUNT] = {};
bool s_targetBeamEligible[TARGET_COUNT] = {};
int s_targetScreenX[TARGET_COUNT] = {};
int s_targetScreenY[TARGET_COUNT] = {};
float s_targetScreenRange[TARGET_COUNT] = {};
bool s_targetScreenGround[TARGET_COUNT] = {};
bool s_targetLockEligible[TARGET_COUNT] = {};
int s_enemyExplosionTarget = -1;
int s_enemyExplosionX = CX, s_enemyExplosionY = HORIZON_Y;
bool s_enemyExplosionGround = false;
uint32_t s_enemyExplosionStartMs = 0;
bool s_missileDamageApplied = false;
int s_missileTarget = -1;
bool s_missileFireLatched = false;
struct MissileBlast
{
    uint32_t startMs;
    int x;
    int y;
    bool heavy;
};
MissileBlast s_missileBlasts[8] = {};
uint8_t s_nextMissileBlast = 0;
constexpr int MULTI_LOCK_MAX = 6;
// Twelve missiles form one full volley; six spare slots allow the next
// release to begin while the tail of the previous volley is resolving.
constexpr int MULTI_SHOT_MAX = MULTI_LOCK_MAX * 3;
constexpr float MULTI_MISSILE_RANGE = 42.0f;
constexpr uint32_t MULTI_LOCK_ARM_MS = 220;
constexpr uint32_t MULTI_MISSILE_FLIGHT_MS = 880;
bool s_multiLockCharging = false;
uint32_t s_multiLockStartMs = 0;
bool s_multiLocked[TARGET_COUNT] = {};
uint8_t s_multiLockCount = 0;
struct MultiMissileShot
{
    uint32_t startMs;
    int8_t target;
    int8_t side;
    uint8_t ordinal;
    int impactX;
    int impactY;
    bool damageApplied;
};
MultiMissileShot s_multiShots[MULTI_SHOT_MAX] = {};
uint8_t s_nextMultiShot = 0;
bool s_interceptorActive = false;
bool s_interceptorWarned = false;
float s_interceptorX = 0.0f, s_interceptorY = 0.0f, s_interceptorZ = 34.0f;
enum class InterceptorPattern : uint8_t { DIRECT, OFFSET, FEINT };
InterceptorPattern s_interceptorPattern = InterceptorPattern::DIRECT;
float s_interceptorClosure = 10.0f;
uint32_t s_nextInterceptorMs = 0;
bool s_rearMissileActive = false;
uint32_t s_rearMissileHitStartMs = 0;
uint32_t s_nextRearMissileMs = 0;
uint32_t s_rearMissileElapsedMs = 0;
uint32_t s_rearMissileEscapeMs = 0;
uint32_t s_flareCooldownUntilMs = 0;
uint32_t s_flareVisualUntilMs = 0;
uint32_t s_flareLungeStartMs = 0;
uint32_t s_flareInterceptUntilMs = 0;
uint32_t s_flareInterceptAtMs = 0;
bool s_flareInterceptPending = false;
bool s_flareHomingBurst = false;
int s_flareOriginX = CX;
int s_flareOriginY = 151;
int s_flareTargetX = CX;
int s_flareTargetY = 170;
int s_flareDetonationX = CX;
int s_flareDetonationY = 151;
bool s_rearMissileHudDrawn = false;
bool s_enemyMissileHudDrawn = false;
bool s_bossBattleActive = false;
enum class BossMovePhase : uint8_t {
    APPROACH, HOVER, CHARGE_WINDUP, CHARGE, EXIT, WAVE, ATTACK_PASS, RETURN
};
BossMovePhase s_bossMovePhase = BossMovePhase::APPROACH;
uint32_t s_bossPhaseStartMs = 0;
bool s_bossEncounterStarted = false;
int8_t s_bossExitSide = 1;
float s_bossLateral = 0.0f;
float s_bossAltitudeM = 1650.0f;
float s_bossRange = 44.0f;
float s_bossVerticalOffset = 0.0f;
uint32_t s_bossCollisionCooldownUntilMs = 0;
uint32_t s_bossNextChargeMs = 0;
float s_bossChargeLineX = 0.0f;
float s_bossChargeLineAltitude = 1650.0f;
uint32_t s_collisionShakeUntilMs = 0, s_collisionSlowUntilMs = 0;
uint32_t s_missileCutStartMs = 0;
int16_t s_missileCutX = CX, s_missileCutY = HORIZON_Y;
uint8_t s_missileCutCount = 0;
uint32_t s_terrainCollisionCooldownUntilMs = 0;
uint32_t s_routeEnemyCollisionCooldownUntilMs = 0;
int s_playerHull = 3;
enum class EnemyShotKind : uint8_t { CANNON, HOMING_MISSILE, DUMB_MISSILE };
struct EnemyShot
{
    uint32_t startMs;
    uint16_t durationMs;
    int16_t startX, startY;
    int16_t aimX, aimY;
    EnemyShotKind kind;
};
constexpr int ENEMY_SHOT_MAX = 7;
EnemyShot s_enemyShots[ENEMY_SHOT_MAX] = {};
uint8_t s_nextEnemyShot = 0;
uint32_t s_enemyNextFireMs[TARGET_COUNT] = {};
uint32_t s_enemyQuietUntilMs = 0;
uint8_t s_enemyVolleyShots = 0;
uint32_t s_lastHitRejectLogMs = 0;
int s_cursorSwitchCandidate = -1;
uint32_t s_cursorSwitchSinceMs = 0;

bool enemyShotIsMissile(EnemyShotKind kind)
{
    return kind == EnemyShotKind::HOMING_MISSILE ||
           kind == EnemyShotKind::DUMB_MISSILE;
}

bool enemyMissileThreatActive()
{
    const uint32_t now = millis();
    for (const EnemyShot& shot : s_enemyShots)
        if (shot.startMs != 0 && enemyShotIsMissile(shot.kind) &&
            now - shot.startMs < shot.durationMs) return true;
    return false;
}

constexpr float CANNON_RANGE = 15.0f;
constexpr float MISSILE_RANGE = 36.0f;
constexpr float BEAM_RANGE = 52.0f;
constexpr float BLADE_RANGE = 7.5f;
constexpr float BLADE_DASH_RANGE = 14.0f;

float currentWeaponRange()
{
    return s_weapon == 0 ? CANNON_RANGE
         : s_weapon == 1 ? MISSILE_RANGE
         : s_weapon == 2 ? BEAM_RANGE
         : s_weapon == 3 ? MULTI_MISSILE_RANGE
         : s_weapon == 4 ? BLADE_RANGE : 0.0f;
}

bool bossDamageWindowOpen(uint32_t now)
{
    if (!s_bossEncounterStarted || s_missionComplete) return false;
    const uint32_t age = now - s_bossPhaseStartMs;
    switch (s_bossMovePhase)
    {
        // The rival exposes its core after settling into hover, then closes its
        // guard before the next maneuver. This is the main readable opening.
        case BossMovePhase::HOVER:         return age >= 500 && age < 4300;
        // Early windup is a short risk/reward opening before the ram commits.
        case BossMovePhase::CHARGE_WINDUP: return age < 540;
        // A successful evade is rewarded with a recovery opening.
        case BossMovePhase::RETURN:        return age >= 550;
        // The tail end of a high-speed pass can be punished by accurate fire.
        case BossMovePhase::ATTACK_PASS:   return age >= 750;
        default:                           return false;
    }
}

int currentWeaponCaptureRadius(float z, bool warningThreat)
{
    if (s_weapon == 1)
        return warningThreat ? 96 : (z < 14.0f ? 80 : 68);
    if (s_weapon == 2)
        return z < 14.0f ? 24 : 18;
    if (s_weapon == 3)
        return z < 14.0f ? 82 : 70;
    if (s_weapon == 4)
        return z < 8.0f ? 52 : 36;
    if (s_weapon == 5) return 0;
    return z < 12.0f ? 36 : 28;
}

struct WarpStar { float x, y, z; };
constexpr int STAR_COUNT = 50;
WarpStar s_stars[STAR_COUNT];
float s_starCombatDim = 0.0f;
bool s_previousBoost = false;
bool s_configWasActive = false;
uint32_t s_lastUpdateMs = 0, s_lastDrawMs = 0;
uint32_t s_fireUntilMs = 0;
uint32_t s_perfSinceMs = 0, s_perfFrames = 0, s_perfDrawUs = 0;
uint32_t s_perfPixels = 0;
uint32_t s_perfRenderUs = 0, s_perfTotalUs = 0;
uint32_t s_perfSceneUs = 0, s_perfShipUs = 0;
uint32_t s_perfMaxTotalUs = 0;
uint32_t s_perfMaxRenderUs = 0, s_perfMaxLcdUs = 0;

struct Point2 { int x; int y; };

void drawSegmentedReticle(int x, int y, uint16_t color)
{
    // Eight short octagonal arc segments read as a circle but avoid the
    // generic circle rasterizer. Cardinal gaps keep the target visible.
    constexpr int r = 8;
    constexpr int a = 3;
    constexpr int b = 6;
    s_frame.drawLine(x+a, y-r, x+b, y-b, color);
    s_frame.drawLine(x+b, y-b, x+r, y-a, color);
    s_frame.drawLine(x+r, y+a, x+b, y+b, color);
    s_frame.drawLine(x+b, y+b, x+a, y+r, color);
    s_frame.drawLine(x-a, y+r, x-b, y+b, color);
    s_frame.drawLine(x-b, y+b, x-r, y+a, color);
    s_frame.drawLine(x-r, y-a, x-b, y-b, color);
    s_frame.drawLine(x-b, y-b, x-a, y-r, color);
}

void drawWeaponReticle(int x, int y, uint8_t weapon, bool firing)
{
    const uint16_t base = (s_rearMissileActive || enemyMissileThreatActive()) ? RED
                        : weapon == 0 ? GREEN
                        : weapon == 1 ? ORANGE
                        : weapon == 2 ? CYAN
                        : weapon == 3 ? MAGENTA
                        : weapon == 4 ? 0x7DFF : YELLOW;
    const uint16_t color = firing ? WHITE : base;
    if (weapon == 0)
    {
        // Cannon: compact lead-computing cross with an open center.
        s_frame.drawFastHLine(x-15, y, 11, color);
        s_frame.drawFastHLine(x+5, y, 11, color);
        s_frame.drawFastVLine(x, y-9, 5, color);
        s_frame.drawFastVLine(x, y+5, 5, color);
        s_frame.drawPixel(x, y, YELLOW);
    }
    else if (weapon == 1)
    {
        // Missile: four inward-facing acquisition corners.
        constexpr int r = 12, a = 6;
        s_frame.drawLine(x-r,y-r,x-a,y-r,color); s_frame.drawLine(x-r,y-r,x-r,y-a,color);
        s_frame.drawLine(x+r,y-r,x+a,y-r,color); s_frame.drawLine(x+r,y-r,x+r,y-a,color);
        s_frame.drawLine(x-r,y+r,x-a,y+r,color); s_frame.drawLine(x-r,y+r,x-r,y+a,color);
        s_frame.drawLine(x+r,y+r,x+a,y+r,color); s_frame.drawLine(x+r,y+r,x+r,y+a,color);
    }
    else if (weapon == 2)
    {
        // Beam: circular energy aperture.
        drawSegmentedReticle(x, y, color);
        s_frame.drawPixel(x, y, WHITE);
    }
    else if (weapon == 3)
    {
        // Multi-lock: hexagonal scan cell plus three satellite pips.
        constexpr int r = 11, h = 6;
        s_frame.drawLine(x-r,y,x-h,y-r,color); s_frame.drawLine(x-h,y-r,x+h,y-r,color);
        s_frame.drawLine(x+h,y-r,x+r,y,color); s_frame.drawLine(x+r,y,x+h,y+r,color);
        s_frame.drawLine(x+h,y+r,x-h,y+r,color); s_frame.drawLine(x-h,y+r,x-r,y,color);
        s_frame.drawPixel(x-16,y-7,color); s_frame.drawPixel(x+16,y-7,color);
        s_frame.drawPixel(x,y+16,color);
    }
    else if (weapon == 4)
    {
        // Plasma blade: compact crossed edge guides around an open centre.
        s_frame.drawLine(x-12, y+11, x-3, y+2, color);
        s_frame.drawLine(x+12, y+11, x+3, y+2, color);
        s_frame.drawLine(x-9, y-8, x-3, y-2, color);
        s_frame.drawLine(x+9, y-8, x+3, y-2, color);
        s_frame.drawPixel(x, y, WHITE);
    }
    else
    {
        // Flare: an outward-facing decoy marker, distinct from attack sights.
        s_frame.drawLine(x-14, y-6, x-7, y, color);
        s_frame.drawLine(x-7, y, x-14, y+6, color);
        s_frame.drawLine(x+14, y-6, x+7, y, color);
        s_frame.drawLine(x+7, y, x+14, y+6, color);
        s_frame.drawPixel(x, y, WHITE);
    }
}

void drawBeamChargeReticle(int x, int y, uint32_t elapsed)
{
    const int lit = min(8, (int)(elapsed * 8 / BEAM_CHARGE_MS) + 1);
    constexpr int r = 13, a = 5, b = 10;
    const int segment[8][4] = {
        { a,-r, b,-b}, { b,-b, r,-a}, { r, a, b, b}, { b, b, a, r},
        {-a, r,-b, b}, {-b, b,-r, a}, {-r,-a,-b,-b}, {-b,-b,-a,-r}
    };
    for (int i = 0; i < 8; ++i)
    {
        const uint16_t c = i < lit ? (lit == 8 ? CYAN : ORANGE) : 0x4208;
        s_frame.drawLine(x + segment[i][0], y + segment[i][1],
                         x + segment[i][2], y + segment[i][3], c);
    }
}

void drawBeamChargeStatus(uint32_t now)
{
    constexpr int x = CX - 34, y = 3, w = 68, h = 12;
    if (s_beamState == BeamState::CHARGING || s_beamState == BeamState::READY)
    {
        const uint32_t elapsed = s_beamState == BeamState::READY
            ? BEAM_CHARGE_MS : min(BEAM_CHARGE_MS, now - s_beamStateSinceMs);
        const int fill = (int)(elapsed * (w - 2) / BEAM_CHARGE_MS);
        s_frame.fillRect(x, y, w, h, BLACK);
        s_frame.drawRect(x, y, w, h, s_beamState == BeamState::READY ? WHITE : ORANGE);
        s_frame.fillRect(x + 1, y + h - 3, fill, 2,
                         s_beamState == BeamState::READY ? CYAN : ORANGE);
        s_frame.setTextColor(s_beamState == BeamState::READY ? WHITE : ORANGE, BLACK);
        s_frame.setCursor(x + (s_beamState == BeamState::READY ? 12 : 18), y + 1);
        if (s_beamState == BeamState::READY)
            s_frame.print("RELEASE");
        else
            s_frame.printf("CHG %02lu", (unsigned long)(elapsed * 99 / BEAM_CHARGE_MS));
    }
    else if ((int32_t)(s_beamMessageUntilMs - now) > 0)
    {
        s_frame.fillRect(x, y, w, h, BLACK);
        s_frame.drawRect(x, y, w, h, RED);
        s_frame.setTextColor(ORANGE, BLACK);
        s_frame.setCursor(x + 10, y + 2);
        s_frame.print("CANCELLED");
    }
}

bool applyTargetDamage(int target, int damage, const char* weapon,
                       float range, int hitX, int hitY, bool ground)
{
    if (target < 0 || target >= TARGET_COUNT ||
        s_targetDestroyedUntilMs[target] != 0) return false;
    if (target == MISSION_TARGET && !bossDamageWindowOpen(millis()))
    {
        const uint32_t now = millis();
        if (!(s_autoPilotEnabled && s_autoPilotWasDriving) &&
            now - s_lastHitRejectLogMs >= 500)
        {
            s_lastHitRejectLogMs = now;
            Serial.printf("[MECHA SHOT BLOCKED] weapon=%s reason=BOSS_GUARD\n", weapon);
        }
        return false;
    }

    if (s_loadout.arms && strcmp(weapon, "COLLISION") != 0) ++damage;
    s_targetHp[target] = max(0, s_targetHp[target] - damage);
    if (target == MISSION_TARGET) s_missionTargetHp = s_targetHp[target];
    s_playerHitConfirmUntilMs = millis() + 460;
    s_playerHitConfirmX = hitX;
    s_playerHitConfirmY = hitY;
    s_playerHitConfirmDamage = damage;
    s_playerHitConfirmTarget = target;
    s_externalHudDirty = true;
    Serial.printf("[MECHA HIT] weapon=%s target=%d damage=%d hp=%d range=%.1f stable=1\n",
                  weapon, target, damage, s_targetHp[target], range);

    if (s_targetHp[target] == 0)
    {
        const uint32_t now = millis();
        s_enemyExplosionTarget = target;
        s_enemyExplosionX = hitX;
        s_enemyExplosionY = hitY;
        s_enemyExplosionGround = ground;
        s_enemyExplosionStartMs = now;
        s_targetDestroyedUntilMs[target] = target == MISSION_TARGET
            ? UINT32_MAX : now + 5000;
        if (target == INTERCEPTOR_TARGET)
        {
            s_interceptorActive = false;
            s_nextInterceptorMs = now + 4500;
        }
        if (target == MISSION_TARGET) s_missionComplete = true;
        if (s_storyMode && s_demoStoryPhase == DemoStoryPhase::COMBAT)
        {
            s_demoKills = min((int)DEMO_KILLS_TO_UNLOCK, (int)s_demoKills + 1);
            Serial.printf("[MECHA DEMO] DESTROYED %u/%u\n",
                          (unsigned)s_demoKills, (unsigned)DEMO_KILLS_TO_UNLOCK);
            if (s_demoKills >= DEMO_KILLS_TO_UNLOCK)
            {
                s_demoStoryPhase = DemoStoryPhase::UNLOCK_CINEMATIC;
                s_demoPhaseStartedMs = now;
                s_demoCinematicPage = 255;
                s_lockedTarget = -1;
                s_lockedTargetHitEligible = false;
                s_rearMissileActive = false;
                for (EnemyShot& shot : s_enemyShots) shot.startMs = 0;
                core2_glass2_set_demo_playing(0);
                core2_glass2_demo_unlock_message();
                Serial.println("[MECHA DEMO] IMPULSE UNLOCK CINEMATIC");
            }
        }
        Serial.printf("[MECHA DESTROY] target=%d type=%s\n", target,
                      ground ? "GND" : "AIR");
        // The exhibition GLASS2 is reserved for story/tutorial messaging.
        // Do not let the 20th kill overwrite the impulse-unlock sequence.
        if (!s_storyMode)
            core2_glass2_message("TARGET DESTROYED", "目標撃破");
        if (s_lockedTarget == target)
        {
            s_lockedTarget = -1;
            s_lockedTargetHitEligible = false;
        }
        if (target == MISSION_TARGET)
            Serial.println("[MECHA MISSION] OBJECTIVE DESTROYED");
    }
    return true;
}

bool damageLockedTarget(int damage, const char* weapon)
{
    const bool quietDemoMiss = s_autoPilotEnabled && s_autoPilotWasDriving;
    const int target = s_lockedTarget;
    if (target < 0 || target >= TARGET_COUNT)
    {
        const uint32_t now = millis();
        if (!quietDemoMiss && now - s_lastHitRejectLogMs >= 500)
        {
            s_lastHitRejectLogMs = now;
            Serial.printf("[MECHA SHOT MISS] weapon=%s reason=NO_LOCK\n", weapon);
        }
        return false;
    }
    if (!s_lockedTargetHitEligible || s_targetDestroyedUntilMs[target] != 0)
    {
        const uint32_t now = millis();
        if (!quietDemoMiss && now - s_lastHitRejectLogMs >= 500)
        {
            s_lastHitRejectLogMs = now;
            Serial.printf("[MECHA SHOT MISS] weapon=%s target=%d reason=INVALID_LOCK\n",
                          weapon, target);
        }
        return false;
    }

    int maxAimError = 36;
    float maxRange = CANNON_RANGE;
    if (strcmp(weapon, "MISSILE") == 0)
    {
        // Once launched, the missile may continue correcting toward its fixed
        // target ID even when the nose moves away from the original lead.
        maxAimError = 100;
        maxRange = MISSILE_RANGE;
    }
    else if (strcmp(weapon, "BEAM") == 0)
    {
        maxAimError = 24;
        maxRange = BEAM_RANGE;
    }
    else if (strncmp(weapon, "BLADE", 5) == 0)
    {
        const bool dashSlash = strcmp(weapon, "BLADE-CHARGE") == 0;
        maxAimError = dashSlash ? 54 : 38;
        maxRange = dashSlash ? BLADE_DASH_RANGE : BLADE_RANGE;
    }
    // The rival BOARD is a large, deliberate boss target. Give its visible
    // silhouette a slightly wider damage envelope so shots that clearly cross
    // the sprite do not fail on the point-target tuning used by minor craft.
    if (target == MISSION_TARGET)
    {
        if (strcmp(weapon, "CANNON") == 0) maxRange += 2.5f;
        else if (strcmp(weapon, "MISSILE") == 0) maxRange += 3.0f;
        maxAimError += 10;
    }
    if (s_lockedTargetRange > maxRange || s_lockedAimErrorPx > maxAimError)
    {
        const uint32_t now = millis();
        if (!quietDemoMiss && now - s_lastHitRejectLogMs >= 500)
        {
            s_lastHitRejectLogMs = now;
            Serial.printf("[MECHA SHOT MISS] weapon=%s target=%d reason=ENVELOPE range=%.1f/%0.1f aim=%d/%d\n",
                          weapon, target, s_lockedTargetRange, maxRange,
                          s_lockedAimErrorPx, maxAimError);
        }
        return false;
    }

    return applyTargetDamage(target, damage, weapon, s_lockedTargetRange,
                             s_lockedTargetX, s_lockedTargetY,
                             s_lockedTargetGround);
}

int damageBeamSweep()
{
    const int sweepX = (int)s_freeAimX;
    const int sweepY = (int)s_freeAimY;
    int hitCount = 0;
    for (int i = 0; i < TARGET_COUNT; ++i)
    {
        if (!s_targetBeamEligible[i] || s_targetDestroyedUntilMs[i] != 0)
            continue;
        const int dx = s_targetScreenX[i] - sweepX;
        const int dy = s_targetScreenY[i] - sweepY;
        // The boss silhouette is much larger than ordinary contacts. Its beam
        // envelope follows that visible body instead of treating it as a
        // point target, eliminating apparent edge hits that caused no damage.
        const int sweepRadius = i == MISSION_TARGET ? 32 : 22;
        if (dx*dx + dy*dy > sweepRadius*sweepRadius)
            continue;
        const int beamDamage = i == MISSION_TARGET ? 3 : 5;
        if (applyTargetDamage(i, beamDamage, "BEAM", s_targetScreenRange[i],
                              s_targetScreenX[i], s_targetScreenY[i],
                              s_targetScreenGround[i]))
            ++hitCount;
    }
    if (hitCount == 0)
    {
        const uint32_t now = millis();
        if (!(s_autoPilotEnabled && s_autoPilotWasDriving) &&
            now - s_lastHitRejectLogMs >= 500)
        {
            s_lastHitRejectLogMs = now;
            Serial.printf("[MECHA SHOT MISS] weapon=BEAM reason=SWEEP_EMPTY aim=%d,%d\n",
                          sweepX, sweepY);
        }
    }
    return hitCount;
}

void drawEnemyDestruction()
{
    if (s_enemyExplosionTarget < 0) return;
    const uint32_t elapsed = millis() - s_enemyExplosionStartMs;
    const bool boss = s_enemyExplosionTarget == MISSION_TARGET;
    const uint32_t duration = boss ? 1900 : 1100;
    if (elapsed >= duration)
    {
        s_enemyExplosionTarget = -1;
        return;
    }

    if (boss && elapsed < 150)
    {
        // Two-frame whiteout, then hot yellow: dramatic but entirely inside
        // the SRAM viewport, so it adds no extra LCD transactions.
        s_frame.fillScreen(elapsed < 65 ? WHITE : 0xFFE0);
    }

    const int phase = min(24, (int)(elapsed / 55));
    const int core = max(1, 7 - phase / 2);
    const int ring1 = 4 + phase * 2;
    const int ring2 = max(2, ring1 - 7);
    s_frame.fillCircle(s_enemyExplosionX, s_enemyExplosionY, core,
                       phase < 5 ? WHITE : YELLOW);
    s_frame.drawCircle(s_enemyExplosionX, s_enemyExplosionY, ring1,
                       phase < 7 ? ORANGE : 0x7800);
    s_frame.drawCircle(s_enemyExplosionX, s_enemyExplosionY, ring2,
                       phase < 6 ? WHITE : ORANGE);

    static const int8_t dx[12] = { 5,4,2,0,-2,-4,-5,-4,-2,0,2,4 };
    static const int8_t dy[12] = { 0,3,5,5, 5, 3, 0,-3,-5,-5,-5,-3 };
    for (int i = 0; i < 12; ++i)
    {
        const int speed = 2 + (i % 3);
        const int distance = 2 + phase * speed;
        const int x = s_enemyExplosionX + dx[i] * distance / 5;
        const int y = s_enemyExplosionY + dy[i] * distance / 5
                    + (s_enemyExplosionGround ? phase / 3 : 0);
        s_frame.drawPixel(x, y, (i % 3) == 0 ? WHITE : ((i & 1) ? ORANGE : YELLOW));
        if (phase < 9 && (i & 1) == 0)
            s_frame.drawPixel(x - dx[i] / 5, y - dy[i] / 5, 0xF980);
    }
    if (boss)
    {
        static const int8_t bx[7] = {-18, 15, -8, 20, -21, 6, 0};
        static const int8_t by[7] = {-17, -13, 2, 10, 17, 20, -2};
        for (int i = 0; i < 7; ++i)
        {
            const int age = (int)elapsed - i * 105;
            if (age < 0 || age >= 520) continue;
            const int r = 3 + age / 48;
            const int x = s_enemyExplosionX + bx[i];
            const int y = s_enemyExplosionY + by[i];
            s_frame.fillCircle(x, y, max(1, 4 - age / 100),
                               age < 120 ? WHITE : YELLOW);
            s_frame.drawCircle(x, y, r, age < 250 ? ORANGE : RED);
            s_frame.drawLine(x-r-3, y, x-r, y, YELLOW);
            s_frame.drawLine(x+r, y, x+r+3, y, ORANGE);
            s_frame.drawLine(x, y-r-3, x, y-r, WHITE);
        }
        if (elapsed > 720 && elapsed < 1450)
        {
            const int shock = 12 + (int)(elapsed - 720) / 12;
            s_frame.drawCircle(s_enemyExplosionX, s_enemyExplosionY,
                               shock, (elapsed & 64) ? WHITE : MAGENTA);
        }
    }
}

void updateInterceptor(float dt, uint32_t now)
{
    if (s_bossBattleActive)
    {
        s_interceptorActive = false;
        s_interceptorWarned = false;
        s_nextInterceptorMs = now + 1800;
        return;
    }
    if (s_rearMissileActive)
    {
        // One major warning at a time keeps the defensive choice readable.
        s_nextInterceptorMs = now + 1000;
        return;
    }
    // The choreographed demo already fills the screen with targets. Keep the
    // random interceptor out of that reel so collision warnings cannot land
    // on top of a weapon payoff or a vertical loop. Manual play is unchanged.
    if (s_autoPilotEnabled && s_autoPilotWasDriving)
    {
        s_interceptorActive = false;
        s_interceptorWarned = false;
        s_nextInterceptorMs = now + 1500;
        return;
    }
    if (!s_interceptorActive)
    {
        if ((int32_t)(now - s_nextInterceptorMs) < 0) return;
        if (s_targetDestroyedUntilMs[INTERCEPTOR_TARGET] != 0 &&
            (int32_t)(now - s_targetDestroyedUntilMs[INTERCEPTOR_TARGET]) < 0)
            return;

        s_targetDestroyedUntilMs[INTERCEPTOR_TARGET] = 0;
        s_targetHp[INTERCEPTOR_TARGET] = NORMAL_TARGET_HP;
        s_interceptorPattern = (InterceptorPattern)random(0, 3);
        const float offsetX = random(-65, 66) / 100.0f;
        const float offsetY = random(-45, 46) / 100.0f;
        s_interceptorX = s_shipX +
            (s_interceptorPattern == InterceptorPattern::DIRECT ? offsetX * 0.25f : offsetX);
        s_interceptorY = s_shipY +
            (s_interceptorPattern == InterceptorPattern::DIRECT ? offsetY * 0.25f : offsetY);
        s_interceptorZ = random(300, 430) / 10.0f;
        s_interceptorClosure = random(85, 155) / 10.0f;
        s_interceptorActive = true;
        s_interceptorWarned = false;
        Serial.printf("[MECHA INTERCEPT] INBOUND target=4 pattern=%u closure=%.1f\n",
                      (unsigned)s_interceptorPattern, s_interceptorClosure);
        return;
    }

    // DIRECT pursues until the warning; OFFSET commits early and is avoidable
    // with ordinary lateral movement; FEINT changes line once before committing.
    if (!s_interceptorWarned && s_interceptorPattern == InterceptorPattern::DIRECT)
    {
        s_interceptorX += (s_shipX - s_interceptorX) * min(1.0f, dt * 2.8f);
        s_interceptorY += (s_shipY - s_interceptorY) * min(1.0f, dt * 2.8f);
    }
    else if (!s_interceptorWarned && s_interceptorPattern == InterceptorPattern::FEINT &&
             s_interceptorZ < 28.0f)
    {
        s_interceptorX += (s_shipX - s_interceptorX) * min(1.0f, dt * 5.0f);
        s_interceptorY += (s_shipY - s_interceptorY) * min(1.0f, dt * 5.0f);
    }
    const float closureSpeed = s_interceptorClosure + s_currentSpeed * 0.20f;
    s_interceptorZ -= closureSpeed * dt;
    if (!s_interceptorWarned && s_interceptorZ <= 22.0f)
    {
        s_interceptorWarned = true;
        Serial.println("[MECHA INTERCEPT] COLLISION COURSE");
    }

    if (s_interceptorZ > 0.65f) return;

    const float dx = s_interceptorX - s_shipX;
    const float dy = s_interceptorY - s_shipY;
    const uint32_t rollAge = now - s_rollStartMs;
    const bool timingGuard = s_rollActive && rollAge >= 90 && rollAge <= 560;
    const bool collision = !timingGuard && fabsf(dx) < 0.52f && fabsf(dy) < 0.42f;
    const bool nearMiss = !collision && fabsf(dx) < 1.25f && fabsf(dy) < 0.95f;
    if (collision)
    {
        if (PLAYER_HULL_ENABLED) s_playerHull = max(0, s_playerHull - 1);
        s_collisionShakeUntilMs = now + 650;
        s_collisionSlowUntilMs = now + 900;
        s_boostKick = -1.5f;
        Serial.printf("[MECHA COLLISION] target=4 hull=%d dx=%.2f dy=%.2f\n",
                      s_playerHull, dx, dy);
    }
    else if (timingGuard && fabsf(dx) < 0.72f && fabsf(dy) < 0.62f)
    {
        Serial.printf("[MECHA ROLL GUARD] target=4 timing=%lums\n",
                      (unsigned long)rollAge);
    }
    else if (nearMiss)
    {
        Serial.printf("[MECHA NEAR MISS] target=4 dx=%.2f dy=%.2f\n", dx, dy);
    }
    else
    {
        Serial.printf("[MECHA EVADE] target=4 dx=%.2f dy=%.2f\n", dx, dy);
    }

    if (s_lockedTarget == INTERCEPTOR_TARGET)
    {
        s_lockedTarget = -1;
        s_lockedTargetHitEligible = false;
    }
    s_interceptorActive = false;
    s_nextInterceptorMs = now + random(5200, 9800);
}

void resolveRearMissile(uint32_t now, const char* result, bool impact)
{
    const bool flareIntercept = strcmp(result, "INTERCEPTED FLARE") == 0;
    s_rearMissileActive = false;
    s_flareInterceptPending = false;
    s_rearMissileEscapeMs = 0;
    s_nextRearMissileMs = now + (s_storyMode &&
        s_demoStoryPhase == DemoStoryPhase::ESCAPE
        ? random(2300, 3900)
        : random(14000, REAR_MISSILE_COOLDOWN_MS + 11000));
    s_externalHudDirty = true;
    MissileBlast& blast = s_missileBlasts[s_nextMissileBlast];
    s_nextMissileBlast = (uint8_t)((s_nextMissileBlast + 1) % 8);
    blast.startMs = now;
    blast.x = flareIntercept ? s_flareTargetX
                             : CX + 31;
    blast.y = flareIntercept ? s_flareTargetY
                             : H - 29;
    blast.heavy = impact || flareIntercept;
    if (flareIntercept)
    {
        const uint32_t interceptDuration = s_storyMode &&
            s_demoStoryPhase == DemoStoryPhase::IMPULSE_REVEAL ? 1200 : 750;
        s_flareInterceptUntilMs = now + interceptDuration;
        s_flareDetonationX = blast.x;
        s_flareDetonationY = blast.y;
        if (s_storyMode &&
            s_demoStoryPhase == DemoStoryPhase::IMPULSE_REVEAL)
        {
            // The exhibition salvo arrives from four bearings. Seed three
            // extra heavy blasts so every warning axis ends in a visible
            // flare-triggered detonation instead of an apparent empty shot.
            static const int8_t dx[3] = {-42, -32, 32};
            static const int8_t dy[3] = {-28, 30, 34};
            for (int i = 0; i < 3; ++i)
            {
                MissileBlast& extra = s_missileBlasts[s_nextMissileBlast];
                s_nextMissileBlast = (uint8_t)((s_nextMissileBlast + 1) % 8);
                extra.startMs = now;
                extra.x = constrain(s_flareOriginX + dx[i], 12, W - 12);
                extra.y = constrain(s_flareOriginY + dy[i], 18, H - 12);
                extra.heavy = true;
            }
        }
    }
    if (impact)
    {
        s_rearMissileHitStartMs = now;
        if (PLAYER_HULL_ENABLED) s_playerHull = max(0, s_playerHull - 1);
        s_collisionShakeUntilMs = now + 650;
        s_collisionSlowUntilMs = now + 900;
        s_boostKick = -1.5f;
    }
    Serial.printf("[MECHA REAR MISSILE] %s hull=%d\n", result, s_playerHull);
}

void updateRearMissile(uint32_t elapsedMs, uint32_t now, bool boost)
{
    if (s_bossBattleActive)
    {
        if (s_rearMissileActive) s_externalHudDirty = true;
        s_rearMissileActive = false;
        s_flareInterceptPending = false;
        s_nextRearMissileMs = now + 2500;
        return;
    }
    // The scripted autopilot is a weapon showcase. Preserve its timing, but
    // allow a manual demo run to encounter and test this threat.
    if (s_autoPilotEnabled && s_autoPilotWasDriving)
    {
        if (s_rearMissileActive) s_externalHudDirty = true;
        s_rearMissileActive = false;
        s_flareInterceptPending = false;
        s_nextRearMissileMs = now + 3000;
        return;
    }
    if (s_rearMissileActive && s_flareInterceptPending)
    {
        if ((int32_t)(now - s_flareInterceptAtMs) >= 0)
            resolveRearMissile(now, "INTERCEPTED FLARE", false);
        return;
    }
    if (!s_rearMissileActive)
    {
        if ((int32_t)(now - s_nextRearMissileMs) < 0) return;
        if (s_interceptorActive)
        {
            s_nextRearMissileMs = now + 600;
            return;
        }
        s_rearMissileActive = true;
        s_rearMissileElapsedMs = 0;
        s_rearMissileEscapeMs = 0;
        s_externalHudDirty = true;
        Serial.println("[MECHA REAR MISSILE] LOCK ON - MISSILE ALERT");
        return;
    }
    s_rearMissileElapsedMs += elapsedMs;
    // An immediate roll is too early; the missile can reacquire. The player
    // has a generous late window and can still accelerate or deploy a flare.
    const bool timingWindow = s_rearMissileElapsedMs >= 1500 &&
        s_rearMissileElapsedMs < REAR_MISSILE_APPROACH_MS;
    const uint32_t rollAge = now - s_rollStartMs;
    const uint32_t loopAge = now - s_manualLoopStartMs;
    if (timingWindow && s_rollActive && rollAge >= 110 && rollAge <= 430)
    {
        resolveRearMissile(now, "EVADED ROLL", false);
        return;
    }
    if (timingWindow && s_manualLoopActive && loopAge >= 240 && loopAge <= 1100)
    {
        resolveRearMissile(now, "EVADED LOOP", false);
        return;
    }
    if (boost && s_throttlePercent >= 135.0f && s_currentSpeed >= 28.0f)
        s_rearMissileEscapeMs += elapsedMs;
    else s_rearMissileEscapeMs = 0;
    if (s_rearMissileEscapeMs >= REAR_MISSILE_ESCAPE_MS)
    {
        resolveRearMissile(now, "OUTRUN", false);
        return;
    }
    if (s_rearMissileElapsedMs >= REAR_MISSILE_APPROACH_MS)
        resolveRearMissile(now, "IMPACT", true);
}

void initializeSinLut()
{
    if (s_sinLutReady) return;
    for (int i = 0; i < SIN_LUT_SIZE; ++i)
        s_sinLut[i] = sinf(TWO_PI * i / SIN_LUT_SIZE);
    s_sinLutReady = true;
}

inline float routeSin(float radians)
{
    // Route distances are positive. A power-of-two LUT and linear
    // interpolation replace hundreds of libm sinf calls per frame while
    // retaining smooth centerline motion.
    constexpr float SCALE = SIN_LUT_SIZE / TWO_PI;
    const float sample = radians * SCALE;
    const int32_t whole = (int32_t)sample;
    const float fraction = sample - whole;
    const int i0 = whole & (SIN_LUT_SIZE - 1);
    const int i1 = (i0 + 1) & (SIN_LUT_SIZE - 1);
    return s_sinLut[i0] + (s_sinLut[i1] - s_sinLut[i0]) * fraction;
}

float smoothCoursePhase(float phase)
{
    phase = constrain(phase, 0.0f, 1.0f);
    return phase * phase * (3.0f - 2.0f * phase);
}

float courseDistance()
{
    float d = fmodf(s_travel, 240.0f);
    return d < 0.0f ? d + 240.0f : d;
}

bool tunnelActiveAt(float d)
{
    return TUNNEL_TEST_ENABLED && s_tunnelRunActive && d >= 120.0f && d < 172.0f;
}
bool tunnelNarrowAt(float d) { return d >= 143.0f && d < 159.0f; }

void updateCourseMap()
{
    constexpr float COURSE_LENGTH = 240.0f;
    const float d = fmodf(s_travel, COURSE_LENGTH);
    s_courseBank = 0.0f;
    s_coursePitch = 0.0f;
    s_courseLoopSin = 0.0f;
    s_courseLoopCos = 1.0f;

    if (d < 30.0f)
    {
        s_courseSection = "STRAIGHT"; s_courseHeadingOffset = 0;
    }
    else if (d < 65.0f)
    {
        const float p = smoothCoursePhase((d - 30.0f) / 35.0f);
        s_courseSection = "LEFT ORBIT";
        s_courseBank = -0.34f * sinf(PI * p);
        s_courseHeadingOffset = (int)(90.0f * p);
    }
    else if (d < 90.0f)
    {
        const float p = smoothCoursePhase((d - 65.0f) / 25.0f);
        s_courseSection = "CLIMB ARC";
        s_courseBank = -0.10f * sinf(PI * p);
        s_coursePitch = -0.24f * sinf(PI * p);
        s_courseHeadingOffset = 90;
    }
    else if (d < 126.0f)
    {
        s_courseSection = "LOOP GATE";
        s_courseHeadingOffset = 90;
    }
    else if (s_tunnelRunActive && d < 143.0f)
    {
        s_courseSection = "TUNNEL ENTRY"; s_courseHeadingOffset = 90;
    }
    else if (s_tunnelRunActive && d < 159.0f)
    {
        s_courseSection = "NARROW TUNNEL"; s_courseHeadingOffset = 90;
    }
    else if (s_tunnelRunActive && d < 172.0f)
    {
        s_courseSection = "TUNNEL EXIT"; s_courseHeadingOffset = 90;
    }
    else if (!s_tunnelRunActive && d < 161.0f)
    {
        // The tunnel is a low-altitude branch. Normal flight remains on the
        // open-air route above it instead of being drawn into the entrance.
        s_courseSection = "OVERPASS"; s_courseHeadingOffset = 90;
    }
    else if (d < 196.0f)
    {
        const float orbitStart = s_tunnelRunActive ? 172.0f : 161.0f;
        const float p = smoothCoursePhase((d - orbitStart) / (196.0f - orbitStart));
        s_courseSection = "RIGHT ORBIT";
        s_courseBank = 0.34f * sinf(PI * p);
        s_courseHeadingOffset = 180 + (int)(180.0f * p);
    }
    else if (d < 220.0f)
    {
        const float p = smoothCoursePhase((d - 196.0f) / 24.0f);
        s_courseSection = "DIVE ARC";
        s_coursePitch = 0.24f * sinf(PI * p);
        s_courseHeadingOffset = 0;
    }
    else
    {
        s_courseSection = "FINAL RUN"; s_courseHeadingOffset = 0;
    }
}

const char* weaponName()
{
    static const char* names[] = { "BARRAGE", "MISSILE", "BEAM", "MULTI", "P-BLADE", "FLARE" };
    return names[s_weapon % GAME_WEAPON_COUNT];
}

bool multiVolleyActive()
{
    for (const MultiMissileShot& shot : s_multiShots)
        if (shot.startMs != 0) return true;
    return false;
}

void respawnStar(int i)
{
    s_stars[i].x = random(-1000, 1001) / 1000.0f;
    s_stars[i].y = random(-1000, 1001) / 1000.0f;
    s_stars[i].z = random(350, 1001) / 1000.0f;
}

void updateWarpStars(float dt, float speed)
{
    // At zero commanded throttle the machine is hovering. Freeze longitudinal
    // star travel immediately instead of retaining the former minimum drift.
    if (s_throttlePercent <= 0.01f) return;
    const float warp = 0.40f + speed * 0.025f;
    for (int i = 0; i < STAR_COUNT; ++i)
    {
        s_stars[i].z -= warp * dt;
        if (s_stars[i].z <= 0.055f) respawnStar(i);
    }
}

void routePosition(float distance, float* x, float* y)
{
    // Two low-frequency waves avoid a visibly repetitive single sine while
    // keeping the course deterministic and almost free to calculate.
    *x = routeSin(distance * 0.120f) * 3.05f
       + routeSin(distance * 0.043f + 1.2f) * 1.05f;
    *y = routeSin(distance * 0.086f + 0.7f) * 1.65f
       + routeSin(distance * 0.033f + 2.4f) * 0.68f;
}

float towerLane(int i)
{
    // Preserve near-center pressure without ever sealing the central escape
    // corridor. Most towers now live on the outer half of the flight range.
    static const float lane[6] = { -1.75f, 1.60f, 2.15f,
                                   -2.05f, 0.95f, -1.05f };
    return lane[i % 6];
}

float treeLane(int i)
{
    static const float lane[8] = { -2.10f, -1.20f, 1.10f, 2.05f,
                                   -1.65f, 0.85f, 1.60f, -0.90f };
    return lane[i % 8];
}

void applyTerrainCollision(const char* type, uint32_t now, float knockX)
{
    if ((int32_t)(now - s_terrainCollisionCooldownUntilMs) < 0) return;
    s_terrainCollisionCooldownUntilMs = now + 900;
    if (PLAYER_HULL_ENABLED) s_playerHull = max(0, s_playerHull - 1);
    s_collisionShakeUntilMs = now + 520;
    s_collisionSlowUntilMs = now + 700;
    s_boostKick = -1.1f;
    s_shipVx += knockX;
    Serial.printf("[MECHA TERRAIN HIT] type=%s hull=%d alt=%.0f x=%.2f\n",
                  type, s_playerHull, s_altitudeM, s_shipX);
}

void updateTerrainCollisions(uint32_t now)
{
    if ((int32_t)(now - s_terrainCollisionCooldownUntilMs) < 0) return;

    // The flight floor is a real surface rather than only a numerical clamp.
    if (s_altitudeM <= MIN_ALTITUDE_M + 18.0f && s_shipVy > 0.35f)
    {
        applyTerrainCollision("GROUND", now, 0.0f);
        s_altitudeM = MIN_ALTITUDE_M + 45.0f;
        s_shipVy = -1.5f;
        return;
    }

    float routeNowX, routeNowY;
    routePosition(s_travel, &routeNowX, &routeNowY);
    // Match the six side towers drawn by drawWorldAnchors().
    for (int i = 0; i < 6; ++i)
    {
        float z = fmodf(i * 7.0f + 5.0f - s_travel, 56.0f);
        if (z < 0.0f) z += 56.0f;
        if (z > 1.35f) continue;
        float ax, ay; routePosition(s_travel + z, &ax, &ay);
        const float objectX = ax - routeNowX + towerLane(i);
        const float heightM = (1.5f + (i % 4) * 0.55f) / ALTITUDE_TO_WORLD;
        if (fabsf(s_shipX - objectX) < 0.58f && s_altitudeM < heightM + 90.0f)
        {
            applyTerrainCollision("STRUCTURE", now, s_shipX < objectX ? -1.8f : 1.8f);
            return;
        }
    }
    // Match the eight trees drawn by drawWorldAnchors().
    for (int i = 0; i < 8; ++i)
    {
        float z = fmodf(2.0f + i * 4.1f - s_travel, 49.2f);
        if (z < 0.0f) z += 49.2f;
        if (z > 1.15f) continue;
        float ax, ay; routePosition(s_travel + z, &ax, &ay);
        const float objectX = ax - routeNowX + treeLane(i);
        const float heightM = (0.65f + (i % 4) * 0.16f) / ALTITUDE_TO_WORLD;
        if (fabsf(s_shipX - objectX) < 0.42f && s_altitudeM < heightM + 55.0f)
        {
            applyTerrainCollision("TREE", now, s_shipX < objectX ? -1.2f : 1.2f);
            return;
        }
    }
}

void enforceTunnelPassage(float previousTravel, uint32_t now)
{
    const float d = courseDistance();
    if (!tunnelActiveAt(d)) return;

    const float minTunnelAltitude = tunnelNarrowAt(d) ? 300.0f : 180.0f;
    const float maxTunnelAltitude = tunnelNarrowAt(d) ? 720.0f : 980.0f;
    const bool outsideWalls = fabsf(s_shipX) > (tunnelNarrowAt(d) ? 1.18f : 2.65f) ||
                              s_altitudeM < minTunnelAltitude ||
                              s_altitudeM > maxTunnelAltitude;
    if (outsideWalls)
    {
        applyTerrainCollision("TUNNEL WALL", now, s_shipX < 0.0f ? 1.5f : -1.5f);
        s_shipX = constrain(s_shipX, -2.55f, 2.55f);
        s_altitudeM = constrain(s_altitudeM,
            minTunnelAltitude + 30.0f, maxTunnelAltitude - 30.0f);
    }

    // Alternating maintenance shutters create a slalom without enemies.
    if (fabsf(d - 132.0f) < 0.55f && s_shipX < 0.65f)
        applyTerrainCollision("TUNNEL SHUTTER R", now, 1.8f);
    if (fabsf(d - 138.0f) < 0.55f && s_shipX > -0.65f)
        applyTerrainCollision("TUNNEL SHUTTER L", now, -1.8f);
    if (fabsf(d - 165.0f) < 0.55f && s_altitudeM > 560.0f)
    {
        applyTerrainCollision("TUNNEL CEILING BAR", now, 0.0f);
        s_shipVy = max(s_shipVy, 1.4f);
    }

    // The narrow shutter is physically too low for humanoid form. Hold the
    // craft at its entrance until transformation is substantially complete.
    if (tunnelNarrowAt(d) && s_transformBlend < 0.82f)
    {
        const float lapStart = floorf(previousTravel / 240.0f) * 240.0f;
        s_travel = max(previousTravel, lapStart + 142.72f);
        if (courseDistance() >= 143.0f) s_travel = lapStart + 142.72f;
        applyTerrainCollision("FLIGHT FORM REQUIRED", now, 0.0f);
        s_collisionSlowUntilMs = now + 350;
    }
}

void updateTunnelRouteTrigger(uint32_t now)
{
    if (!TUNNEL_TEST_ENABLED)
    {
        s_tunnelRunActive = false;
        return;
    }
    const float d = courseDistance();
    if (!s_tunnelRunActive && d >= 118.5f && d < 122.0f &&
        s_altitudeM <= 900.0f && fabsf(s_shipX) <= 2.35f)
    {
        s_tunnelRunActive = true;
        s_lockedTarget = -1;
        s_lockCandidate = -1;
        s_lockedTargetHitEligible = false;
        for (EnemyShot& shot : s_enemyShots) shot.startMs = 0;
        s_rearMissileActive = false;
        s_interceptorActive = false;
        s_externalHudDirty = true;
        Serial.printf("[MECHA TUNNEL] ENTER alt=%.0f x=%.2f\n",
                      s_altitudeM, s_shipX);
    }
    else if (s_tunnelRunActive && (d >= 172.0f || d < 118.0f))
    {
        s_tunnelRunActive = false;
        s_externalHudDirty = true;
        Serial.printf("[MECHA TUNNEL] EXIT alt=%.0f\n", s_altitudeM);
    }
}

void updateRouteEnemyCollisions(uint32_t now)
{
    if ((int32_t)(now - s_routeEnemyCollisionCooldownUntilMs) < 0) return;
    float routeNowX, routeNowY;
    routePosition(s_travel, &routeNowX, &routeNowY);
    for (int i = 1; i < TARGET_COUNT; i += 2)
    {
        if (i >= 5) continue; // formation contacts use their own clustered path
        if (i == INTERCEPTOR_TARGET || s_targetDestroyedUntilMs[i] != 0) continue;
        float z = fmodf(16.0f + i * 13.0f - s_travel, 62.0f);
        if (z < 0.0f) z += 62.0f;
        if (z < 0.25f || z > 1.50f) continue;
        float routeX, routeY;
        routePosition(s_travel + z, &routeX, &routeY);
        float x = routeX - routeNowX + ((i & 2) ? 2.4f : -2.0f);
        const float targetAltitudeM = 1500.0f + i * 450.0f;
        float y = routeY - routeNowY
                + (s_altitudeM - targetAltitudeM) * ALTITUDE_TO_WORLD;
        const float commit = constrain((18.0f - z) / 17.3f, 0.0f, 0.88f);
        x *= 1.0f - commit;
        y *= 1.0f - commit;
        if (fabsf(x - s_shipX) >= 0.58f || fabsf(y - s_shipY) >= 0.48f)
            continue;

        const uint32_t rollAge = now - s_rollStartMs;
        const bool timingGuard = s_rollActive && rollAge >= 90 && rollAge <= 560;
        s_routeEnemyCollisionCooldownUntilMs = now + 900;
        if (timingGuard)
        {
            s_targetDestroyedUntilMs[i] = now + 2500;
            if (s_lockedTarget == i) s_lockedTarget = -1;
            Serial.printf("[MECHA ROLL GUARD] target=%d timing=%lums\n",
                          i, (unsigned long)rollAge);
            return;
        }

        if (PLAYER_HULL_ENABLED) s_playerHull = max(0, s_playerHull - 1);
        s_collisionShakeUntilMs = now + 650;
        s_collisionSlowUntilMs = now + 900;
        s_boostKick = -1.5f;
        applyTargetDamage(i, s_targetHp[i], "COLLISION", z,
                          s_targetScreenX[i], s_targetScreenY[i], false);
        Serial.printf("[MECHA COLLISION] target=%d hull=%d dx=%.2f dy=%.2f\n",
                      i, s_playerHull, x - s_shipX, y - s_shipY);
        return;
    }
}

bool project(float x, float y, float z, Point2* out)
{
    if (z < 0.25f) return false;
    const float projection = PROJECTION / s_fovScale;
    const float px = (x - s_cameraX) * projection / z;
    // Pitch adds a depth-proportional vertical component: distant geometry
    // and the horizon move together as the craft climbs or dives.
    const float py = ((y - s_cameraY) +
                     (s_cameraPitch + s_routePitch * AUTO_ROUTE_CAMERA
                      + s_coursePitch) * z)
                   * projection / z;
    // Small-angle roll is visually effective and avoids per-point trig.
    const float totalBank = s_cameraBank + s_routeBank * AUTO_ROUTE_CAMERA + s_courseBank;
    const float bankX = px - totalBank * py;
    const float bankY = py + totalBank * px;
    // Aerobatic camera roll needs a true rotation (the normal coordinated
    // bank above intentionally uses the cheaper small-angle approximation).
    const float actionCos = cosf(s_actionCameraRoll);
    const float actionSin = sinf(s_actionCameraRoll);
    const float rx = bankX * actionCos - bankY * actionSin;
    const float bankedY = bankX * actionSin + bankY * actionCos;
    // A vertical loop flips only the pitch plane. This retains the cheap
    // forward projection while allowing ground and horizon to pass overhead.
    const float ry = bankedY * s_courseLoopCos + s_courseLoopSin * 72.0f;
    out->x = CX + (int)rx;
    out->y = HORIZON_Y + (int)ry + (int)(s_boostKick * 2.0f);
    return out->x > -150 && out->x < 390 && out->y > -120 && out->y < 300;
}

uint16_t depthColor(float z)
{
    if (z < 8) return CYAN;
    if (z < 18) return 0x04D3;
    if (z < 35) return 0x034C;
    return 0x01A6;
}

void drawGate(float z, int gateIndex)
{
    const float pulse = sinf((float)gateIndex * 1.7f) * 0.45f;
    float routeNowX, routeNowY, routeAheadX, routeAheadY;
    routePosition(s_travel, &routeNowX, &routeNowY);
    routePosition(s_travel + z, &routeAheadX, &routeAheadY);
    const float cx = routeAheadX - routeNowX + pulse;
    const float cy = routeAheadY - routeNowY;
    const float halfW = 4.2f;
    const float halfH = 2.55f;
    Point2 p[4];
    if (!project(cx - halfW, cy - halfH, z, &p[0]) ||
        !project(cx + halfW, cy - halfH, z, &p[1]) ||
        !project(cx + halfW, cy + halfH, z, &p[2]) ||
        !project(cx - halfW, cy + halfH, z, &p[3])) return;
    const uint16_t color = depthColor(z);
    for (int i = 0; i < 4; ++i)
        s_frame.drawLine(p[i].x, p[i].y, p[(i + 1) & 3].x, p[(i + 1) & 3].y, color);

    // Short notches retain the mechanical silhouette without 4 costly circle
    // rasterizations per gate.
    const int notch = z < 12 ? 5 : 3;
    for (int i = 0; i < 4; ++i)
        s_frame.drawFastHLine(p[i].x - notch, p[i].y, notch * 2 + 1, color);
}

void drawWorldAnchors()
{
    float routeNowX, routeNowY;
    routePosition(s_travel, &routeNowX, &routeNowY);

    // A sparse distant mountain ridge supplies a stable horizon reference.
    Point2 previous = {};
    bool havePrevious = false;
    for (int i = 0; i <= 9; ++i)
    {
        const float z = 31.0f + (i & 1) * 2.5f;
        float ax, ay; routePosition(s_travel + z, &ax, &ay);
        const float x = ax - routeNowX + (i - 4.5f) * 2.75f;
        const float peak = 1.0f + (float)((i * 7) % 5) * 0.42f;
        Point2 p;
        const float ground = ay - routeNowY + s_altitudeM * ALTITUDE_TO_WORLD;
        if (project(x, ground - peak, z, &p))
        {
            if (havePrevious) s_frame.drawLine(previous.x, previous.y, p.x, p.y, 0x0324);
            previous = p; havePrevious = true;
        }
    }

    // Alternating side towers provide strong near-field parallax and height.
    for (int i = 0; i < 6; ++i)
    {
        float z = fmodf(i * 7.0f + 5.0f - s_travel, 56.0f);
        if (z < 0.25f) z += 56.0f;
        float ax, ay; routePosition(s_travel + z, &ax, &ay);
        const float cx = ax - routeNowX + towerLane(i);
        const float ground = ay - routeNowY + s_altitudeM * ALTITUDE_TO_WORLD;
        const float height = 1.5f + (i % 4) * 0.55f;
        Point2 base, top, left, right;
        if (!project(cx, ground, z, &base) || !project(cx, ground - height, z, &top)) continue;
        project(cx - 0.45f, ground, z, &left);
        project(cx + 0.45f, ground, z, &right);
        const uint16_t c = z < 13 ? 0x05EC : 0x02C5;
        s_frame.drawLine(base.x, base.y, top.x, top.y, c);
        s_frame.drawLine(left.x, left.y, base.x, base.y, c);
        s_frame.drawLine(base.x, base.y, right.x, right.y, c);
        s_frame.drawFastHLine(top.x - (z < 12 ? 4 : 2), top.y, z < 12 ? 9 : 5, c);
    }

    // Two floor rails bend with the route and make climb/dive immediately legible.
    for (int side = -1; side <= 1; side += 2)
    {
        Point2 prev = {}; bool valid = false;
        for (int i = 1; i <= 5; ++i)
        {
            const float z = i * 6.0f;
            float ax, ay; routePosition(s_travel + z, &ax, &ay);
            Point2 p;
            const float ground = ay - routeNowY + s_altitudeM * ALTITUDE_TO_WORLD;
            if (!project(ax - routeNowX + side * 4.8f, ground, z, &p)) continue;
            if (valid) s_frame.drawLine(prev.x, prev.y, p.x, p.y, 0x0223);
            prev = p; valid = true;
        }
    }

    // Near-ground crossbars make altitude and closure rate readable. At high
    // altitude their projected Y naturally leaves the viewport; at low
    // altitude the near bars expand toward the bottom edge.
    if (s_altitudeM < 2600.0f)
    {
        static const float groundZ[] = { 1.0f, 1.8f, 3.2f, 5.8f, 10.5f, 17.0f };
        for (float z : groundZ)
        {
            float ax, ay; routePosition(s_travel + z, &ax, &ay);
            const float ground = ay - routeNowY + s_altitudeM * ALTITUDE_TO_WORLD;
            Point2 left, right;
            if (project(ax - routeNowX - 7.0f, ground, z, &left) &&
                project(ax - routeNowX + 7.0f, ground, z, &right))
            {
                const uint16_t c = z < 3.0f ? 0x04C3 : 0x0262;
                s_frame.drawLine(left.x, left.y, right.x, right.y, c);
            }
        }

        // Sparse wireframe trees provide familiar scale close to the ground.
        // Their fixed world height means they grow rapidly during approach.
        for (int i = 0; i < 8; ++i)
        {
            float z = fmodf(2.0f + i * 4.1f - s_travel, 49.2f);
            if (z < 0.25f) z += 49.2f;
            float ax, ay; routePosition(s_travel + z, &ax, &ay);
            const float x = ax - routeNowX + treeLane(i);
            const float ground = ay - routeNowY + s_altitudeM * ALTITUDE_TO_WORLD;
            const float height = 0.65f + (i % 4) * 0.16f;
            Point2 base, trunkTop, crownTop, crownLeft, crownRight;
            if (!project(x, ground, z, &base) ||
                !project(x, ground - height * 0.45f, z, &trunkTop) ||
                !project(x, ground - height, z, &crownTop) ||
                !project(x - height * 0.38f, ground - height * 0.38f, z, &crownLeft) ||
                !project(x + height * 0.38f, ground - height * 0.38f, z, &crownRight)) continue;
            const uint16_t c = z < 12.0f ? 0x0664 : 0x0323;
            s_frame.drawLine(base.x, base.y, trunkTop.x, trunkTop.y, c);
            s_frame.drawTriangle(crownTop.x, crownTop.y, crownLeft.x, crownLeft.y,
                                 crownRight.x, crownRight.y, c);
        }
    }
}

void drawTunnelSection(uint32_t now)
{
    if (!TUNNEL_TEST_ENABLED) return;
    const float d = courseDistance();
    if (d < 112.0f || d >= 172.0f) return;
    const bool inside = s_tunnelRunActive;
    if (!inside && d >= 120.0f) return;

    // Once the entry trigger is crossed, replace the open-air stage with the
    // tunnel interior. Before entry, only its mouth is visible from outside.
    if (inside) s_frame.fillScreen(0x0003);

    float routeNowX, routeNowY;
    routePosition(s_travel, &routeNowX, &routeNowY);
    Point2 previous[4] = {};
    bool havePrevious = false;
    for (int ring = 17; ring >= 0; --ring)
    {
        const float ringD = 120.0f + ring * 3.0f;
        if (ringD >= 172.0f) continue;
        const float z = ringD - d;
        if (z < 0.45f || z > 42.0f) continue;
        float routeX, routeY;
        routePosition(s_travel + z, &routeX, &routeY);
        const float cx = routeX - routeNowX;
        // The tunnel is built on the ground and has a fixed vertical corridor.
        // Offset it against current altitude so high-level normal flight sees
        // the mouth below and passes over it.
        const float cy = routeY - routeNowY
                       + (s_altitudeM - 520.0f) * ALTITUDE_TO_WORLD;
        const bool narrow = tunnelNarrowAt(ringD);
        const float halfW = narrow ? 1.55f : 3.25f;
        const float halfH = narrow ? 0.98f : 2.05f;
        Point2 p[4];
        if (!project(cx - halfW, cy - halfH, z, &p[0]) ||
            !project(cx + halfW, cy - halfH, z, &p[1]) ||
            !project(cx + halfW, cy + halfH, z, &p[2]) ||
            !project(cx - halfW, cy + halfH, z, &p[3])) continue;
        const uint16_t c = narrow
            ? (((now / 120 + ring) & 1) ? ORANGE : RED)
            : (z < 9.0f ? 0x7DFF : z < 22.0f ? 0x34B6 : 0x1250);
        for (int edge = 0; edge < 4; ++edge)
        {
            s_frame.drawLine(p[edge].x, p[edge].y,
                             p[(edge + 1) & 3].x, p[(edge + 1) & 3].y, c);
            if (z < 8.0f)
                s_frame.drawLine(p[edge].x + 1, p[edge].y,
                                 p[(edge + 1) & 3].x + 1,
                                 p[(edge + 1) & 3].y, c);
            if (havePrevious)
                s_frame.drawLine(p[edge].x, p[edge].y,
                                 previous[edge].x, previous[edge].y,
                                 narrow ? 0x8200 : 0x1290);
            previous[edge] = p[edge];
        }

        // Solid maintenance shutters match the collision geometry: first a
        // right-hand opening, then a left-hand opening, followed by a low bar.
        if (fabsf(ringD - 132.0f) < 0.25f)
        {
            const int gapX = p[0].x + (p[1].x - p[0].x) * 3 / 4;
            s_frame.fillRect(p[0].x, p[0].y, max(1, gapX - p[0].x),
                             max(1, p[3].y - p[0].y), 0x3186);
            s_frame.drawLine(gapX, p[0].y, gapX, p[3].y, ORANGE);
        }
        else if (fabsf(ringD - 138.0f) < 0.25f)
        {
            const int gapX = p[0].x + (p[1].x - p[0].x) / 4;
            s_frame.fillRect(gapX, p[1].y, max(1, p[1].x - gapX),
                             max(1, p[2].y - p[1].y), 0x3186);
            s_frame.drawLine(gapX, p[0].y, gapX, p[3].y, ORANGE);
        }
        else if (fabsf(ringD - 165.0f) < 0.25f)
        {
            const int barY = p[0].y + (p[3].y - p[0].y) * 2 / 5;
            s_frame.fillRect(p[0].x, p[0].y, max(1, p[1].x - p[0].x),
                             max(1, barY - p[0].y), 0x3186);
            s_frame.drawLine(p[0].x, barY, p[1].x, barY, ORANGE);
        }
        havePrevious = true;
    }

    if (!inside)
    {
        const bool lowEnough = s_altitudeM <= 900.0f;
        const uint16_t cue = lowEnough ? CYAN : ORANGE;
        s_frame.fillRect(CX - 60, 20, 120, 20, BLACK);
        s_frame.drawRect(CX - 60, 20, 120, 20, cue);
        s_frame.setTextColor(cue, BLACK);
        s_frame.setCursor(CX - 46, 23);
        s_frame.print("LOW ALT TUNNEL");
        s_frame.setCursor(CX - 42, 31);
        s_frame.print(lowEnough ? "ENTRY READY" : "DESCEND <900M");
    }

    if (inside && d >= 136.0f && d < 159.0f)
    {
        const bool ready = s_transformBlend >= 0.82f;
        const uint16_t warning = ready ? CYAN : (((now / 180) & 1) ? RED : ORANGE);
        s_frame.fillRect(CX - 62, 20, 124, 12, BLACK);
        s_frame.drawRect(CX - 62, 20, 124, 12, warning);
        s_frame.setTextColor(warning, BLACK);
        s_frame.setCursor(CX - (ready ? 34 : 53), 23);
        s_frame.print(ready ? "FLIGHT FORM OK" : "FLIGHT FORM REQUIRED");
    }
}

static const char* const BOSS_PIXEL_ROWS[] = {
    "..........KK............", ".........KAAK...........",
    "........KAPPAK..........", "...DD...KAPPAK...DD.....",
    "..KDDK.KAAWWAAK.KDDK....", ".KDPDKKAWCCWAKKDPDK.....",
    ".KDPPAKAAAAAAKAPPDK......", "KDDPPPKARRAKAPPPDDK.....",
    "KDDPPPKAAAAAKPPPDDK.....", ".KDKKKAAAAAAAAKKKDK......",
    "..KK.KAAAPAAAK.KK.......", "....KAPPPPPPAK...........",
    "...KAPAKKKKAPAK..........", "..KAAK.KDDK.KAAK.........",
    ".KAAK..KDDK..KAAK........", "KADK...KPPK...KDAK.......",
    "KADK..KPPPPK..KDAK.......", ".KK...KPAAPK...KK.........",
    ".....KPA..APK............", "....KPAK..KAPK...........",
    "...KAAK....KAAK..........", "...KADK....KDAK..........",
    "..KADK......KDAK.........", "..KADK......KDAK.........",
    ".KAAK........KAAK........", ".KAWK........KWAK........",
    ".KAK..........KAK........", "KAAK..........KAAK.......",
    "KAK............KAK.......", "KK..............KK........",
    "O................O.......", "........................"
};

uint16_t bossPixelColor(char c)
{
    switch (c)
    {
        case 'K': return 0x0841; // deep outline
        case 'D': return 0x2928; // inner frame
        case 'A': return 0xC618; // cool armor
        case 'W': return WHITE;
        case 'P': return 0x79DF; // violet enemy armor
        case 'R': return RED;
        case 'C': return CYAN;
        case 'O': return ORANGE;
        default: return MECHA_HYBRID_BODY_TRANSPARENT;
    }
}

void initializeBossSprite()
{
    s_bossCanvas.setColorDepth(16);
    s_bossCanvasReady = s_bossCanvas.createSprite(24, 32) != nullptr;
    if (!s_bossCanvasReady) return;
    s_bossCanvas.fillSprite(MECHA_HYBRID_BODY_TRANSPARENT);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 24 && BOSS_PIXEL_ROWS[y][x] != '\0'; ++x)
            if (BOSS_PIXEL_ROWS[y][x] != '.')
                s_bossCanvas.drawPixel(x, y, bossPixelColor(BOSS_PIXEL_ROWS[y][x]));
    s_bossCanvas.setPivot(12, 16);
}

void drawBossMecha(int x, int y, int size, uint32_t now)
{
    if (!s_bossCanvasReady) return;
    const float scale = constrain(0.70f + size / 35.0f, 0.92f, 1.48f);
    const float bank = sinf(now * 0.0012f) * 3.0f;
    s_bossCanvas.pushRotateZoom(&s_frame, x, y, bank, scale, scale,
                                MECHA_HYBRID_BODY_TRANSPARENT);
    // Rear-view hover posture: paired nozzles remain lit even while stationary;
    // departure/attack phases stretch only the luminous core, not a long tail.
    const bool fast = s_bossMovePhase == BossMovePhase::EXIT ||
                      s_bossMovePhase == BossMovePhase::ATTACK_PASS ||
                      s_bossMovePhase == BossMovePhase::CHARGE;
    const int nozzleY = y + (int)(15 * scale);
    const int nozzleX = max(3, (int)(4 * scale));
    const int plume = fast ? 8 : 3 + ((now / 90) & 1);
    s_frame.fillCircle(x-nozzleX, nozzleY, 2, 0x0410);
    s_frame.fillCircle(x+nozzleX, nozzleY, 2, 0x0410);
    s_frame.drawLine(x-nozzleX, nozzleY+2, x-nozzleX, nozzleY+plume, CYAN);
    s_frame.drawLine(x+nozzleX, nozzleY+2, x+nozzleX, nozzleY+plume, CYAN);
    s_frame.drawPixel(x-nozzleX, nozzleY+plume+1, WHITE);
    s_frame.drawPixel(x+nozzleX, nozzleY+plume+1, WHITE);
    const int gunY = y + (int)(7 * scale);
    const int gunX = (int)(11 * scale);
    s_frame.drawLine(x-gunX, gunY, x-gunX-4, gunY+8, 0xB81F);
    s_frame.drawLine(x+gunX, gunY, x+gunX+4, gunY+8, 0xB81F);
    if (now + 180 >= s_enemyNextFireMs[MISSION_TARGET])
    {
        s_frame.fillCircle(x-gunX-4, gunY+8, 2, ORANGE);
        s_frame.fillCircle(x+gunX+4, gunY+8, 2, ORANGE);
    }
    if (s_bossMovePhase == BossMovePhase::CHARGE_WINDUP)
    {
        const uint16_t pulse = ((now / 90) & 1) ? RED : ORANGE;
        const int r = (int)(18 * scale) + ((now / 70) % 5);
        s_frame.drawCircle(x, y, r, pulse);
        s_frame.drawLine(x-r-8, y, x-r+1, y, pulse);
        s_frame.drawLine(x+r-1, y, x+r+8, y, pulse);
    }
}

void drawEnemyShots(uint32_t now)
{
    bool missileThreat = false;
    for (EnemyShot& shot : s_enemyShots)
    {
        if (shot.startMs == 0) continue;
        const uint32_t age = now - shot.startMs;
        if (age >= shot.durationMs) continue;
        const int p = (int)(age * 255 / shot.durationMs);
        const int x = shot.startX + (shot.aimX - shot.startX) * p / 255;
        const int y = shot.startY + (shot.aimY - shot.startY) * p / 255;
        if (shot.kind == EnemyShotKind::CANNON)
        {
            const int tailP = max(0, p - 42);
            const int tx = shot.startX + (shot.aimX - shot.startX) * tailP / 255;
            const int ty = shot.startY + (shot.aimY - shot.startY) * tailP / 255;
            s_frame.drawLine(tx, ty, x, y, p < 150 ? ORANGE : WHITE);
            s_frame.drawPixel(x, y, YELLOW);
        }
        else if (shot.kind == EnemyShotKind::HOMING_MISSILE)
        {
            missileThreat = true;
            const int curve = (int)(sinf(p * PI / 255.0f) * 17.0f);
            const int side = (shot.startX & 1) ? -1 : 1;
            const int mx = x + side * curve;
            s_frame.drawLine(mx - side * 7, y-3, mx, y, ORANGE);
            s_frame.drawLine(mx - side * 4, y-2, mx, y, RED);
            s_frame.fillCircle(mx, y, 2, WHITE);
        }
        else
        {
            // Unguided missile: a bright, straight trajectory. It is faster
            // but commits to the player's old position and can be sidestepped.
            missileThreat = true;
            const int tailP = max(0, p - 58);
            const int tx = shot.startX + (shot.aimX - shot.startX) * tailP / 255;
            const int ty = shot.startY + (shot.aimY - shot.startY) * tailP / 255;
            s_frame.drawLine(tx, ty, x, y, YELLOW);
            s_frame.drawLine((tx+x)/2, (ty+y)/2, x, y, WHITE);
            s_frame.fillCircle(x, y, 2, ORANGE);
        }
    }
    const uint32_t cutAge = now - s_missileCutStartMs;
    if (s_missileCutStartMs != 0 && cutAge < 440)
    {
        const int r = 4 + (int)(cutAge / 24);
        const uint16_t edge = cutAge < 120 ? WHITE : GREEN;
        // Crossed blade flash, expanding plasma ring and a few deterministic
        // fragments make the interception distinct from a normal explosion.
        s_frame.drawLine(s_missileCutX - r, s_missileCutY - r,
                         s_missileCutX + r, s_missileCutY + r, edge);
        s_frame.drawLine(s_missileCutX + r, s_missileCutY - r,
                         s_missileCutX - r, s_missileCutY + r, CYAN);
        s_frame.drawCircle(s_missileCutX, s_missileCutY, max(2, r / 2), GREEN);
        if (cutAge < 100) s_frame.fillCircle(s_missileCutX, s_missileCutY, 3, WHITE);
        for (int i = 0; i < 6; ++i)
        {
            const int dx = ((i * 7) % 11) - 5;
            const int dy = ((i * 5) % 9) - 4;
            s_frame.drawPixel(s_missileCutX + dx * r / 5,
                              s_missileCutY + dy * r / 5,
                              (i & 1) ? ORANGE : CYAN);
        }
        s_frame.fillRect(CX - 40, 44, 80, 13, BLACK);
        s_frame.drawRect(CX - 40, 44, 80, 13, GREEN);
        s_frame.setTextColor(cutAge < 180 ? WHITE : GREEN, BLACK);
        s_frame.setCursor(CX - 32, 47);
        s_frame.print(s_missileCutCount > 1 ? "MULTI CUT" : "MISSILE CUT");
    }
    if (missileThreat)
    {
        const uint16_t warning = ((now / 110) & 1) ? RED : WHITE;
        s_frame.drawRect(1, 1, W - 2, H - 2, RED);
        s_frame.fillRect(CX - 55, 27, 110, 14, BLACK);
        s_frame.drawRect(CX - 55, 27, 110, 14, RED);
        s_frame.setTextColor(warning, BLACK);
        s_frame.setCursor(CX - 43, 30);
        s_frame.print("MISSILE ALERT");
    }
    if (s_bossMovePhase == BossMovePhase::CHARGE_WINDUP ||
        s_bossMovePhase == BossMovePhase::CHARGE)
    {
        const uint16_t warning = ((now / 85) & 1) ? RED : ORANGE;
        s_frame.drawRect(3, 3, W - 6, H - 6, warning);
        // Warning text is centralized in the lower status rail; the double
        // frame remains a strong, low-cost whole-screen danger cue.
    }
}

void updateBossMotion(float dt, uint32_t now)
{
    if (s_missionComplete) { s_bossBattleActive = false; return; }

    if (!s_bossEncounterStarted)
    {
        // Enter the first hover just inside the rival-adjusted cannon lock
        // envelope, avoiding a visible-but-unhittable opening beat.
        s_bossRange = max(17.4f, s_bossRange - max(4.0f, s_currentSpeed) * dt * 0.34f);
        if (s_bossRange <= 17.45f)
        {
            s_bossEncounterStarted = true;
            s_bossMovePhase = BossMovePhase::HOVER;
            s_bossPhaseStartMs = now;
            // Reserve an opening close-combat window before the first ram.
            s_bossNextChargeMs = now + random(4600, 8200);
            s_bossBattleActive = true;
            Serial.println("[MECHA BOSS] HOVER ENGAGE");
        }
        return;
    }

    s_bossBattleActive = true;
    uint32_t age = now - s_bossPhaseStartMs;
    float targetX = sinf(now * 0.00072f) * 1.45f;
    // Hovering is deliberately inside cannon range (15). This is the player's
    // clear damage window before the boss breaks away.
    const float pursuitClosure = constrain((s_currentSpeed - 12.0f) * 0.39f,
                                           0.0f, 7.8f);
    float targetRange = 12.7f + sinf(now * 0.00043f) * 0.8f
                      - pursuitClosure;
    float targetVerticalOffset = 0.0f;
    float response = 2.0f;

    if (s_bossMovePhase == BossMovePhase::HOVER)
    {
        // The boss itself enters blade range once per hover. A smooth
        // approach/hold/retreat creates a readable melee opportunity without
        // extending the plasma blade or silently increasing its lock range.
        const float closePhase = constrain((age - 1250.0f) / 2850.0f, 0.0f, 1.0f);
        const float closeApproach = sinf(closePhase * PI) * 5.6f;
        targetRange = max(6.4f, targetRange - closeApproach);
        if (closeApproach > 0.3f) response = 3.4f;
    }

    if (s_bossMovePhase == BossMovePhase::HOVER &&
        (int32_t)(now - s_bossNextChargeMs) >= 0)
    {
        s_bossMovePhase = BossMovePhase::CHARGE_WINDUP;
        s_bossPhaseStartMs = now;
        age = 0;
        Serial.println("[MECHA BOSS] RAM WARNING");
    }
    if (s_bossMovePhase == BossMovePhase::CHARGE_WINDUP)
    {
        // Pull back and visibly align before committing. The player can start
        // a roll or move off-axis during this telegraph.
        targetX = s_shipX;
        targetRange = 15.8f;
        targetVerticalOffset = 0.0f;
        response = 5.0f;
        if (age >= 720)
        {
            s_bossMovePhase = BossMovePhase::CHARGE;
            s_bossPhaseStartMs = now;
            s_bossChargeLineX = s_shipX;
            s_bossChargeLineAltitude = s_altitudeM;
            age = 0;
            Serial.println("[MECHA BOSS] RAM ATTACK");
        }
    }
    if (s_bossMovePhase == BossMovePhase::CHARGE)
    {
        const float u = constrain(age / 720.0f, 0.0f, 1.0f);
        // Lock the attack line at launch: normal lateral/altitude movement can
        // evade it, while a late roll grants the stronger collision guard.
        targetX = s_bossChargeLineX;
        targetRange = 15.0f - 13.4f * u;
        targetVerticalOffset = 0.0f;
        response = 11.0f;
        if (age >= 760)
        {
            s_bossMovePhase = BossMovePhase::RETURN;
            s_bossPhaseStartMs = now;
            s_bossRange = max(s_bossRange, 5.8f);
            age = 0;
            Serial.println("[MECHA BOSS] RAM PASS");
        }
    }
    if (s_bossMovePhase == BossMovePhase::HOVER && age >= 6200)
    {
        s_bossMovePhase = BossMovePhase::EXIT;
        s_bossPhaseStartMs = now;
        s_bossExitSide = ((now / 6200) & 1) ? -1 : 1;
        age = 0;
        Serial.printf("[MECHA BOSS] BREAK AWAY side=%d\n", s_bossExitSide);
    }
    if (s_bossMovePhase == BossMovePhase::EXIT)
    {
        const float u = constrain(age / 900.0f, 0.0f, 1.0f);
        // Leave through depth, not through a side curtain: the rival rapidly
        // shrinks toward the vanishing point before the escort wave arrives.
        targetX = sinf(now * 0.0014f) * 0.45f;
        targetRange = 12.5f + 29.0f * u;
        targetVerticalOffset = -0.35f * u;
        response = 5.2f;
        if (age >= 900)
        {
            s_bossMovePhase = BossMovePhase::WAVE;
            s_bossPhaseStartMs = now;
            s_bossLateral = 0.0f;
            s_bossRange = 42.0f;
            s_bossVerticalOffset = -0.35f;
            for (int i = 5; i < TARGET_COUNT; ++i)
            {
                s_targetHp[i] = 3;
                s_targetDestroyedUntilMs[i] = 0;
                s_enemyNextFireMs[i] = now + 500 + (i - 5) * 260;
            }
            s_enemyNextFireMs[MISSION_TARGET] = now + 180;
            age = 0;
            Serial.println("[MECHA BOSS] ESCORT WAVE");
        }
    }
    if (s_bossMovePhase == BossMovePhase::WAVE)
    {
        targetX = 0.0f;
        targetRange = 42.0f;
        targetVerticalOffset = -0.35f;
        response = 5.0f;
        int remaining = 0;
        for (int i = 5; i < TARGET_COUNT; ++i)
        {
            if (s_targetDestroyedUntilMs[i] == 0 && s_targetHp[i] > 0)
                ++remaining;
            else if (s_targetDestroyedUntilMs[i] != 0)
                s_targetDestroyedUntilMs[i] = s_bossPhaseStartMs + 12000;
        }
        if (remaining == 0 || age >= 8000)
        {
            for (int i = 5; i < TARGET_COUNT; ++i)
                s_targetDestroyedUntilMs[i] = now + 2500;
            s_bossMovePhase = BossMovePhase::ATTACK_PASS;
            s_bossPhaseStartMs = now;
            // Re-enter from below/behind the player, then overtake into the
            // forward hover slot. A small lateral bias preserves readable bank.
            s_bossLateral = -s_bossExitSide * 0.75f;
            s_bossRange = 3.4f;
            s_bossVerticalOffset = 2.85f;
            s_enemyNextFireMs[MISSION_TARGET] = now + 180;
            age = 0;
            Serial.printf("[MECHA BOSS] HIGH-SPEED ATTACK waveRemaining=%d\n",
                          remaining);
        }
    }
    if (s_bossMovePhase == BossMovePhase::ATTACK_PASS)
    {
        const float u = constrain(age / 1350.0f, 0.0f, 1.0f);
        targetX = s_bossExitSide * (0.55f - 1.0f * u)
                + sinf(u * PI) * s_bossExitSide * 0.55f;
        targetRange = 3.4f + 9.4f * u;
        targetVerticalOffset = 2.85f * (1.0f - u) * (1.0f - u);
        response = 6.5f;
        if (age >= 1350)
        {
            s_bossMovePhase = BossMovePhase::RETURN;
            s_bossPhaseStartMs = now;
            age = 0;
            Serial.println("[MECHA BOSS] RETURN TO HOVER");
        }
    }
    if (s_bossMovePhase == BossMovePhase::RETURN)
    {
        targetX = sinf(now * 0.00072f) * 1.15f;
        targetRange = 12.7f - pursuitClosure;
        targetVerticalOffset = 0.0f;
        response = 3.2f;
        if (age >= 1800)
        {
            s_bossMovePhase = BossMovePhase::HOVER;
            s_bossPhaseStartMs = now;
            s_bossNextChargeMs = now + random(4600, 8500);
            Serial.println("[MECHA BOSS] HOVER");
        }
    }

    const float targetAltitude = s_bossMovePhase == BossMovePhase::CHARGE
        ? s_bossChargeLineAltitude
        : s_altitudeM
        + sinf(now * 0.00058f) * 290.0f
        + sinf(now * 0.00121f) * 70.0f;
    s_bossLateral += (targetX - s_bossLateral) * min(1.0f, dt * response);
    s_bossRange += (targetRange - s_bossRange) * min(1.0f, dt * response * 0.9f);
    s_bossVerticalOffset += (targetVerticalOffset - s_bossVerticalOffset)
                          * min(1.0f, dt * response);
    s_bossAltitudeM += (targetAltitude - s_bossAltitudeM)
                     * min(1.0f, dt * 1.8f);
}

void updateBossCollision(uint32_t now)
{
    if (!s_bossBattleActive || s_bossMovePhase == BossMovePhase::EXIT ||
        s_bossMovePhase == BossMovePhase::WAVE ||
        (int32_t)(now - s_bossCollisionCooldownUntilMs) < 0) return;

    const bool attackPass = s_bossMovePhase == BossMovePhase::ATTACK_PASS;
    const bool ramAttack = s_bossMovePhase == BossMovePhase::CHARGE;
    const float dangerRange = ramAttack ? 7.0f : (attackPass ? 7.2f : 5.2f);
    const bool aligned = fabsf(s_bossLateral - s_shipX) < 0.82f &&
        fabsf(s_bossAltitudeM - s_altitudeM) < 520.0f &&
        fabsf(s_bossVerticalOffset) < ((attackPass || ramAttack) ? 1.65f : 0.75f);
    if (s_bossRange > dangerRange || !aligned) return;

    const uint32_t rollAge = now - s_rollStartMs;
    const bool rollGuard = s_rollActive && rollAge >= 80 && rollAge <= 560;
    s_bossCollisionCooldownUntilMs = now + 1300;
    if (rollGuard)
    {
        s_bossRange += 3.5f;
        Serial.println("[MECHA BOSS COLLISION] EVADED ROLL");
        return;
    }

    // A committed ram also permits a pure flight-path dodge. This keeps the
    // attack readable for players who move normally instead of spending roll.
    if (ramAttack && (fabsf(s_shipX - s_bossChargeLineX) > 0.72f ||
        fabsf(s_altitudeM - s_bossChargeLineAltitude) > 430.0f))
    {
        s_bossRange += 3.0f;
        Serial.println("[MECHA BOSS COLLISION] EVADED FLIGHT PATH");
        return;
    }

    if (PLAYER_HULL_ENABLED) s_playerHull = max(0, s_playerHull - 1);
    s_collisionShakeUntilMs = now + 720;
    s_collisionSlowUntilMs = now + 850;
    s_boostKick = -1.8f;
    s_bossRange += 4.5f;
    s_bossLateral += s_shipX <= s_bossLateral ? 1.4f : -1.4f;
    Serial.printf("[MECHA BOSS COLLISION] HIT hull=%d range=%.1f\n",
                  s_playerHull, s_bossRange);
}

void updateEnemyWeapons(uint32_t now)
{
    if (s_autoPilotEnabled && s_autoPilotWasDriving) return;
    int activeShots = 0;
    int activeMissiles = 0;
    for (EnemyShot& shot : s_enemyShots)
    {
        if (shot.startMs == 0 || now - shot.startMs >= shot.durationMs) continue;
        ++activeShots;
        if (enemyShotIsMissile(shot.kind)) ++activeMissiles;
        if (shot.kind != EnemyShotKind::HOMING_MISSILE) continue;
        const int shipX = constrain(CX + (int)(s_shipX * 19.0f), 42, W-42);
        const int shipY = constrain(153 + (int)(s_shipY * 18.0f), 102, H-16);
        shot.aimX += (shipX - shot.aimX) / 5;
        shot.aimY += (shipY - shot.aimY) / 5;
    }
    const uint32_t phaseAge = now - s_bossPhaseStartMs;
    const bool bossBarrageWindow = s_bossBattleActive &&
        (s_bossMovePhase == BossMovePhase::ATTACK_PASS ||
         (s_bossMovePhase == BossMovePhase::HOVER &&
          phaseAge >= 3500 && phaseAge < 5000));
    const bool majorThreatActive = s_rearMissileActive || s_interceptorActive ||
        s_bossMovePhase == BossMovePhase::CHARGE_WINDUP ||
        s_bossMovePhase == BossMovePhase::CHARGE;
    const int maxActiveShots = majorThreatActive ? 1 : (bossBarrageWindow ? 4 : 2);
    const int maxActiveMissiles = majorThreatActive ? 0 : (bossBarrageWindow ? 2 : 1);

    static const int shooters[] = {1, MISSION_TARGET, 3, 5, 7};
    for (int target : shooters)
    {
        if ((int32_t)(now - s_enemyQuietUntilMs) < 0) break;
        if (activeShots >= maxActiveShots) break;
        if (s_targetDestroyedUntilMs[target] != 0 ||
            s_targetScreenRange[target] < 4.0f ||
            s_targetScreenRange[target] > 34.0f ||
            s_targetScreenX[target] < 8 || s_targetScreenX[target] >= W-8 ||
            s_targetScreenY[target] < 8 || s_targetScreenY[target] >= H-8)
            continue;
        if ((int32_t)(now - s_enemyNextFireMs[target]) < 0) continue;

        EnemyShot& shot = s_enemyShots[s_nextEnemyShot];
        s_nextEnemyShot = (uint8_t)((s_nextEnemyShot + 1) % ENEMY_SHOT_MAX);
        shot.startMs = now;
        shot.startX = s_targetScreenX[target];
        shot.startY = s_targetScreenY[target];
        shot.aimX = constrain(CX + (int)(s_shipX * 19.0f), 42, W-42);
        shot.aimY = constrain(153 + (int)(s_shipY * 18.0f), 102, H-16);
        const uint32_t bossPhaseAge = now - s_bossPhaseStartMs;
        const bool bossBarrage = target == MISSION_TARGET &&
            (s_bossMovePhase == BossMovePhase::ATTACK_PASS ||
             (s_bossMovePhase == BossMovePhase::HOVER &&
              bossPhaseAge >= 3300 && bossPhaseAge < 5400));
        const int roll = random(0, 100);
        if (bossBarrage)
            shot.kind = roll < 44 ? EnemyShotKind::HOMING_MISSILE
                      : roll < 78 ? EnemyShotKind::DUMB_MISSILE
                                  : EnemyShotKind::CANNON;
        else if (target >= 5)
            shot.kind = roll < 32 ? EnemyShotKind::HOMING_MISSILE
                      : roll < 68 ? EnemyShotKind::DUMB_MISSILE
                                  : EnemyShotKind::CANNON;
        else if (target == MISSION_TARGET)
            shot.kind = roll < 24 ? EnemyShotKind::HOMING_MISSILE
                      : roll < 55 ? EnemyShotKind::DUMB_MISSILE
                                  : EnemyShotKind::CANNON;
        else
            shot.kind = roll < 15 ? EnemyShotKind::HOMING_MISSILE
                      : roll < 43 ? EnemyShotKind::DUMB_MISSILE
                                  : EnemyShotKind::CANNON;

        // Preserve one readable missile threat outside a boss barrage. If the
        // missile budget is already occupied, downgrade this shot to cannon.
        if (enemyShotIsMissile(shot.kind) && activeMissiles >= maxActiveMissiles)
            shot.kind = EnemyShotKind::CANNON;

        shot.durationMs = shot.kind == EnemyShotKind::HOMING_MISSILE ? random(980, 1320)
                        : shot.kind == EnemyShotKind::DUMB_MISSILE ? random(620, 850)
                        : random(430, 610);
        const uint32_t cadence = target == MISSION_TARGET
            ? (bossBarrage ? random(650, 980) : random(1800, 3000))
            : random(2400, 4600);
        s_enemyNextFireMs[target] = now + cadence;
        ++activeShots;
        if (enemyShotIsMissile(shot.kind)) ++activeMissiles;
        ++s_enemyVolleyShots;
        const uint8_t volleyLimit = bossBarrageWindow ? 5 : 3;
        if (s_enemyVolleyShots >= volleyLimit)
        {
            s_enemyVolleyShots = 0;
            s_enemyQuietUntilMs = now + (bossBarrageWindow
                ? random(1200, 1900) : random(1900, 3200));
        }
        const char* weaponName = shot.kind == EnemyShotKind::HOMING_MISSILE
            ? "HOMING" : shot.kind == EnemyShotKind::DUMB_MISSILE
            ? "DUMB_MISSILE" : "CANNON";
        Serial.printf("[MECHA ENEMY FIRE] target=%d weapon=%s next=%lums\n",
                      target, weaponName, (unsigned long)cadence);
    }

    // Plasma blades can physically intercept visible missiles. This is tied
    // to the active cutting portion of the animation, not simply to having the
    // weapon selected, so timing the swing remains an intentional defence.
    if (s_weapon == 4 && s_bladeSwingStartMs != 0)
    {
        const uint32_t bladeAge = now - s_bladeSwingStartMs;
        const uint32_t swingDuration = s_bladeSwingCharged
            ? BLADE_CHARGED_SWING_MS
            : (s_bladeSwingStage == 2 ? BLADE_SWING_MS : BLADE_QUICK_SWING_MS);
        const bool cuttingFrame = bladeAge >= 12 &&
            bladeAge <= swingDuration * 72 / 100;
        if (cuttingFrame)
        {
            const int bladeCenterX = constrain(CX + (int)(s_shipX * 25.0f), 38, W - 38);
            const int bladeCenterY = constrain(144 + (int)(s_shipY * 25.0f), 55, H - 24);
            const int cutRadius = s_bladeSwingCharged ? 94
                : (s_bladeSwingStage == 2 ? 80 : 62);
            const int cutRadius2 = cutRadius * cutRadius;
            uint8_t cutsThisFrame = 0;
            for (EnemyShot& shot : s_enemyShots)
            {
                if (shot.startMs == 0 || !enemyShotIsMissile(shot.kind)) continue;
                const uint32_t shotAge = now - shot.startMs;
                if (shotAge >= shot.durationMs) continue;
                const int p = (int)(shotAge * 255 / shot.durationMs);
                int missileX = shot.startX + (shot.aimX - shot.startX) * p / 255;
                const int missileY = shot.startY + (shot.aimY - shot.startY) * p / 255;
                if (shot.kind == EnemyShotKind::HOMING_MISSILE)
                {
                    const int side = (shot.startX & 1) ? -1 : 1;
                    missileX += side * (int)(sinf(p * PI / 255.0f) * 17.0f);
                }
                const int dx = missileX - bladeCenterX;
                const int dy = missileY - bladeCenterY;
                if (dx * dx + dy * dy > cutRadius2) continue;

                shot.startMs = 0; // prevents the later impact/damage pass
                s_missileCutX = missileX;
                s_missileCutY = missileY;
                s_missileCutStartMs = now;
                ++cutsThisFrame;
                if (!s_bladeSwingCharged && s_bladeSwingStage != 2) break;
            }
            if (cutsThisFrame > 0)
            {
                s_missileCutCount = (uint8_t)min(9,
                    (int)s_missileCutCount + (int)cutsThisFrame);
                s_externalHudDirty = true;
                Serial.printf("[MECHA BLADE] MISSILE CUT count=%u\n",
                              (unsigned)s_missileCutCount);
            }
        }
    }

    for (EnemyShot& shot : s_enemyShots)
    {
        if (shot.startMs == 0 || now - shot.startMs < shot.durationMs) continue;
        const uint32_t impactAge = now - shot.startMs - shot.durationMs;
        if (impactAge > 80) { shot.startMs = 0; continue; }
        const uint32_t rollAge = now - s_rollStartMs;
        const int currentShipX = constrain(CX + (int)(s_shipX * 19.0f), 42, W-42);
        const int currentShipY = constrain(153 + (int)(s_shipY * 18.0f), 102, H-16);
        const bool flightPathDodge = shot.kind == EnemyShotKind::DUMB_MISSILE &&
            (abs(currentShipX - shot.aimX) > 15 || abs(currentShipY - shot.aimY) > 13);
        const bool evaded = flightPathDodge ||
            (s_rollActive && rollAge >= 80 && rollAge <= 560) ||
            (shot.kind == EnemyShotKind::HOMING_MISSILE &&
             (s_manualLoopActive ||
              (s_boostActive && s_throttlePercent >= 135.0f &&
               s_currentSpeed >= 28.0f)));
        if (!evaded)
        {
            if (PLAYER_HULL_ENABLED) s_playerHull = max(0, s_playerHull - 1);
            s_collisionShakeUntilMs = now + (enemyShotIsMissile(shot.kind) ? 720 : 420);
            s_collisionSlowUntilMs = now + 650;
            if (enemyShotIsMissile(shot.kind))
                s_rearMissileHitStartMs = now;
            const char* hitName = shot.kind == EnemyShotKind::HOMING_MISSILE
                ? "HOMING" : shot.kind == EnemyShotKind::DUMB_MISSILE
                ? "DUMB_MISSILE" : "CANNON";
            Serial.printf("[MECHA ENEMY HIT] weapon=%s hull=%d\n",
                          hitName, s_playerHull);
        }
        else Serial.printf("[MECHA ENEMY SHOT] EVADED %s\n",
                           flightPathDodge ? "FLIGHT PATH" : "MANEUVER");
        shot.startMs = 0;
    }
}

void drawTargets()
{
    const uint32_t now = millis();
    float routeNowX, routeNowY;
    routePosition(s_travel, &routeNowX, &routeNowY);
    const float bossRange = s_bossRange;
    s_bossBattleActive = false;
    // Proximity only creates a selectable candidate. It never changes the
    // active lock until the player presses FIRE.
    int bestDistance2 = 80 * 80;
    s_lockCandidate = -1;
    bool lockedTargetVisible = false;
    s_lockedTargetHitEligible = false;
    s_lockedAimErrorPx = 999;
    const bool bossWave = s_bossMovePhase == BossMovePhase::WAVE;
    const bool showMultiFormation = bossWave || s_weapon == 3 ||
                                    s_multiLockCharging || multiVolleyActive() ||
                                    s_autoPilotEnabled;
    for (int i = 0; i < TARGET_COUNT; ++i)
    {
        if (i == MISSION_TARGET) continue;
        s_targetScreenRange[i] = 999.0f;
        // The extra four contacts exist specifically for MULTI testing. Do not
        // pay their projection/drawing cost during the other three weapons.
        if (i >= 5 && !showMultiFormation) continue;
        s_targetBeamEligible[i] = false;
        s_targetLockEligible[i] = false;
    }

    for (int i = 0; i < TARGET_COUNT; ++i)
    {
        // The boss owns the combat frame. Removing minor contacts here keeps
        // both the silhouette and incoming fire readable, and gives back the
        // projection/drawing budget used by the detailed rival machine.
        if (s_bossBattleActive)
        {
            if (bossWave && (i < 5 || i == MISSION_TARGET)) continue;
            if (!bossWave && i != MISSION_TARGET) continue;
        }
        if (s_targetDestroyedUntilMs[i] != 0)
        {
            if (i == MISSION_TARGET ||
                (int32_t)(now - s_targetDestroyedUntilMs[i]) < 0)
                continue;
            s_targetDestroyedUntilMs[i] = 0;
            s_targetHp[i] = NORMAL_TARGET_HP;
        }
        if (i == MISSION_TARGET) continue;
        if (i == INTERCEPTOR_TARGET && !s_interceptorActive) continue;

        float z, x, y;
        bool ground;
        float targetAltitudeM;
        if (i == INTERCEPTOR_TARGET)
        {
            z = max(0.7f, s_interceptorZ);
            ground = false;
            x = (s_interceptorX - s_shipX) * 3.2f;
            y = (s_interceptorY - s_shipY) * 2.8f;
            targetAltitudeM = s_altitudeM;
        }
        else if (i == MISSION_TARGET)
        {
            // Rival BOARD: a slower central approach keeps its high-detail
            // silhouette readable and gives its ranged attacks room to work.
            z = bossRange;
            float routeX, routeY; routePosition(s_travel + z, &routeX, &routeY);
            ground = false;
            x = routeX - routeNowX + s_bossLateral;
            targetAltitudeM = s_bossAltitudeM;
            y = routeY - routeNowY
              + (s_altitudeM - targetAltitudeM) * ALTITUDE_TO_WORLD
              + s_bossVerticalOffset;
        }
        else if (i >= 5)
        {
            // Multi-lock test formation: four contacts cross the same range
            // band together, giving the player a stable sweep-and-mark window.
            const int formation = i - 5;
            if (bossWave)
            {
                const float waveAge = (now - s_bossPhaseStartMs) * 0.001f;
                z = max(6.0f, 29.0f - waveAge * 2.8f + formation * 0.8f);
            }
            else if (s_autoPilotEnabled && s_autoPilotLastWeaponMs != 0)
            {
                // Each seven-second weapon act receives its own formation:
                // distant entrance, readable firing pass, then close fly-by.
                const float actAge = (now - s_autoPilotLastWeaponMs) * 0.001f;
                z = max(5.5f, 34.0f - actAge * 4.0f + formation * 0.75f);
            }
            else
            {
                z = fmodf(43.0f + formation * 1.4f - s_travel, 74.0f);
                if (z < 0.25f) z += 74.0f;
            }
            float routeX, routeY; routePosition(s_travel + z, &routeX, &routeY);
            static const float formationLane[4] = { -2.7f, -0.9f, 0.9f, 2.7f };
            ground = false;
            x = routeX - routeNowX + formationLane[formation];
            targetAltitudeM = 1500.0f + formation * 160.0f;
            y = routeY - routeNowY
              + (s_altitudeM - targetAltitudeM) * ALTITUDE_TO_WORLD;
        }
        else
        {
            z = fmodf(16.0f + i * 13.0f - s_travel, 62.0f);
            if (z < 0.25f) z += 62.0f;
            float routeX, routeY; routePosition(s_travel + z, &routeX, &routeY);
            ground = (i & 1) == 0;
            x = routeX - routeNowX + ((i & 2) ? 2.4f : -2.0f);
            targetAltitudeM = ground ? 0.0f : (1500.0f + i * 450.0f);
            y = routeY - routeNowY
              + (s_altitudeM - targetAltitudeM) * ALTITUDE_TO_WORLD;
            if (!ground)
            {
                // Air contacts begin on distinct lanes, then commit toward
                // the course center below range 18. The player must leave the
                // centerline or destroy them instead of watching them slide by.
                const float commit = constrain((18.0f - z) / 17.3f, 0.0f, 0.88f);
                x *= 1.0f - commit;
                y *= 1.0f - commit;
            }
        }
        // Every target has an altitude. Ground targets remain at terrain level;
        // aerial targets fly at fixed bands, so climbing past one makes it
        // appear below the reticle instead of staying glued to the screen.
        // Ground radar loses the depression angle at high altitude. Airborne
        // targets require the aircraft to climb into their acquisition band;
        // higher targets deliberately require a little more climb.
        const float airAcquireAltitudeM =
            max(AIR_LOCK_MIN_ALTITUDE_M, targetAltitudeM - 900.0f);
        const bool altitudeEligible = i == INTERCEPTOR_TARGET ? true : (ground
            ? s_altitudeM <= GROUND_LOCK_MAX_ALTITUDE_M
            : s_altitudeM >= airAcquireAltitudeM);
        // Blade lock-on covers the charged lunge envelope, not only the
        // stationary blade length. Boss acquisition also receives a modest
        // silhouette allowance matching its larger on-screen body.
        float acquisitionRange = currentWeaponRange();
        if (i == MISSION_TARGET)
        {
            if (s_weapon == 0) acquisitionRange += 2.5f;
            else if (s_weapon == 1 || s_weapon == 3) acquisitionRange += 3.0f;
        }
        const bool inRange = z <= acquisitionRange;
        const bool lockEligible = altitudeEligible && inRange;
        Point2 p;
        if (!project(x, y, z, &p) || p.x < 5 || p.x >= W-5 || p.y < 10 || p.y >= H-8) continue;

        s_targetScreenX[i] = p.x;
        s_targetScreenY[i] = p.y;
        s_targetScreenRange[i] = z;
        s_targetScreenGround[i] = ground;
        const bool bossWindowOpen = i != MISSION_TARGET || bossDamageWindowOpen(now);
        s_targetBeamEligible[i] = altitudeEligible && z <= BEAM_RANGE && bossWindowOpen;
        s_targetLockEligible[i] = lockEligible;

        const uint16_t c = lockEligible
            ? (ground ? 0xFD20 : 0x07FF)
            : (ground ? 0x4100 : 0x0186);
        if (ground)
        {
            const int size = z < 15 ? 7 : 4;
            s_frame.drawRect(p.x-size, p.y-size/2, size*2, size, c);
            s_frame.drawLine(p.x-size-3, p.y, p.x+size+3, p.y, c);
        }
        else
        {
            const int size = i == INTERCEPTOR_TARGET
                ? constrain(16 - (int)(z * 0.32f), 5, 15)
                : (z < 15 ? 7 : 4);
            if (i == MISSION_TARGET)
            {
                const int bossSize = constrain(35 - (int)(z * 0.62f), 10, 27);
                drawBossMecha(p.x, p.y, bossSize, now);
            }
            else s_frame.drawTriangle(p.x, p.y-size, p.x-size, p.y+size,
                                      p.x+size, p.y+size, c);
            if (i == INTERCEPTOR_TARGET)
            {
                s_frame.drawLine(p.x-size, p.y+size, p.x, p.y+size/3, RED);
                s_frame.drawLine(p.x+size, p.y+size, p.x, p.y+size/3, RED);
                if (z <= 22.0f)
                {
                    const uint16_t warning = ((now / 120) & 1) ? RED : ORANGE;
                    s_frame.drawRect(p.x-size-4, p.y-size-4,
                                     size*2+8, size*2+8, warning);
                    s_frame.setTextColor(warning, BLACK);
                    s_frame.setCursor(CX - 23, 27);
                    s_frame.print(z < 8.0f ? "BREAK!" : "WARNING");
                }
            }
        }

        if (i == MISSION_TARGET)
        {
            const int objectiveSize = z < 15.0f ? 10 : 6;
            const int bossAimDx = p.x - (int)s_freeAimX;
            const int bossAimDy = p.y - (int)s_freeAimY;
            const int bossAimDistance2 = bossAimDx*bossAimDx + bossAimDy*bossAimDy;
            const bool attackWeapon = s_weapon <= 4;
            bool weaponInRange = lockEligible;
            int weaponAimRadius = currentWeaponCaptureRadius(z, false) + 10;
            if (s_weapon == 2)
            {
                // Beam contact is continuous and does not require a stored
                // lock, so mirror the exact boss sweep radius.
                weaponInRange = s_targetBeamEligible[i];
                weaponAimRadius = 32;
            }
            else if (s_weapon == 4 && s_bladeCharging)
            {
                // Charged blade uses the longer dash envelope.
                weaponInRange = altitudeEligible && z <= BLADE_DASH_RANGE;
                weaponAimRadius = 64;
            }
            const bool bossGuarded = !bossDamageWindowOpen(now);
            const bool weaponContact = attackWeapon && !bossGuarded && weaponInRange &&
                bossAimDistance2 <= weaponAimRadius * weaponAimRadius;
            // One shared, high-contrast language across every attack weapon:
            // green can hit, yellow needs alignment, red cannot reach.
            const uint16_t objectiveColor = !attackWeapon ? 0x8410
                : bossGuarded ? MAGENTA
                : weaponContact ? GREEN : weaponInRange ? YELLOW : RED;
            s_frame.drawRect(p.x - objectiveSize, p.y - objectiveSize,
                             objectiveSize * 2 + 1, objectiveSize * 2 + 1,
                             objectiveColor);
            s_frame.drawPixel(p.x, p.y, WHITE);
            if (attackWeapon)
            {
                const char* state = bossGuarded ? "GUARD"
                                  : weaponContact ? "CONTACT"
                                  : weaponInRange ? "ALIGN" : "RANGE";
                const int labelW = weaponContact ? 45 : 31;
                const int labelX = constrain(p.x - labelW / 2, 2, W - labelW - 2);
                const int labelY = constrain(p.y + objectiveSize + 3, 17, H - 12);
                s_frame.fillRect(labelX, labelY, labelW, 9, BLACK);
                s_frame.setTextColor(objectiveColor, BLACK);
                s_frame.setCursor(labelX + 2, labelY + 1);
                s_frame.print(state);
            }
            else
            {
                const int labelX = constrain(p.x - 22, 2, W - 46);
                const int labelY = constrain(p.y + objectiveSize + 3, 17, H - 12);
                s_frame.fillRect(labelX, labelY, 46, 9, BLACK);
                s_frame.setTextColor(0x8410, BLACK);
                s_frame.setCursor(labelX + 2, labelY + 1);
                s_frame.print("DEFENSE");
            }

            if (s_playerHitConfirmTarget == MISSION_TARGET &&
                (int32_t)(s_playerHitConfirmUntilMs - now) > 0)
            {
                const uint32_t remaining = s_playerHitConfirmUntilMs - now;
                const int ring = 9 + (460 - remaining) / 34;
                const uint16_t hitColor = ((now / 55) & 1) ? WHITE : YELLOW;
                s_frame.drawCircle(p.x, p.y, ring, hitColor);
                s_frame.drawLine(p.x-ring-4, p.y, p.x-ring+1, p.y, hitColor);
                s_frame.drawLine(p.x+ring-1, p.y, p.x+ring+4, p.y, hitColor);
                s_frame.drawLine(p.x, p.y-ring-4, p.x, p.y-ring+1, hitColor);
                s_frame.drawLine(p.x, p.y+ring-1, p.x, p.y+ring+4, hitColor);
                const int hitLabelX = constrain(p.x - 22, 2, W - 47);
                const int hitLabelY = constrain(p.y - objectiveSize - 14, 17, H - 12);
                s_frame.fillRect(hitLabelX, hitLabelY, 47, 10, BLACK);
                s_frame.setTextColor(hitColor, BLACK);
                s_frame.setCursor(hitLabelX + 4, hitLabelY + 2);
                s_frame.printf("HIT -%d", s_playerHitConfirmDamage);
            }
        }

        if (s_weapon == 3 && s_multiLocked[i])
        {
            const int r = 9 + ((now / 120 + i) & 1);
            s_frame.drawRect(p.x-r, p.y-r, r*2, r*2, MAGENTA);
            s_frame.drawPixel(p.x-2, p.y, WHITE);
            s_frame.drawPixel(p.x+2, p.y, WHITE);
            s_frame.drawPixel(p.x, p.y-2, WHITE);
            s_frame.drawPixel(p.x, p.y+2, WHITE);
        }

        // A small cross tells the player that the contact is visible but is
        // still beyond weapon acquisition range.
        if (altitudeEligible && !inRange)
        {
            s_frame.drawLine(p.x-3, p.y-3, p.x+3, p.y+3, 0x7800);
            s_frame.drawLine(p.x+3, p.y-3, p.x-3, p.y+3, 0x7800);
        }

        const int dx = p.x - (int)s_freeAimX;
        const int dy = p.y - (int)s_freeAimY;
        const int distance2 = dx*dx + dy*dy;
        const bool priorityThreat = i == INTERCEPTOR_TARGET && z <= 22.0f;
        const int captureRadius = currentWeaponCaptureRadius(z, priorityThreat)
                                + (i == MISSION_TARGET ? 10 : 0);
        if (lockEligible && distance2 < captureRadius * captureRadius &&
            (priorityThreat || distance2 < bestDistance2))
        {
            bestDistance2 = distance2;
            s_lockCandidate = i;
            s_lockCandidateX = p.x;
            s_lockCandidateY = p.y;
            s_lockCandidateGround = ground;
            s_lockCandidateRange = z;
        }
        if (lockEligible && i == s_lockedTarget)
        {
            lockedTargetVisible = true;
            s_lockedTargetHitEligible = true;
            s_lockedTargetX = p.x;
            s_lockedTargetY = p.y;
            s_lockedTargetGround = ground;
            s_lockedTargetRange = z;
            s_lockedAimErrorPx = (int)sqrtf((float)distance2);
        }
    }

    if (!bossWave && !s_missionComplete &&
        s_targetDestroyedUntilMs[MISSION_TARGET] == 0 &&
        s_targetScreenRange[MISSION_TARGET] < 45.0f)
    {
        const int barX = CX - 58, barY = 4, barW = 116;
        const bool bossHitFlash = s_playerHitConfirmTarget == MISSION_TARGET &&
            (int32_t)(s_playerHitConfirmUntilMs - now) > 0;
        const uint16_t bossBarColor = bossHitFlash && ((now / 55) & 1)
            ? WHITE : MAGENTA;
        s_frame.fillRect(barX, barY, barW, 11, BLACK);
        s_frame.drawRect(barX, barY, barW, 11, bossBarColor);
        const int hpW = (barW - 4) * s_targetHp[MISSION_TARGET] / BOSS_TARGET_HP;
        if (hpW > 0) s_frame.fillRect(barX + 2, barY + 2, hpW, 3, RED);
        s_frame.setTextColor(WHITE, BLACK);
        s_frame.setCursor(CX - 28, barY + 3);
        s_frame.print("RIVAL BOARD");
        if (s_targetScreenRange[MISSION_TARGET] <= BLADE_RANGE)
        {
            const uint16_t closeColor = s_targetScreenRange[MISSION_TARGET] <= 5.2f
                ? RED : CYAN;
            s_frame.fillRect(CX - 39, 16, 78, 10, BLACK);
            s_frame.setTextColor(closeColor, BLACK);
            s_frame.setCursor(CX - 32, 18);
            s_frame.print(closeColor == RED ? "COLLISION!" : "BLADE WINDOW");
        }
    }
    else if (bossWave)
    {
        int remaining = 0;
        for (int i = 5; i < TARGET_COUNT; ++i)
            if (s_targetDestroyedUntilMs[i] == 0 && s_targetHp[i] > 0) ++remaining;
        s_frame.fillRect(CX - 43, 4, 86, 11, BLACK);
        s_frame.drawRect(CX - 43, 4, 86, 11, RED);
        s_frame.setTextColor(WHITE, BLACK);
        s_frame.setCursor(CX - 35, 7);
        s_frame.printf("ESCORT WAVE %d", remaining);
    }

    drawEnemyShots(now);

    if (s_lockedTarget >= 0 && !lockedTargetVisible)
    {
        Serial.printf("[MECHA LOCK] LOST target=%d\n", s_lockedTarget);
        s_lockedTarget = -1;
        s_lockedTargetHitEligible = false;
        s_lockedAimErrorPx = 999;
        s_externalHudDirty = true;
    }

    // Yellow corner marks show which target FIRE will acquire. The reticle
    // itself remains entirely driven by the ball/flight direction.
    if (s_lockCandidate >= 0 && s_lockCandidate != s_lockedTarget)
    {
        const int x = s_lockCandidateX, y = s_lockCandidateY, r = 11;
        s_frame.drawLine(x-r, y-r, x-4, y-r, YELLOW);
        s_frame.drawLine(x-r, y-r, x-r, y-4, YELLOW);
        s_frame.drawLine(x+r, y-r, x+4, y-r, YELLOW);
        s_frame.drawLine(x+r, y-r, x+r, y-4, YELLOW);
        s_frame.drawLine(x-r, y+r, x-4, y+r, YELLOW);
        s_frame.drawLine(x-r, y+r, x-r, y+4, YELLOW);
        s_frame.drawLine(x+r, y+r, x+4, y+r, YELLOW);
        s_frame.drawLine(x+r, y+r, x+r, y+4, YELLOW);
    }

    drawEnemyDestruction();
}

void drawFlightSpace(bool boost)
{
    const uint32_t starNow = millis();
    bool hostileProjectileActive = s_rearMissileActive || s_interceptorActive ||
        s_bossMovePhase == BossMovePhase::CHARGE_WINDUP ||
        s_bossMovePhase == BossMovePhase::CHARGE;
    for (const EnemyShot& shot : s_enemyShots)
    {
        if (shot.startMs != 0 && starNow - shot.startMs < shot.durationMs)
        {
            hostileProjectileActive = true;
            break;
        }
    }
    // Smoothly lower background salience while hostile projectiles are on
    // screen. Star depth continues updating, so speed sensation returns
    // immediately after the short combat window without a visual jump.
    const float starDimTarget = hostileProjectileActive ? 1.0f : 0.0f;
    s_starCombatDim += (starDimTarget - s_starCombatDim) *
                       (hostileProjectileActive ? 0.26f : 0.10f);
    const int starStep = s_starCombatDim > 0.65f ? 3
                       : s_starCombatDim > 0.18f ? 2 : 1;

    // Daytime flight rendering is temporarily hidden. Keep the renderer below
    // intact so it can be restored later without rebuilding the stage art.
    const bool daySky = false;
    if (daySky)
    {
        // Eight broad bands are cheaper than per-line gradients and remain
        // visually smooth on the 256x192 viewport.
        static const uint8_t skyRgb[8][3] = {
            {35, 112, 188}, {45, 132, 205}, {58, 151, 218}, {76, 170, 228},
            {101, 187, 234}, {132, 202, 237}, {166, 216, 239}, {194, 226, 241}
        };
        for (int band = 0; band < 8; ++band)
        {
            const uint16_t c = M5.Display.color565(
                skyRgb[band][0], skyRgb[band][1], skyRgb[band][2]);
            s_frame.fillRect(0, band * 24, W, 24, c);
        }
        // Sun and restrained cloud silhouettes stay outside the central
        // combat lane. Clouds are reduced further during enemy fire.
        s_frame.fillCircle(W - 32, 36, 10, 0xFFE0);
        s_frame.fillCircle(W - 32, 36, 6, WHITE);
        const int cloudCount = hostileProjectileActive ? 1 : 3;
        for (int i = 0; i < cloudCount; ++i)
        {
            int cloudX = (int)fmodf(i * 103.0f - s_travel * (1.2f + i * 0.2f),
                                   W + 80.0f) - 35;
            if (cloudX < -45) cloudX += W + 80;
            const int cloudY = 72 + i * 39;
            const uint16_t shade = i == 0 ? 0xD71C : 0xC69B;
            s_frame.fillCircle(cloudX, cloudY, 8, shade);
            s_frame.fillCircle(cloudX + 10, cloudY - 3, 10, shade);
            s_frame.fillCircle(cloudX + 21, cloudY + 1, 7, shade);
            s_frame.fillRect(cloudX, cloudY, 22, 7, shade);
        }
    }
    else
    {
        s_frame.fillScreen(BLACK);
    }

    const float totalBank = s_cameraBank + s_routeBank * AUTO_ROUTE_CAMERA + s_courseBank;
    const float actionCos = cosf(s_actionCameraRoll);
    const float actionSin = sinf(s_actionCameraRoll);
    const int flowCx = CX + (int)(s_routeLeadX * 2.5f);
    const int flowCy = HORIZON_Y + (int)(s_routeLeadY * 2.0f)
                     + (int)((s_cameraPitch + s_routePitch * AUTO_ROUTE_CAMERA
                              + s_coursePitch) * PROJECTION);

    // TheExtend cockpit asset: persistent depth stars with a projected tail.
    // Day maps retain depth through cloud parallax and do not draw stars.
    for (int i = 0; !daySky && i < STAR_COUNT; i += starStep)
    {
        const float inv = 0.65f / s_stars[i].z;
        // Collapse projected tails to points while hovering. This keeps stars
        // visible as a stationary reference without suggesting forward motion.
        const float tailDepth = s_throttlePercent <= 0.01f ? 0.0f
            : 0.055f * (1.0f - s_starCombatDim * 0.68f);
        const float prevInv = 0.65f / (s_stars[i].z + tailDepth);
        const int rawSx = flowCx + (int)(s_stars[i].x * inv * (W / 2));
        const int rawSy = flowCy + (int)(s_stars[i].y * inv * (H / 2));
        const float starDx = rawSx - flowCx;
        const float starDy = rawSy - flowCy;
        const int sx = flowCx + (int)(starDx * actionCos - starDy * actionSin);
        const int rolledSy = flowCy + (int)(starDx * actionSin + starDy * actionCos);
        const int sy = flowCy + (int)((rolledSy - flowCy) * s_courseLoopCos
                                     + s_courseLoopSin * 72.0f);
        if (sx < 0 || sx >= W || sy < 24 || sy >= H - 12) continue;
        // During a barrage reserve a clean central combat lane for targets,
        // warning icons and projectile silhouettes. Peripheral stars remain
        // visible, preserving motion and depth without resembling enemy fire.
        if (s_starCombatDim > 0.22f && sx > 48 && sx < W - 48 &&
            sy > 34 && sy < H - 18) continue;
        const int rawPx = flowCx + (int)(s_stars[i].x * prevInv * (W / 2));
        const int rawPy = flowCy + (int)(s_stars[i].y * prevInv * (H / 2));
        const float prevDx = rawPx - flowCx;
        const float prevDy = rawPy - flowCy;
        const int px = flowCx + (int)(prevDx * actionCos - prevDy * actionSin);
        const int rolledPy = flowCy + (int)(prevDx * actionSin + prevDy * actionCos);
        const int py = flowCy + (int)((rolledPy - flowCy) * s_courseLoopCos
                                     + s_courseLoopSin * 72.0f);
        const float nearLevel = 1.0f - constrain((s_stars[i].z - 0.055f) / 0.945f, 0.0f, 1.0f);
        const float combatBrightness = 1.0f - s_starCombatDim * 0.62f;
        const uint8_t v = (uint8_t)((65 + nearLevel * 190.0f) * combatBrightness);
        const uint16_t color = M5.Display.color565(v, v, v);
        s_frame.drawLine(px, py, sx, sy, color);
        s_frame.drawPixel(sx, sy, color);
    }

    if (!tunnelActiveAt(courseDistance())) drawWorldAnchors();
    drawTunnelSection(starNow);
    if (!tunnelActiveAt(courseDistance())) drawTargets();

    if (daySky)
    {
        s_frame.setTextColor(0xFFFF, 0x3C59);
        s_frame.setCursor(5, 27);
        s_frame.print("DAY SKY");
    }

    // Draw far gates first so near rings dominate naturally.
    if (s_routeGuideEnabled && !tunnelActiveAt(courseDistance()))
    {
        const bool heavyManeuver = fabsf(s_courseBank) > 0.14f ||
                                   fabsf(s_coursePitch) > 0.11f ||
                                   fabsf(s_cameraBank) > 0.40f ||
                                   fabsf(s_cameraPitch) > 0.40f;
        const int farthestGate = heavyManeuver ? 4 : 7;
        for (int order = farthestGate; order >= 0; --order)
        {
            float z = fmodf((float)order * 4.2f + 3.0f - s_travel, 50.4f);
            if (z < 1.2f) z += 50.4f;
            drawGate(z, order);
        }
    }

    // Boost is expressed by real star depth motion, FOV and engine exhaust.
    // The former full-screen radial lines duplicated that motion and looked
    // like a flat overlay, so they are intentionally omitted.
}

void drawShipAndHud(bool boost, float speed)
{
    const uint32_t drawNow = millis();
    const bool collisionShake = (int32_t)(drawNow - s_collisionShakeUntilMs) < 0;
    const int shakeX = collisionShake ? (((drawNow / 28) & 1) ? 4 : -4) : 0;
    const int shakeY = collisionShake ? (((drawNow / 41) & 1) ? 3 : -3) : 0;
    int bladeMotionX = 0;
    int bladeMotionY = 0;
    float bladePoseYaw = 0.0f;
    float bladeActionScale = 1.0f;
    const bool bladeComboHold = s_weapon == 4 && s_bladeDashTargetX >= 0 &&
        s_bladeComboStep != 0 &&
        drawNow - s_bladeLastReleaseMs <= BLADE_COMBO_RESET_MS;
    if (s_weapon == 4 && (s_bladeSwingStartMs != 0 || bladeComboHold))
    {
        const bool swingActive = s_bladeSwingStartMs != 0;
        const uint32_t age = swingActive ? drawNow - s_bladeSwingStartMs : 0;
        const uint32_t duration = s_bladeSwingCharged ? BLADE_CHARGED_SWING_MS
            : (s_bladeSwingStage == 2 ? BLADE_SWING_MS : BLADE_QUICK_SWING_MS);
        const float p = swingActive
            ? constrain(age / (float)max<uint32_t>(1, duration), 0.0f, 1.0f)
            : 1.0f;
        const int neutralX = CX + (int)(s_shipX * 25.0f) + shakeX;
        const int neutralY = 153 + (int)(s_shipY * 25.0f)
                           + (int)(s_boostKick * 4.0f) + shakeY;
        const bool lockedCombo = s_bladeDashTargetX >= 0;
        const int destinationX = lockedCombo
            ? constrain(s_bladeDashTargetX, 55, W - 55) : neutralX;
        const int destinationY = lockedCombo
            ? (s_bladeSwingCharged
                ? constrain(s_bladeDashTargetY + 8, 38, 127)
                : constrain(s_bladeDashTargetY + 25, 55, 139)) : neutralY;
        const int dashX = destinationX - neutralX;
        const int dashY = destinationY - neutralY;
        if (s_bladeSwingStage == 0)
        {
            if (lockedCombo)
            {
                const float q = min(1.0f, p / 0.42f);
                const float warp = q * q * (3.0f - 2.0f * q);
                bladeMotionX = (int)(dashX * warp);
                bladeMotionY = (int)(dashY * warp);
                bladeActionScale = 0.90f + warp * 0.10f;
            }
            else
            {
                bladeMotionX = (int)(-3.0f + p * 7.0f);
                bladeMotionY = (int)(sinf(p * 3.14159f) * -3.0f);
            }
            bladePoseYaw = 0.18f - p * 0.38f;
        }
        else if (s_bladeSwingStage == 1)
        {
            bladeMotionX = dashX + (swingActive ? (int)(-6.0f + p * 12.0f) : 0);
            bladeMotionY = dashY;
            bladePoseYaw = -0.28f + p * 0.56f;
        }
        else
        {
            const bool comboFinisher = !s_bladeSwingCharged && lockedCombo;
            if (comboFinisher)
            {
                if (p < 0.24f)
                {
                    const float recoil = p / 0.24f;
                    bladeMotionX = dashX;
                    bladeMotionY = dashY + (int)(recoil * 12.0f);
                    bladeActionScale = 1.0f - recoil * 0.12f;
                }
                else if (p < 0.80f)
                {
                    const float strike = (p - 0.24f) / 0.56f;
                    bladeMotionX = dashX;
                    bladeMotionY = dashY + 12 - (int)(strike * 21.0f);
                    bladeActionScale = 0.88f + strike * 0.28f;
                }
                else
                {
                    const float q = (p - 0.80f) / 0.20f;
                    const float back = 1.0f - q * q * (3.0f - 2.0f * q);
                    bladeMotionX = (int)(dashX * back);
                    bladeMotionY = (int)((dashY - 9) * back);
                    bladeActionScale = 1.0f + back * 0.08f;
                }
            }
            else if (s_bladeSwingCharged)
            {
                // The charged strike has a deliberate wind-up, a long dash
                // through the lock/aim point, then a short overrun before
                // returning. It must not feel like combo hit three.
                if (p < 0.14f)
                {
                    const float recoil = p / 0.14f;
                    bladeMotionY = (int)(recoil * 14.0f);
                    bladeActionScale = 1.0f - recoil * 0.13f;
                }
                else if (p < 0.63f)
                {
                    const float q = (p - 0.14f) / 0.49f;
                    const float warp = q * q * (3.0f - 2.0f * q);
                    bladeMotionX = (int)(dashX * warp);
                    bladeMotionY = 14 + (int)((dashY - 26) * warp);
                    bladeActionScale = 0.87f + warp * 0.25f;
                }
                else if (p < 0.84f)
                {
                    bladeMotionX = dashX;
                    bladeMotionY = dashY - 12;
                    bladeActionScale = 1.12f;
                }
                else
                {
                    const float q = (p - 0.84f) / 0.16f;
                    const float back = 1.0f - q * q * (3.0f - 2.0f * q);
                    bladeMotionX = (int)(dashX * back);
                    bladeMotionY = (int)((dashY - 12) * back);
                    bladeActionScale = 1.0f + back * 0.12f;
                }
            }
            else if (p < 0.20f)
            {
                const float recoil = p / 0.20f;
                bladeMotionY = (int)(recoil * 12.0f);
                bladeActionScale = 1.0f - recoil * 0.14f;
            }
            else if (p < 0.48f)
            {
                const float q = (p - 0.20f) / 0.28f;
                const float warp = q * q * (3.0f - 2.0f * q);
                bladeMotionX = (int)(dashX * warp);
                bladeMotionY = (int)(12.0f + (dashY - 12.0f) * warp);
                bladeActionScale = 0.86f + warp * 0.12f;
            }
            else if (p < 0.80f)
            {
                const float strike = (p - 0.48f) / 0.32f;
                bladeMotionX = dashX;
                bladeMotionY = dashY - (int)(sinf(strike * 3.14159f) * 9.0f);
                bladeActionScale = 0.98f + sinf(strike * 3.14159f) * 0.18f;
            }
            else
            {
                const float q = (p - 0.80f) / 0.20f;
                const float back = 1.0f - q * q * (3.0f - 2.0f * q);
                bladeMotionX = (int)(dashX * back);
                bladeMotionY = (int)(dashY * back);
                bladeActionScale = 1.0f + back * 0.08f;
            }
            bladePoseYaw = sinf(p * 3.14159f) *
                (s_bladeSwingCharged ? 0.56f : 0.38f);
        }
    }
    const int sx = CX + (int)(s_shipX * 25.0f) + shakeX + bladeMotionX;
    const int beamRecoil = s_beamState == BeamState::FIRING
        ? 6 + (int)(sinf((millis() - s_beamStateSinceMs) * 0.045f) * 2.0f)
        : 0;
    int flareLunge = 0;
    if (s_flareLungeStartMs != 0)
    {
        const uint32_t age = drawNow - s_flareLungeStartMs;
        if (age < 100) flareLunge = -(int)(age * 13 / 100);
        else if (age < 390) flareLunge = -(int)((390 - age) * 13 / 290);
    }
    const int sy = 153 + (int)(s_shipY * 25.0f) + (int)(s_boostKick * 4.0f)
                 + beamRecoil + shakeY + bladeMotionY + flareLunge;
    // MULTI always shows the movable scan reticle; individual stored locks
    // already have their own purple boxes.
    const int aimX = s_weapon == 3 ? (int)s_freeAimX
                   : (s_lockedTarget >= 0 ? s_lockedTargetX : (int)s_freeAimX);
    const int aimY = s_weapon == 3 ? (int)s_freeAimY
                   : (s_lockedTarget >= 0 ? s_lockedTargetY : (int)s_freeAimY);
    const int bankPx = s_rollActive ? 0 :
        (int)((s_cameraBank + s_routeBank * AUTO_ROUTE_CAMERA + s_courseBank) * 42.0f);
    const int pitchPx = s_rollActive ? 0 :
        (int)((s_cameraPitch + s_routePitch * AUTO_ROUTE_CAMERA + s_coursePitch) * 60.0f);
    const int noseX = s_rollActive ? 0 : constrain((aimX - sx) / 5, -16, 16);
    const int noseY = s_rollActive ? 0 : constrain((aimY - HORIZON_Y) / 3, -18, 18);
    const uint16_t body = boost ? WHITE : CYAN;
    // The compact flight form is seen from behind. A full 2-D sprite rotation
    // makes its long nose-to-tail silhouette sweep like a clock hand. During a
    // boosted roll, keep that longitudinal axis pointed into the scene and
    // rotate only the lateral span in perspective.
    const bool perspectiveFlightRoll = s_rollActive && boost;
    auto shipPoint = [&](int lx, int ly, int* ox, int* oy) {
        // Whole-body steering pose: upper mass leads the turn while the legs
        // and backpack lag behind. Pitch also twists wide parts of the frame.
        const float poseScale = 1.0f - s_transformBlend * 0.35f;
        const float drawYaw = s_rollActive ? 0.0f : s_poseYaw + bladePoseYaw;
        const float drawPitch = s_rollActive ? 0.0f : s_posePitch;
        // Slightly larger than the former 0.72 scale so one local design unit
        // maps to more Core2 pixels without letting the machine dominate the HUD.
        const float MECHA_DRAW_SCALE = 0.80f * bladeActionScale;
        const float posedX = (lx - ly * drawYaw * 0.34f * poseScale)
                          * MECHA_DRAW_SCALE;
        const float posedY = (ly + fabsf((float)lx) * drawPitch * 0.12f)
                          * MECHA_DRAW_SCALE;
        if (s_rollActive)
        {
            if (perspectiveFlightRoll)
            {
                *ox = sx + (int)(posedX * s_barrelCos);
                *oy = sy + (int)(posedY + posedX * s_barrelSin * 0.55f);
            }
            else
            {
                *ox = sx + (int)(posedX * s_barrelCos - posedY * s_barrelSin);
                *oy = sy + (int)(posedX * s_barrelSin + posedY * s_barrelCos);
            }
        }
        else
        {
            *ox = sx + (int)posedX;
            *oy = sy + (int)posedY;
        }
    };
    struct MechaPoint { int x; int y; };
    auto detailPoint = [&](const MechaPoint& center, int dx, int dy) {
        MechaPoint p;
        if (s_rollActive)
        {
            if (perspectiveFlightRoll)
            {
                p.x = center.x + (int)(dx * s_barrelCos);
                p.y = center.y + dy + (int)(dx * s_barrelSin * 0.55f);
            }
            else
            {
                p.x = center.x + (int)(dx * s_barrelCos - dy * s_barrelSin);
                p.y = center.y + (int)(dx * s_barrelSin + dy * s_barrelCos);
            }
        }
        else
        {
            p.x = center.x + dx;
            p.y = center.y + dy;
        }
        return p;
    };
    auto morphPoint = [&](int hx, int hy, int fx, int fy) {
        const float b = s_transformBlend;
        const int localX = (int)(hx + (fx - hx) * b);
        const int localY = (int)(hy + (fy - hy) * b);
        MechaPoint p;
        shipPoint(localX, localY, &p.x, &p.y);
        return p;
    };
    const float wingRaw = constrain((s_transformBlend - 0.22f) / 0.68f, 0.0f, 1.0f);
    const float wingBlend = wingRaw * wingRaw * (3.0f - 2.0f * wingRaw);
    auto morphWingPoint = [&](int hx, int hy, int fx, int fy) {
        const int localX = (int)(hx + (fx - hx) * wingBlend);
        const int localY = (int)(hy + (fy - hy) * wingBlend);
        MechaPoint p;
        shipPoint(localX, localY, &p.x, &p.y);
        return p;
    };
    // Humanoid points morph into a compact nose/wing/engine arrangement.
    const int yawGesture = s_rollActive ? 0 : (int)(s_poseYaw * 5.0f);
    const int lean = (int)(s_forwardLean * 6.0f);
    // Panels and limbs flare outward halfway through transformation, then lock
    // into the compact rear cluster. The center cowl simultaneously surges.
    const float transformArc = sinf(PI * s_transformBlend);
    const int panelFlare = (int)(transformArc * 14.0f);
    const int limbFlare = (int)(transformArc * 18.0f);
    const int legDrop = (int)(transformArc * 8.0f);
    const MechaPoint head = morphPoint(noseX / 5, -11 - lean / 2 + noseY / 5,
                                       noseX * 2 / 3, -13 + pitchPx / 3 + noseY / 3);
    const MechaPoint neck = morphPoint(0, -5 - lean / 3,
                                       noseX / 2, -7 + pitchPx / 4);
    const MechaPoint shoulderL = morphPoint(-8, -3, -7, -4 - bankPx / 3);
    const MechaPoint shoulderR = morphPoint( 8, -3,  7, -4 + bankPx / 3);
    const MechaPoint hipL = morphPoint(-4, 8, -3, 6);
    const MechaPoint hipR = morphPoint( 4, 8,  3, 6);
    const MechaPoint elbowL = morphPoint(-20 - panelFlare, 4 + yawGesture + lean / 2,
                                         -16 - panelFlare, 6 - bankPx / 2);
    const MechaPoint elbowR = morphPoint( 20 + panelFlare, 4-yawGesture + lean / 2,
                                          16 + panelFlare, 6 + bankPx / 2);
    const MechaPoint handL = morphWingPoint(-21 - limbFlare + lean / 3,
                                            14 + yawGesture + lean + legDrop / 2,
                                            -17, 13 - bankPx);
    const MechaPoint handR = morphWingPoint( 21 + limbFlare - lean / 3,
                                             14 - yawGesture + lean + legDrop / 2,
                                              17, 13 + bankPx);
    const MechaPoint kneeL = morphPoint(-5 - panelFlare / 3,
                                        14 - yawGesture + lean / 2 + legDrop, -3, 11);
    const MechaPoint kneeR = morphPoint( 5 + panelFlare / 3,
                                        14 + yawGesture + lean / 2 + legDrop,  3, 11);
    const MechaPoint footL = morphPoint(-6 - limbFlare / 3 + lean / 3,
                                        25 - yawGesture + lean + legDrop, -5, 18);
    const MechaPoint footR = morphPoint( 6 + limbFlare / 3 - lean / 3,
                                        25 + yawGesture + lean + legDrop,  5, 18);
    const MechaPoint thighOuterL = morphPoint(-7, 9 - yawGesture / 2, -4, 7);
    const MechaPoint thighOuterR = morphPoint( 7, 9 + yawGesture / 2,  4, 7);
    const MechaPoint shinOuterL = morphPoint(-10, 19 - yawGesture, -6, 14);
    const MechaPoint shinOuterR = morphPoint( 10, 19 + yawGesture,  6, 14);
    const MechaPoint shoulderArmorL = morphPoint(-22 - panelFlare, -8 - lean / 3,
                                                  -20, 7 - bankPx / 3);
    const MechaPoint shoulderArmorR = morphPoint( 22 + panelFlare, -8 - lean / 3,
                                                   20, 7 + bankPx / 3);
    const MechaPoint shoulderSkirtL = morphWingPoint(-21 - panelFlare,
                                                     8 + lean / 2 + legDrop / 2,
                                                     -24, 18 - bankPx / 2);
    const MechaPoint shoulderSkirtR = morphWingPoint( 21 + panelFlare,
                                                      8 + lean / 2 + legDrop / 2,
                                                       24, 18 + bankPx / 2);
    const MechaPoint chestKeel = morphPoint(0, 10, 0, 9);

    // Exchangeable weapon-arm hardpoints. Keep these pose coordinates grouped
    // so a later cannon/arm module can replace the current pair without
    // rewriting the transform or aiming paths.
    constexpr int CANNON_MOUNT_NORMAL_X = 30;
    constexpr int CANNON_MOUNT_NORMAL_Y = 2;
    constexpr int CANNON_MOUNT_FLIGHT_X = 21;
    constexpr int CANNON_MOUNT_FLIGHT_Y = 5;
    constexpr int CANNON_TIP_NORMAL_Y = -31;
    constexpr int CANNON_TIP_FLIGHT_Y = -20;
    // The hardpoints sit outside and slightly aft of the shoulders, making the
    // receiver and its shoulder link read as one articulated weapon arm.
    const MechaPoint cannonBaseL = morphPoint(-CANNON_MOUNT_NORMAL_X,
                                              CANNON_MOUNT_NORMAL_Y - lean / 2,
                                              -CANNON_MOUNT_FLIGHT_X,
                                              CANNON_MOUNT_FLIGHT_Y - bankPx / 4);
    const MechaPoint cannonBaseR = morphPoint( CANNON_MOUNT_NORMAL_X,
                                              CANNON_MOUNT_NORMAL_Y - lean / 2,
                                               CANNON_MOUNT_FLIGHT_X,
                                              CANNON_MOUNT_FLIGHT_Y + bankPx / 4);
    const MechaPoint cannonRestTipL = morphPoint(-CANNON_MOUNT_NORMAL_X,
                                                 CANNON_TIP_NORMAL_Y - lean,
                                                 -CANNON_MOUNT_FLIGHT_X,
                                                 CANNON_TIP_FLIGHT_Y - bankPx / 3);
    const MechaPoint cannonRestTipR = morphPoint( CANNON_MOUNT_NORMAL_X,
                                                 CANNON_TIP_NORMAL_Y - lean,
                                                  CANNON_MOUNT_FLIGHT_X,
                                                 CANNON_TIP_FLIGHT_Y + bankPx / 3);
    auto aimedCannonTip = [&](const MechaPoint& base, const MechaPoint& rest) {
        const float dx = (float)(aimX - base.x);
        const float dy = (float)(aimY - base.y);
        const float length = sqrtf(dx * dx + dy * dy);
        if (length < 4.0f) return rest;
        // During an axial roll the barrels point mainly into screen depth.
        // Foreshorten them while their hardpoints orbit with the machine;
        // otherwise a full-length 2D barrel reads as a rotating clock hand.
        const float barrelScreenLength = s_rollActive
            ? 9.0f
            : 25.0f + s_transformBlend * 10.0f;
        MechaPoint tip;
        tip.x = base.x + (int)(dx * barrelScreenLength / length);
        tip.y = base.y + (int)(dy * barrelScreenLength / length);
        return tip;
    };
    // The visible barrels and their projectiles now share the same boresight.
    // In particular, lowering the reticle visibly lowers both gun barrels.
    const MechaPoint cannonTipL = aimedCannonTip(cannonBaseL, cannonRestTipL);
    const MechaPoint cannonTipR = aimedCannonTip(cannonBaseR, cannonRestTipR);
    // The two exchangeable gun-arms become short armored hilts with plasma
    // edges during a blade action. Charge unfolds them; a quick slash snaps
    // them open, and the emitters retract briefly after the strike.
    float armBladeDeploy = 0.0f;
    uint32_t armSlashAge = UINT32_MAX;
    const uint32_t armSlashDuration = s_bladeSwingCharged
        ? BLADE_CHARGED_SWING_MS
        : (s_bladeSwingStage == 2 ? BLADE_SWING_MS : BLADE_QUICK_SWING_MS);
    if (s_weapon == 4)
    {
        if (s_bladeCharging)
            armBladeDeploy = min(1.0f, (drawNow - s_bladeChargeStartMs) / 220.0f);
        if (s_bladeDualMode) armBladeDeploy = 1.0f;
        if (s_bladeLastReleaseMs)
        {
            armSlashAge = drawNow - s_bladeLastReleaseMs;
            if (armSlashAge < armSlashDuration) armBladeDeploy = 1.0f;
            else if (armSlashAge < armSlashDuration + 180)
                armBladeDeploy = max(armBladeDeploy,
                    (armSlashDuration + 180 - armSlashAge) / 180.0f);
        }
    }
    // Bring the weapon arms toward the hands as the gun barrels fold. Keeping
    // both edges nearly parallel prevents the opened pair reading as pincers.
    const float armFold = armBladeDeploy * 0.72f;
    MechaPoint bladeGripL = handL;
    MechaPoint bladeGripR = handR;
    MechaPoint bladeElbowL = elbowL;
    MechaPoint bladeElbowR = elbowR;
    if (s_weapon == 4 && s_bladeSwingStartMs &&
        armSlashAge < armSlashDuration)
    {
        // Move the grips with the cut, not just the glowing tips. The elbows
        // follow half as far so each arm reads as an articulated swing.
        const int cut = min(255, (int)(armSlashAge * 255 /
            max<uint32_t>(1, armSlashDuration * 58 / 100)));
        int leftX = 0, leftY = 0, rightX = 0, rightY = 0;
        if (s_bladeSwingStage == 0)
        {
            rightX = 8 - cut * 17 / 255;
            rightY = -11 + cut * 23 / 255;
        }
        else if (s_bladeSwingStage == 1)
        {
            leftX = -9 + cut * 19 / 255;
            leftY = -5 + cut * 9 / 255;
        }
        else
        {
            const int reach = s_bladeSwingCharged ? 14 : 8;
            leftX = -reach + cut * (reach + 8) / 255;
            rightX = reach - cut * (reach + 8) / 255;
            leftY = -9 + cut * (s_bladeSwingCharged ? 22 : 14) / 255;
            rightY = leftY;
        }
        bladeGripL = detailPoint(handL, leftX, leftY);
        bladeGripR = detailPoint(handR, rightX, rightY);
        bladeElbowL = detailPoint(elbowL, leftX / 2, leftY / 2);
        bladeElbowR = detailPoint(elbowR, rightX / 2, rightY / 2);
    }
    const MechaPoint swordMountL = {
        cannonBaseL.x + (int)((bladeGripL.x - cannonBaseL.x) * armFold),
        cannonBaseL.y + (int)((bladeGripL.y - cannonBaseL.y) * armFold)
    };
    const MechaPoint swordMountR = {
        cannonBaseR.x + (int)((bladeGripR.x - cannonBaseR.x) * armFold),
        cannonBaseR.y + (int)((bladeGripR.y - cannonBaseR.y) * armFold)
    };
    const MechaPoint swordForwardL = detailPoint(swordMountL, -2, -29);
    const MechaPoint swordForwardR = detailPoint(swordMountR,  2, -29);
    MechaPoint cannonVisualTipL = {
        cannonTipL.x + (int)((swordForwardL.x - cannonTipL.x) * armBladeDeploy),
        cannonTipL.y + (int)((swordForwardL.y - cannonTipL.y) * armBladeDeploy)
    };
    MechaPoint cannonVisualTipR = {
        cannonTipR.x + (int)((swordForwardR.x - cannonTipR.x) * armBladeDeploy),
        cannonTipR.y + (int)((swordForwardR.y - cannonTipR.y) * armBladeDeploy)
    };
    if (s_weapon == 4 && s_bladeSwingStartMs && armSlashAge < armSlashDuration)
    {
        const float cut = min(1.0f, armSlashAge / (armSlashDuration * 0.58f));
        if (s_bladeSwingStage == 0)
            cannonVisualTipR = detailPoint(swordMountR,
                                            2 - (int)(cut * 10),
                                            -29 + (int)(cut * 7));
        else if (s_bladeSwingStage == 1)
            cannonVisualTipL = detailPoint(swordMountL,
                                            -2 + (int)(cut * 10), -29 + (int)(cut * 5));
        else
        {
            if (s_bladeSwingCharged)
            {
                cannonVisualTipL = detailPoint(swordMountL,
                                                -12 + (int)(cut * 22),
                                                -30 + (int)(cut * 40));
                cannonVisualTipR = detailPoint(swordMountR,
                                                 12 - (int)(cut * 22),
                                                -30 + (int)(cut * 40));
            }
            else
            {
                cannonVisualTipL = detailPoint(swordMountL,
                                                -4 + (int)(cut * 4),
                                                -30 + (int)(cut * 5));
                cannonVisualTipR = detailPoint(swordMountR,
                                                 4 - (int)(cut * 4),
                                                -30 + (int)(cut * 5));
            }
        }
    }
    const MechaPoint auxBaseL = morphWingPoint(-13, -2, -14, 1 - bankPx / 3);
    const MechaPoint auxBaseR = morphWingPoint( 13, -2,  14, 1 + bankPx / 3);
    const MechaPoint auxTipL = morphWingPoint(-14 - panelFlare, -8,
                                              -26, 13 - bankPx);
    const MechaPoint auxTipR = morphWingPoint( 14 + panelFlare, -8,
                                               26, 13 + bankPx);

    // Twin backpack pods remain visible behind the torso and become the main
    // aft thrusters when the legs fold into flight nacelles.
    const MechaPoint thrusterTopL = morphPoint(-14, 2 + lean / 2, -8, 8);
    const MechaPoint thrusterTopR = morphPoint( 14, 2 + lean / 2,  8, 8);
    const MechaPoint thrusterEndL = morphPoint(-15 + lean / 3, 16 + lean, -8, 18);
    const MechaPoint thrusterEndR = morphPoint( 15 - lean / 3, 16 + lean,  8, 18);
    const MechaPoint tailL = morphWingPoint(-10, 18, -12, 20 - bankPx / 3);
    const MechaPoint tailR = morphWingPoint( 10, 18,  12, 20 + bankPx / 3);

    // Central flight cowl: the frame folds into a compact mass, while only
    // this sharp armored nose projects forward along the travel direction.
    const MechaPoint cowlTip = morphWingPoint(0, -18 - lean - (int)(transformArc * 10.0f),
                                              noseX, -33 + pitchPx / 2 + noseY / 2);
    const MechaPoint cowlBaseL = morphWingPoint(-4, -8, -9, -8 - bankPx / 4);
    const MechaPoint cowlBaseR = morphWingPoint( 4, -8,  9, -8 + bankPx / 4);

    // Reference-inspired modular backpack: a tall central slab and articulated
    // dark pylons add the characteristic rear-heavy hard-surface silhouette.
    const MechaPoint dorsalTopL = morphPoint(-7, -34, -7, 2);
    const MechaPoint dorsalTopR = morphPoint( 7, -34,  7, 2);
    const MechaPoint dorsalBaseL = morphPoint(-10, -8, -11, 25);
    const MechaPoint dorsalBaseR = morphPoint( 10, -8,  11, 25);
    // Dark inner plate separates the massive backpack from the small head.
    s_frame.fillTriangle(dorsalTopL.x, dorsalTopL.y, dorsalBaseL.x, dorsalBaseL.y,
                         dorsalBaseR.x, dorsalBaseR.y, 0x1082);
    s_frame.fillTriangle(dorsalTopL.x, dorsalTopL.y, dorsalTopR.x, dorsalTopR.y,
                         dorsalBaseR.x, dorsalBaseR.y, 0x1082);
    s_frame.drawLine(dorsalTopL.x, dorsalTopL.y, dorsalTopR.x, dorsalTopR.y, WHITE);
    s_frame.drawLine(dorsalTopL.x, dorsalTopL.y, dorsalBaseL.x, dorsalBaseL.y, DARKCYAN);
    s_frame.drawLine(dorsalTopR.x, dorsalTopR.y, dorsalBaseR.x, dorsalBaseR.y, CYAN);
    s_frame.drawLine(dorsalBaseL.x, dorsalBaseL.y, dorsalBaseR.x, dorsalBaseR.y, WHITE);

    // Head sensor and armored chest remain readable in both silhouettes.
    // A large faceted head over a compressed torso gives the normal form its
    // compact head over a long narrow torso; the old two-head-tall constraint
    // is intentionally removed in favor of a stylish heroic proportion.
    const int headHalf = 5 - (int)(s_transformBlend * 1.0f);
    if (s_transformBlend < 0.82f)
    {
        const float flipPhase = constrain(s_transformBlend / 0.82f, 0.0f, 1.0f);
        const float flip = cosf(PI * flipPhase);
        const int foldedTop = -(int)((headHalf - 2) * flip);
        const int foldedBase = (int)((headHalf - 1) * flip);
        const MechaPoint crown = detailPoint(head, 0, foldedTop - 2);
        const MechaPoint templeL = detailPoint(head, -headHalf, foldedBase - 2);
        const MechaPoint templeR = detailPoint(head,  headHalf, foldedBase - 2);
        const MechaPoint jawL = detailPoint(head, -headHalf * 2 / 3, foldedBase + 3);
        const MechaPoint jawR = detailPoint(head,  headHalf * 2 / 3, foldedBase + 3);
        const MechaPoint chin = detailPoint(head, 0, foldedBase + 5);
        const MechaPoint shellCrownL = detailPoint(head, -headHalf - 1, foldedTop + 1);
        const MechaPoint shellCrownR = detailPoint(head,  headHalf + 1, foldedTop + 1);
        const MechaPoint shellRearL = detailPoint(head, -headHalf - 3, foldedBase + 5);
        const MechaPoint shellRearR = detailPoint(head,  headHalf + 3, foldedBase + 5);
        s_frame.drawLine(crown.x, crown.y, templeL.x, templeL.y, WHITE);
        s_frame.drawLine(crown.x, crown.y, templeR.x, templeR.y, WHITE);
        s_frame.drawLine(templeL.x, templeL.y, jawL.x, jawL.y, CYAN);
        s_frame.drawLine(templeR.x, templeR.y, jawR.x, jawR.y, CYAN);
        s_frame.drawLine(jawL.x, jawL.y, chin.x, chin.y, WHITE);
        s_frame.drawLine(jawR.x, jawR.y, chin.x, chin.y, WHITE);
        // Long swept helmet shells reproduce the reference's rear-heavy head
        // silhouette without filling a large expensive polygon.
        s_frame.drawLine(crown.x, crown.y, shellCrownL.x, shellCrownL.y, DARKCYAN);
        s_frame.drawLine(shellCrownL.x, shellCrownL.y, shellRearL.x, shellRearL.y, WHITE);
        s_frame.drawLine(shellRearL.x, shellRearL.y, jawL.x, jawL.y, CYAN);
        s_frame.drawLine(crown.x, crown.y, shellCrownR.x, shellCrownR.y, DARKCYAN);
        s_frame.drawLine(shellCrownR.x, shellCrownR.y, shellRearR.x, shellRearR.y, WHITE);
        s_frame.drawLine(shellRearR.x, shellRearR.y, jawR.x, jawR.y, CYAN);
        const MechaPoint brow = detailPoint(head, 0, -1);
        s_frame.drawLine(crown.x, crown.y, brow.x, brow.y, DARKCYAN);
        const int sensorHalf = max(1, (int)((headHalf - 2) * fabsf(flip)));
        const MechaPoint visorL = detailPoint(head, -sensorHalf, 1);
        const MechaPoint visorR = detailPoint(head,  sensorHalf, 1);
        s_frame.drawLine(visorL.x, visorL.y, visorR.x, visorR.y,
                         flip < 0.0f ? ORANGE : CYAN);
        // Multi-pronged mechanical crest. Long primary horns, a center blade,
        // bent mid horns and low whiskers all branch from one armored hub.
        const float finFold = fabsf(flip);
        const int longSpan = max(6, (int)(12.5f * finFold));
        const int midSpan = max(5, (int)(9.0f * finFold));
        const MechaPoint finHub = detailPoint(head, 0, -1);
        const MechaPoint crownJoint = detailPoint(head, 0, -4);
        const MechaPoint centerTip = detailPoint(head, 1, -13);
        // The side blades sweep toward the rear instead of opening as a V.
        // This keeps the crest directional and makes it read as aerodynamic
        // head armor rather than a conventional two-horn antenna.
        const MechaPoint primaryJointL = detailPoint(head, -4, -3);
        const MechaPoint primaryJointR = detailPoint(head,  4, -2);
        const MechaPoint primaryTipL = detailPoint(head, -longSpan, -5);
        const MechaPoint primaryTipR = detailPoint(head,  longSpan, -4);
        const MechaPoint midJointL = detailPoint(head, -3, -1);
        const MechaPoint midJointR = detailPoint(head,  4,  0);
        const MechaPoint midTipL = detailPoint(head, -midSpan, -2);
        const MechaPoint midTipR = detailPoint(head,  midSpan + 1, -1);
        const MechaPoint lowTipL = detailPoint(head, -7, 3);
        const MechaPoint lowTipR = detailPoint(head, 8, 3);
        s_frame.fillCircle(finHub.x, finHub.y, 1, YELLOW);
        s_frame.drawLine(finHub.x, finHub.y, crownJoint.x, crownJoint.y, YELLOW);
        s_frame.drawLine(crownJoint.x, crownJoint.y, centerTip.x, centerTip.y, ORANGE);
        s_frame.drawLine(finHub.x, finHub.y, primaryJointL.x, primaryJointL.y, YELLOW);
        s_frame.drawLine(primaryJointL.x, primaryJointL.y,
                         primaryTipL.x, primaryTipL.y, ORANGE);
        s_frame.drawLine(finHub.x, finHub.y, primaryJointR.x, primaryJointR.y, YELLOW);
        s_frame.drawLine(primaryJointR.x, primaryJointR.y,
                         primaryTipR.x, primaryTipR.y, ORANGE);
        s_frame.drawLine(finHub.x, finHub.y, midJointL.x, midJointL.y, YELLOW);
        s_frame.drawLine(midJointL.x, midJointL.y, midTipL.x, midTipL.y, ORANGE);
        s_frame.drawLine(finHub.x, finHub.y, midJointR.x, midJointR.y, YELLOW);
        s_frame.drawLine(midJointR.x, midJointR.y, midTipR.x, midTipR.y, ORANGE);
        s_frame.drawLine(finHub.x, finHub.y, lowTipL.x, lowTipL.y, ORANGE);
        s_frame.drawLine(finHub.x, finHub.y, lowTipR.x, lowTipR.y, ORANGE);
    }
    else
    {
        const MechaPoint storedTip = detailPoint(head, 0, -2);
        const MechaPoint storedL = detailPoint(head, -3, 2);
        const MechaPoint storedR = detailPoint(head, 3, 2);
        s_frame.drawTriangle(storedTip.x, storedTip.y, storedL.x, storedL.y,
                             storedR.x, storedR.y, ORANGE);
    }
    s_frame.drawLine(neck.x, neck.y, shoulderL.x, shoulderL.y, body);
    s_frame.drawLine(neck.x, neck.y, shoulderR.x, shoulderR.y, body);
    s_frame.drawLine(shoulderL.x, shoulderL.y, hipL.x, hipL.y, body);
    s_frame.drawLine(shoulderR.x, shoulderR.y, hipR.x, hipR.y, body);
    s_frame.drawLine(hipL.x, hipL.y, hipR.x, hipR.y, DARKCYAN);
    s_frame.drawLine(hipL.x, hipL.y, chestKeel.x, chestKeel.y, WHITE);
    s_frame.drawLine(hipR.x, hipR.y, chestKeel.x, chestKeel.y, WHITE);
    // Two dark, compact chest plates create one strong torso mass. The former
    // crossing frame lines were removed so the tiny silhouette stays legible.
    const MechaPoint chestL = morphPoint(-8, -2, -5, 1);
    const MechaPoint chestR = morphPoint( 8, -2,  5, 1);
    s_frame.fillTriangle(neck.x, neck.y, chestL.x, chestL.y,
                         chestKeel.x, chestKeel.y, 0x1082);
    s_frame.fillTriangle(neck.x, neck.y, chestR.x, chestR.y,
                         chestKeel.x, chestKeel.y, 0x1082);
    s_frame.drawLine(shoulderL.x, shoulderL.y, chestL.x, chestL.y, WHITE);
    s_frame.drawLine(chestL.x, chestL.y, chestKeel.x, chestKeel.y, WHITE);
    s_frame.drawLine(shoulderR.x, shoulderR.y, chestR.x, chestR.y, WHITE);
    s_frame.drawLine(chestR.x, chestR.y, chestKeel.x, chestKeel.y, WHITE);
    s_frame.drawLine(chestL.x, chestL.y, chestR.x, chestR.y, ORANGE);

    // At high transform ratios the articulated frame closes into an explicit
    // fighter hull and delta wing instead of remaining a folded humanoid.
    if (s_transformBlend > 0.38f)
    {
        const uint16_t flightColor = s_transformBlend > 0.72f ? WHITE : CYAN;
        s_frame.drawTriangle(cowlTip.x, cowlTip.y, shoulderSkirtL.x, shoulderSkirtL.y,
                             shoulderSkirtR.x, shoulderSkirtR.y, flightColor);
        s_frame.drawTriangle(cowlTip.x, cowlTip.y, cowlBaseL.x, cowlBaseL.y,
                             cowlBaseR.x, cowlBaseR.y, WHITE);
        s_frame.drawLine(cowlBaseL.x, cowlBaseL.y, chestKeel.x, chestKeel.y, DARKCYAN);
        s_frame.drawLine(cowlBaseR.x, cowlBaseR.y, chestKeel.x, chestKeel.y, DARKCYAN);
        s_frame.drawLine(shoulderSkirtL.x, shoulderSkirtL.y, tailL.x, tailL.y, DARKCYAN);
        s_frame.drawLine(shoulderSkirtR.x, shoulderSkirtR.y, tailR.x, tailR.y, DARKCYAN);
        s_frame.drawLine(handL.x, handL.y, shoulderSkirtL.x, shoulderSkirtL.y, CYAN);
        s_frame.drawLine(handR.x, handR.y, shoulderSkirtR.x, shoulderSkirtR.y, CYAN);
        s_frame.drawLine(tailL.x, tailL.y, thrusterEndL.x, thrusterEndL.y, CYAN);
        s_frame.drawLine(tailR.x, tailR.y, thrusterEndR.x, thrusterEndR.y, CYAN);
    }

    // Large angular shoulder shells and a broad chest create the massive
    // silhouette; internal diagonals suggest layered armor without fills.
    s_frame.fillTriangle(shoulderL.x, shoulderL.y, shoulderArmorL.x, shoulderArmorL.y,
                         shoulderSkirtL.x, shoulderSkirtL.y, 0x1082);
    s_frame.fillTriangle(shoulderR.x, shoulderR.y, shoulderArmorR.x, shoulderArmorR.y,
                         shoulderSkirtR.x, shoulderSkirtR.y, 0x1082);
    s_frame.drawTriangle(neck.x, neck.y, shoulderArmorL.x, shoulderArmorL.y,
                         shoulderSkirtL.x, shoulderSkirtL.y, body);
    s_frame.drawTriangle(neck.x, neck.y, shoulderArmorR.x, shoulderArmorR.y,
                         shoulderSkirtR.x, shoulderSkirtR.y, body);
    s_frame.drawLine(shoulderArmorL.x, shoulderArmorL.y,
                     shoulderL.x, shoulderL.y, WHITE);
    s_frame.drawLine(shoulderArmorR.x, shoulderArmorR.y,
                     shoulderR.x, shoulderR.y, WHITE);
    s_frame.drawLine(shoulderArmorL.x, shoulderArmorL.y,
                     bladeElbowL.x, bladeElbowL.y, DARKCYAN);
    s_frame.drawLine(shoulderArmorR.x, shoulderArmorR.y,
                     bladeElbowR.x, bladeElbowR.y, DARKCYAN);
    // Bright outer booster blades sit over the dark shoulder mass.
    s_frame.drawTriangle(shoulderArmorL.x, shoulderArmorL.y,
                         auxTipL.x, auxTipL.y, shoulderSkirtL.x, shoulderSkirtL.y, WHITE);
    s_frame.drawTriangle(shoulderArmorR.x, shoulderArmorR.y,
                         auxTipR.x, auxTipR.y, shoulderSkirtR.x, shoulderSkirtR.y, WHITE);

    // Articulated arms become swept wings and shoulder thruster pylons.
    s_frame.drawLine(shoulderL.x, shoulderL.y, bladeElbowL.x, bladeElbowL.y, body);
    s_frame.drawLine(bladeElbowL.x, bladeElbowL.y, bladeGripL.x, bladeGripL.y,
                     s_transformBlend > 0.45f ? WHITE : DARKCYAN);
    s_frame.drawLine(shoulderR.x, shoulderR.y, bladeElbowR.x, bladeElbowR.y, body);
    s_frame.drawLine(bladeElbowR.x, bladeElbowR.y, bladeGripR.x, bladeGripR.y,
                     s_transformBlend > 0.45f ? WHITE : DARKCYAN);
    // Segmented weapon pylons and blade-like outer armor echo the reference's
    // independently articulated long-weapon arms.
    s_frame.drawCircle(bladeElbowL.x, bladeElbowL.y, 2, ORANGE);
    s_frame.drawCircle(bladeElbowR.x, bladeElbowR.y, 2, ORANGE);
    s_frame.drawTriangle(auxBaseL.x, auxBaseL.y, auxTipL.x, auxTipL.y,
                         bladeGripL.x, bladeGripL.y, DARKCYAN);
    s_frame.drawTriangle(auxBaseR.x, auxBaseR.y, auxTipR.x, auxTipR.y,
                         bladeGripR.x, bladeGripR.y, DARKCYAN);

    // Heavy cannons use a broad armored receiver, a narrowed long barrel and a
    // bright muzzle collar. This avoids the previous uniform stick silhouette.
    auto drawHeavyCannon = [&](const MechaPoint& mount, const MechaPoint& tip,
                               const MechaPoint& shoulder, bool leftSide) {
        const float dx = (float)(tip.x - mount.x);
        const float dy = (float)(tip.y - mount.y);
        const float len = max(1.0f, sqrtf(dx * dx + dy * dy));
        const float nx = -dy / len;
        const float ny =  dx / len;
        const MechaPoint receiverEnd = {
            mount.x + (int)(dx * 0.43f),
            mount.y + (int)(dy * 0.43f)
        };
        const MechaPoint metalTip = {
            mount.x + (int)(dx * (1.0f - armBladeDeploy * 0.52f)),
            mount.y + (int)(dy * (1.0f - armBladeDeploy * 0.52f))
        };
        auto offset = [&](const MechaPoint& p, float width) {
            return MechaPoint{p.x + (int)(nx * width), p.y + (int)(ny * width)};
        };
        const float hiltHalf = 5.0f - armBladeDeploy * 2.0f;
        const MechaPoint baseA = offset(mount, hiltHalf);
        const MechaPoint baseB = offset(mount, -hiltHalf);
        const MechaPoint neckA = offset(receiverEnd, 3.0f - armBladeDeploy);
        const MechaPoint neckB = offset(receiverEnd, -3.0f + armBladeDeploy);
        s_frame.fillTriangle(baseA.x, baseA.y, baseB.x, baseB.y,
                             neckA.x, neckA.y, 0x1082);
        s_frame.fillTriangle(baseB.x, baseB.y, neckA.x, neckA.y,
                             neckB.x, neckB.y, 0x1082);
        s_frame.drawLine(baseA.x, baseA.y, neckA.x, neckA.y,
                         leftSide ? WHITE : DARKCYAN);
        s_frame.drawLine(baseB.x, baseB.y, neckB.x, neckB.y,
                         leftSide ? DARKCYAN : WHITE);
        s_frame.drawLine(baseA.x, baseA.y, baseB.x, baseB.y, CYAN);
        const MechaPoint tipA = offset(metalTip, 1.5f);
        const MechaPoint tipB = offset(metalTip, -1.5f);
        s_frame.drawLine(neckA.x, neckA.y, tipA.x, tipA.y, WHITE);
        s_frame.drawLine(neckB.x, neckB.y, tipB.x, tipB.y, DARKCYAN);
        const MechaPoint collarA = offset(metalTip, 4.0f + armBladeDeploy * 1.5f);
        const MechaPoint collarB = offset(metalTip, -4.0f - armBladeDeploy * 1.5f);
        s_frame.drawLine(collarA.x, collarA.y, collarB.x, collarB.y,
                         armBladeDeploy > 0.45f ? GREEN : ORANGE);
        s_frame.drawLine(shoulder.x, shoulder.y, baseA.x, baseA.y, CYAN);
        s_frame.drawCircle(mount.x, mount.y, 2,
                           armBladeDeploy > 0.45f ? GREEN : ORANGE);
        if (armBladeDeploy > 0.05f)
        {
            // A slim plasma edge extends beyond the short armored hilt.
            // Keep the sheath narrow; the separate slash effect supplies
            // the broad motion trail only during an attack.
            const float side = leftSide ? -1.0f : 1.0f;
            const float bladeWidth = 3.2f * armBladeDeploy;
            const MechaPoint rootOuter = offset(metalTip, side * bladeWidth);
            const MechaPoint rootInner = offset(metalTip, -side * bladeWidth * 0.65f);
            const MechaPoint bladeShoulder = {
                metalTip.x + (int)(dx * 10.0f * armBladeDeploy / len
                                    + nx * side * bladeWidth * 1.25f),
                metalTip.y + (int)(dy * 10.0f * armBladeDeploy / len
                                    + ny * side * bladeWidth * 1.25f)
            };
            const MechaPoint bladeTip = {
                metalTip.x + (int)(dx * 24.0f * armBladeDeploy / len
                                    + nx * side * armBladeDeploy),
                metalTip.y + (int)(dy * 24.0f * armBladeDeploy / len
                                    + ny * side * armBladeDeploy)
            };
            s_frame.fillTriangle(rootOuter.x, rootOuter.y,
                                 rootInner.x, rootInner.y,
                                 bladeShoulder.x, bladeShoulder.y, 0x0320);
            s_frame.fillTriangle(rootInner.x, rootInner.y,
                                 bladeShoulder.x, bladeShoulder.y,
                                 bladeTip.x, bladeTip.y, 0x04C0);
            s_frame.drawLine(rootOuter.x, rootOuter.y,
                             bladeShoulder.x, bladeShoulder.y, GREEN);
            s_frame.drawLine(bladeShoulder.x, bladeShoulder.y,
                             bladeTip.x, bladeTip.y, GREEN);
            s_frame.drawLine(rootInner.x, rootInner.y,
                             bladeTip.x, bladeTip.y, GREEN);
            s_frame.drawLine(metalTip.x, metalTip.y,
                             bladeTip.x, bladeTip.y, WHITE);
            s_frame.drawPixel(bladeTip.x, bladeTip.y, WHITE);
        }
    };
    drawHeavyCannon(swordMountL, cannonVisualTipL, shoulderArmorL, true);
    drawHeavyCannon(swordMountR, cannonVisualTipR, shoulderArmorR, false);
    if (s_transformBlend > 0.28f)
    {
        s_frame.drawLine(auxBaseL.x, auxBaseL.y, auxTipL.x, auxTipL.y, CYAN);
        s_frame.drawLine(auxBaseR.x, auxBaseR.y, auxTipR.x, auxTipR.y, CYAN);
        s_frame.drawPixel(auxTipL.x, auxTipL.y, WHITE);
        s_frame.drawPixel(auxTipR.x, auxTipR.y, WHITE);
    }

    // Hybrid prototype: a compact indexed-color body supplies stable visual
    // mass; articulated cannons, pylons, pod contours and exhaust remain live.
    // The middle of the transformation remains procedural so the moving parts
    // stay readable. Once either pose settles, its compact rear sprite supplies
    // stable visual mass while cannons, pylons and exhaust remain live line art.
    const bool hybridPoseAllowed = s_hybridRenderEnabled && !s_manualLoopActive;
    const bool drawHybridBody = hybridPoseAllowed &&
        ((s_rollActive && !boost) || s_transformBlend < 0.28f);
    const bool drawHybridFlight = hybridPoseAllowed &&
        ((s_rollActive && boost) || s_transformBlend > 0.82f);
    // HYBRID sprites already contain the leg armor. Suppress duplicate thigh,
    // shin and foot contours so the wider weapon arms do not make the lower
    // body look busy. LINE mode retains a newly compact procedural leg frame.
    if (!drawHybridBody && !drawHybridFlight)
    {
        s_frame.drawTriangle(hipL.x, hipL.y, thighOuterL.x, thighOuterL.y,
                             kneeL.x, kneeL.y, body);
        s_frame.drawTriangle(hipR.x, hipR.y, thighOuterR.x, thighOuterR.y,
                             kneeR.x, kneeR.y, body);
        s_frame.fillTriangle(kneeL.x, kneeL.y, shinOuterL.x, shinOuterL.y,
                             footL.x, footL.y, 0x1082);
        s_frame.fillTriangle(kneeR.x, kneeR.y, shinOuterR.x, shinOuterR.y,
                             footR.x, footR.y, 0x1082);
        s_frame.drawTriangle(kneeL.x, kneeL.y, shinOuterL.x, shinOuterL.y,
                             footL.x, footL.y, WHITE);
        s_frame.drawTriangle(kneeR.x, kneeR.y, shinOuterR.x, shinOuterR.y,
                             footR.x, footR.y, WHITE);
        const MechaPoint footCapL0 = detailPoint(footL, -2, 0);
        const MechaPoint footCapL1 = detailPoint(footL,  2, 0);
        const MechaPoint footCapR0 = detailPoint(footR, -2, 0);
        const MechaPoint footCapR1 = detailPoint(footR,  2, 0);
        s_frame.drawLine(shinOuterL.x, shinOuterL.y, footCapL0.x, footCapL0.y, CYAN);
        s_frame.drawLine(shinOuterR.x, shinOuterR.y, footCapR1.x, footCapR1.y, CYAN);
        s_frame.drawLine(footCapL0.x, footCapL0.y, footCapL1.x, footCapL1.y, ORANGE);
        s_frame.drawLine(footCapR0.x, footCapR0.y, footCapR1.x, footCapR1.y, ORANGE);
    }
    if (drawHybridBody || drawHybridFlight)
    {
        // Embedded literals are host-order RGB565; M5GFX image transfer needs
        // byte swapping for this source, then the canvas default is restored.
        if ((s_rollActive || drawHybridFlight) && s_hybridCanvasesReady)
        {
            if (drawHybridFlight)
            {
                // Rear-view axial roll: show the wings/pylons changing their
                // projected width instead of spinning the long fuselage flat
                // around the LCD plane. A small bank cue preserves direction.
                const float rollWidth = 0.55f + 0.45f * fabsf(s_barrelCos);
                const float bankCueDeg = s_barrelSin * 7.0f;
                s_hybridFlightCanvas.pushRotateZoom(&s_frame, sx, sy,
                    bankCueDeg, rollWidth, 0.84f,
                    MECHA_HYBRID_FLIGHT_TRANSPARENT);
            }
            else
                s_hybridNormalCanvas.pushRotateZoom(&s_frame, sx, sy,
                    s_rollAngleDeg, 1.0f, 1.0f,
                    MECHA_HYBRID_BODY_TRANSPARENT);
        }
        else
        {
            s_frame.setSwapBytes(true);
            if (drawHybridFlight)
                s_frame.pushImage(sx - MECHA_HYBRID_FLIGHT_W / 2, sy - 35,
                                  MECHA_HYBRID_FLIGHT_W, MECHA_HYBRID_FLIGHT_H,
                                  MECHA_HYBRID_FLIGHT,
                                  MECHA_HYBRID_FLIGHT_TRANSPARENT);
            else
                s_frame.pushImage(sx - MECHA_HYBRID_BODY_W / 2, sy - 40,
                                  MECHA_HYBRID_BODY_W, MECHA_HYBRID_BODY_H,
                                  MECHA_HYBRID_BODY,
                                  MECHA_HYBRID_BODY_TRANSPARENT);
            s_frame.setSwapBytes(false);
        }
    }

    // Temporary module silhouettes. Keep these few primitives separate from
    // the baked body sprite until the final front/rear artwork is ready.
    if (!s_rollActive)
    {
        if (s_loadout.arms)
        {
            s_frame.drawCircle(cannonBaseL.x, cannonBaseL.y, 4, ORANGE);
            s_frame.drawCircle(cannonBaseR.x, cannonBaseR.y, 4, ORANGE);
        }
        if (s_loadout.legs && !drawHybridFlight)
        {
            s_frame.drawLine(kneeL.x, kneeL.y, footL.x, footL.y, CYAN);
            s_frame.drawLine(kneeR.x, kneeR.y, footR.x, footR.y, CYAN);
        }
    }

    // Backpack thruster pods: parallel contours make them read as volume,
    // while the orange end caps remain clear at Core2 resolution.
    const MechaPoint podTopL0 = detailPoint(thrusterTopL, -3, 0);
    const MechaPoint podTopL1 = detailPoint(thrusterTopL,  3, 0);
    const MechaPoint podTopR0 = detailPoint(thrusterTopR, -3, 0);
    const MechaPoint podTopR1 = detailPoint(thrusterTopR,  3, 0);
    const MechaPoint podEndL0 = detailPoint(thrusterEndL, -3, 0);
    const MechaPoint podEndL1 = detailPoint(thrusterEndL,  3, 0);
    const MechaPoint podEndR0 = detailPoint(thrusterEndR, -3, 0);
    const MechaPoint podEndR1 = detailPoint(thrusterEndR,  3, 0);
    s_frame.drawLine(podTopL0.x, podTopL0.y, podEndL0.x, podEndL0.y, DARKCYAN);
    s_frame.drawLine(podTopL1.x, podTopL1.y, podEndL1.x, podEndL1.y, body);
    s_frame.drawLine(podTopR0.x, podTopR0.y, podEndR0.x, podEndR0.y, body);
    s_frame.drawLine(podTopR1.x, podTopR1.y, podEndR1.x, podEndR1.y, DARKCYAN);
    const MechaPoint podCapL0 = detailPoint(thrusterEndL, -4, 0);
    const MechaPoint podCapL1 = detailPoint(thrusterEndL,  4, 0);
    const MechaPoint podCapR0 = detailPoint(thrusterEndR, -4, 0);
    const MechaPoint podCapR1 = detailPoint(thrusterEndR,  4, 0);
    s_frame.drawLine(podCapL0.x, podCapL0.y, podCapL1.x, podCapL1.y, ORANGE);
    s_frame.drawLine(podCapR0.x, podCapR0.y, podCapR1.x, podCapR1.y, ORANGE);
    // A compact inner pair appears as the frame closes, giving flight mode a
    // four-nozzle rear cluster without adding particles or filled geometry.
    const MechaPoint innerTopL = morphPoint(-4, 11, -4, 16);
    const MechaPoint innerTopR = morphPoint( 4, 11,  4, 16);
    const MechaPoint innerEndL = morphPoint(-4, 16, -4, 21);
    const MechaPoint innerEndR = morphPoint( 4, 16,  4, 21);
    if (s_transformBlend > 0.45f)
    {
        s_frame.drawLine(innerTopL.x, innerTopL.y, innerEndL.x, innerEndL.y, DARKCYAN);
        s_frame.drawLine(innerTopR.x, innerTopR.y, innerEndR.x, innerEndR.y, CYAN);
        const MechaPoint innerCapL0 = detailPoint(innerEndL, -2, 0);
        const MechaPoint innerCapL1 = detailPoint(innerEndL,  2, 0);
        const MechaPoint innerCapR0 = detailPoint(innerEndR, -2, 0);
        const MechaPoint innerCapR1 = detailPoint(innerEndR,  2, 0);
        s_frame.drawLine(innerCapL0.x, innerCapL0.y, innerCapL1.x, innerCapL1.y, ORANGE);
        s_frame.drawLine(innerCapR0.x, innerCapR0.y, innerCapR1.x, innerCapR1.y, ORANGE);
    }
    // Rear nozzles glow at their source instead of dragging long flame lines
    // behind the craft. Concentric low-cost RGB565 discs suggest bloom without
    // particles, alpha blending or the former windsock/cardboard appearance.
    {
        const bool hotGlow = boost || s_boostVisual > 0.32f;
        const int pulse = ((drawNow >> (hotGlow ? 5 : 7)) & 1) ? 1 : 0;
        auto drawPodGlow = [&](const MechaPoint& top, const MechaPoint& end) {
            const int dx = end.x - top.x, dy = end.y - top.y;
            const int scale = max(1, max(abs(dx), abs(dy)));
            const int glowX = end.x + dx / scale;
            const int glowY = end.y + dy / scale;
            const int outerRadius = (hotGlow ? 3 : 2) + pulse;
            s_frame.drawCircle(glowX, glowY, outerRadius,
                               s_cyanThrusterEquipped ? DARKCYAN :
                               (hotGlow ? 0xF980 : DARKCYAN));
            s_frame.fillCircle(glowX, glowY, max(1, outerRadius - 1),
                               s_cyanThrusterEquipped ? CYAN :
                               (hotGlow ? ORANGE : CYAN));
            s_frame.fillCircle(glowX, glowY, 1,
                               s_cyanThrusterEquipped ? WHITE :
                               (hotGlow ? YELLOW : WHITE));
            if (hotGlow)
            {
                // A two-pixel axial bloom gives thrust direction without
                // becoming a trailing streamer.
                const int bloomX = end.x + dx * 3 / scale;
                const int bloomY = end.y + dy * 3 / scale;
                s_frame.drawPixel(bloomX, bloomY, WHITE);
            }
        };
        drawPodGlow(thrusterTopL, thrusterEndL);
        drawPodGlow(thrusterTopR, thrusterEndR);
        if (s_transformBlend > 0.45f)
        {
            drawPodGlow(innerTopL, innerEndL);
            drawPodGlow(innerTopR, innerEndR);
        }
    }

    if (s_modelCaptureClean) return;

    const bool reticleFiring = (int32_t)(s_fireUntilMs - millis()) > 0;
    drawWeaponReticle(aimX, aimY, s_weapon, reticleFiring);
    if (s_weapon == 5)
    {
        s_frame.setTextColor(s_rearMissileActive ? RED : YELLOW, BLACK);
        s_frame.setCursor(aimX - 25, max(1, aimY - 27));
        s_frame.print((int32_t)(s_flareCooldownUntilMs - drawNow) > 0
                      ? "RELOAD" : "FLARE RDY");
    }
    if (s_weapon == 2 && (s_beamState == BeamState::CHARGING ||
                          s_beamState == BeamState::READY))
    {
        const uint32_t chargeAge = s_beamState == BeamState::READY
            ? BEAM_CHARGE_MS : millis() - s_beamStateSinceMs;
        drawBeamChargeReticle(aimX, aimY, chargeAge);
        if (s_beamState == BeamState::READY)
        {
            const int pulse = (millis() / 100) & 1;
            const uint16_t readyColor = pulse ? WHITE : CYAN;
            s_frame.drawLine(aimX - 3, aimY, aimX + 3, aimY, readyColor);
            s_frame.drawLine(aimX, aimY - 3, aimX, aimY + 3, readyColor);
        }
    }
    // Beam charge state is shown in the dedicated lower status rail. Keeping
    // it out of the combat viewport prevents overlap with the boss HP bar and
    // missile warnings.
    if (s_weapon == 3 && s_multiLockCharging)
    {
        const int pulse = 12 + ((drawNow / 80) & 3);
        s_frame.drawCircle(aimX, aimY, pulse, s_multiLockCount ? MAGENTA : ORANGE);
        s_frame.setTextColor(s_multiLockCount ? WHITE : ORANGE, BLACK);
        s_frame.setCursor(aimX - 18, aimY - 27);
        s_frame.printf("LOCK %u", (unsigned)s_multiLockCount);
    }
    if (s_weapon == 4 && s_bladeCharging)
    {
        const uint32_t chargeAge = drawNow - s_bladeChargeStartMs;
        const int charge = min(100, (int)(chargeAge * 100 / BLADE_CHARGE_MS));
        const int lit = min(12, charge * 12 / 100);
        // Energy fills two compact blade rails instead of using the beam's
        // circular charge language.
        s_frame.drawLine(aimX - 12, aimY + 13, aimX - 3, aimY - 9, 0x0320);
        s_frame.drawLine(aimX + 12, aimY + 13, aimX + 3, aimY - 9, 0x0320);
        if (lit > 0)
        {
            s_frame.drawLine(aimX - 12, aimY + 13,
                             aimX - 12 + lit * 9 / 12,
                             aimY + 13 - lit * 22 / 12, GREEN);
            if (chargeAge >= BLADE_DUAL_DEPLOY_MS)
                s_frame.drawLine(aimX + 12, aimY + 13,
                                 aimX + 12 - lit * 9 / 12,
                                 aimY + 13 - lit * 22 / 12, WHITE);
        }
        s_frame.setTextColor(chargeAge >= BLADE_DUAL_DEPLOY_MS ? WHITE : GREEN, BLACK);
        s_frame.setCursor(aimX - 22, aimY - 28);
        if (chargeAge >= BLADE_DUAL_DEPLOY_MS) s_frame.print("DUAL READY");
        else s_frame.printf("CHG %02d", charge);
    }
    else if (s_weapon == 4 && s_bladeDualMode)
    {
        s_frame.setTextColor(CYAN, BLACK);
        s_frame.setCursor(aimX - 13, aimY - 25);
        s_frame.print("DUAL");
    }
    // Weapon changes deserve a distinct color-coded acknowledgement. Suppress
    // it while a missile warning owns this area, so the two never overlap.
    if ((int32_t)(s_weaponBannerUntilMs - drawNow) > 0 &&
        !s_rearMissileActive && !enemyMissileThreatActive())
    {
        const uint16_t weaponColor = s_weapon == 0 ? GREEN
            : s_weapon == 1 ? ORANGE
            : s_weapon == 2 ? CYAN
            : s_weapon == 3 ? MAGENTA
            : s_weapon == 4 ? 0x7DFF : YELLOW;
        s_frame.fillRect(CX - 38, 20, 76, 14, BLACK);
        s_frame.drawRect(CX - 38, 20, 76, 14, weaponColor);
        s_frame.drawFastHLine(CX - 34, 31, 68, weaponColor);
        s_frame.setTextColor(weaponColor, BLACK);
        s_frame.setCursor(CX - 29, 23);
        s_frame.print(weaponName());
    }
    if (s_autoPilotEnabled)
    {
        s_frame.setTextColor(MAGENTA, BLACK);
        s_frame.setCursor(3, 3);
        s_frame.print((int32_t)(drawNow - s_autoPilotManualUntilMs) < 0
                      ? "AUTO HOLD" : "AUTO");
    }
    if (s_lockedTarget >= 0)
    {
        const uint16_t lockColor = s_rearMissileActive ? RED
                                 : s_weapon == 3 ? MAGENTA
                                 : s_weapon == 4 ? 0x7DFF
                                 : (s_weapon == 1 ? ORANGE : GREEN);
        const int r = 19, gap = 9;
        s_frame.drawLine(aimX-r, aimY-r, aimX-gap, aimY-r, lockColor);
        s_frame.drawLine(aimX-r, aimY-r, aimX-r, aimY-gap, lockColor);
        s_frame.drawLine(aimX+r, aimY-r, aimX+gap, aimY-r, lockColor);
        s_frame.drawLine(aimX+r, aimY-r, aimX+r, aimY-gap, lockColor);
        s_frame.drawLine(aimX-r, aimY+r, aimX-gap, aimY+r, lockColor);
        s_frame.drawLine(aimX-r, aimY+r, aimX-r, aimY+gap, lockColor);
        s_frame.drawLine(aimX+r, aimY+r, aimX+gap, aimY+r, lockColor);
        s_frame.drawLine(aimX+r, aimY+r, aimX+r, aimY+gap, lockColor);
        const bool beamChargeVisible = s_weapon == 2 &&
            (s_beamState == BeamState::CHARGING || s_beamState == BeamState::READY);
        if (beamChargeVisible || ((millis() / 180) & 1))
        {
            s_frame.setTextColor(lockColor, BLACK); s_frame.setCursor(aimX-15, aimY-30);
            if (s_beamState == BeamState::READY) s_frame.print("READY");
            else if (s_beamState == BeamState::CHARGING) s_frame.print("CHARGE");
            else s_frame.print(s_lockedTargetGround ? "GND" : "AIR");
        }
    }

    if (s_lockedTarget >= 0)
    {
        // The locked reticle follows its target, while this smaller steering
        // cursor continues to follow the ball. Move it onto another yellow
        // candidate and press FIRE to switch targets.
        const int fx = (int)s_freeAimX, fy = (int)s_freeAimY;
        s_frame.drawRect(fx - 3, fy - 3, 7, 7, CYAN);
        s_frame.drawFastHLine(fx - 8, fy, 5, CYAN);
        s_frame.drawFastHLine(fx + 4, fy, 5, CYAN);
        s_frame.drawFastVLine(fx, fy - 8, 5, CYAN);
        s_frame.drawFastVLine(fx, fy + 4, 5, CYAN);
    }

    const uint32_t effectAge = millis() - s_weaponEffectStartMs;
    const bool targetImpact = s_lockedTarget >= 0;
    if (s_weapon == 0 && effectAge < CANNON_EFFECT_MS)
    {
        // Fire from the actual shoulder muzzles toward the boresight. Spread is
        // perpendicular to that line, so it never drifts upward in screen space.
        const int muzzleMidX = (cannonTipL.x + cannonTipR.x) / 2;
        const int muzzleMidY = (cannonTipL.y + cannonTipR.y) / 2;
        const float boreDx = (float)(aimX - muzzleMidX);
        const float boreDy = (float)(aimY - muzzleMidY);
        const float boreLen = max(1.0f, sqrtf(boreDx * boreDx + boreDy * boreDy));
        const float perpX = -boreDy / boreLen;
        const float perpY =  boreDx / boreLen;
        for (int i = 0; i < 5; ++i)
        {
            const int progress = (int)(effectAge * 2 + i * 37) % 220;
            const int spread = (i - 2) * 3;
            const int tx = aimX + (int)(perpX * spread);
            const int ty = aimY + (int)(perpY * spread);
            const MechaPoint& muzzle = (i & 1) ? cannonTipR : cannonTipL;
            const int x0 = muzzle.x + (tx - muzzle.x) * progress / 255;
            const int y0 = muzzle.y + (ty - muzzle.y) * progress / 255;
            const int x1 = muzzle.x + (tx - muzzle.x) * min(progress + 38, 255) / 255;
            const int y1 = muzzle.y + (ty - muzzle.y) * min(progress + 38, 255) / 255;
            const int hotX = x0 + (x1 - x0) * 2 / 3;
            const int hotY = y0 + (y1 - y0) * 2 / 3;
            s_frame.drawLine(x0, y0, x1, y1, 0xF980);
            s_frame.drawLine(hotX, hotY, x1, y1,
                             (i & 1) ? YELLOW : WHITE);
            s_frame.drawPixel(x1, y1, WHITE);
        }
        // Alternating twin gun ports pulse with the 120 ms firing cadence.
        const int flashSide = ((effectAge / 42) & 1) ? 1 : -1;
        const MechaPoint& flashMuzzle = flashSide > 0 ? cannonTipR : cannonTipL;
        const int flashX = flashMuzzle.x;
        const int flashY = flashMuzzle.y;
        s_frame.fillCircle(flashX, flashY, 2, ORANGE);
        s_frame.drawPixel(flashX, flashY - 2, WHITE);
        s_frame.drawPixel(flashX - 1, flashY - 1, YELLOW);
        s_frame.drawPixel(flashX + 1, flashY - 1, YELLOW);

        if (targetImpact)
        {
            // Rapid white-hot hit flicker with four cheap fixed sparks. The
            // phase keeps changing throughout a held burst instead of
            // allocating persistent particle objects for every bullet.
            const int hitPhase = (millis() / 32) & 3;
            const int hitRadius = 2 + (hitPhase & 1);
            s_frame.fillCircle(aimX, aimY, hitRadius, hitPhase & 1 ? WHITE : YELLOW);
            s_frame.drawCircle(aimX, aimY, 4 + hitPhase, ORANGE);
            const int spark = 4 + hitPhase * 2;
            s_frame.drawPixel(aimX + spark, aimY - 1, WHITE);
            s_frame.drawPixel(aimX - spark, aimY + 1, YELLOW);
            s_frame.drawPixel(aimX + 1, aimY + spark, ORANGE);
            s_frame.drawPixel(aimX - 1, aimY - spark, ORANGE);
        }
    }
    else if (s_weapon == 1 && s_weaponEffectStartMs != 0 &&
             effectAge < MISSILE_FLIGHT_MS)
    {
        // Pull the visual detonation toward the horizon so it reads as a
        // deeper impact rather than an explosion pasted on the near reticle.
        const int missileImpactX = aimX + (CX - aimX) * 18 / 100;
        const int missileImpactY = aimY + (HORIZON_Y - aimY) * 28 / 100;
        // Twin missiles spend longer visibly travelling: a slow outward kick,
        // then progressive homing all the way into the common impact point.
        if (effectAge < MISSILE_FLIGHT_MS)
        {
            const int missilePhase = min((int)(effectAge * 255 / MISSILE_FLIGHT_MS), 255);
            const float t = missilePhase / 255.0f;
            // Non-zero launch velocity removes sluggishness; the quadratic
            // term then produces a much faster terminal homing pass.
            const int p = (int)((0.16f * t + 0.84f * t * t) * 255.0f);
            for (int side = -1; side <= 1; side += 2)
            {
                const int startX = sx + side * 9;
                const int startY = sy + 4;
                const int controlX = sx + side * 52;
                const int controlY = sy + 26;
                const int q = 255 - p;
                const int mx = (q*q*startX + 2*q*p*controlX + p*p*missileImpactX) / 65025;
                const int my = (q*q*startY + 2*q*p*controlY + p*p*missileImpactY) / 65025;
                const int tailP = max(0, p - 30);
                const int tailQ = 255 - tailP;
                const int tx = (tailQ*tailQ*startX + 2*tailQ*tailP*controlX
                               + tailP*tailP*missileImpactX) / 65025;
                const int ty = (tailQ*tailQ*startY + 2*tailQ*tailP*controlY
                               + tailP*tailP*missileImpactY) / 65025;
                const int dx = mx - tx;
                const int dy = my - ty;
                const int magnitude = max(1, max(abs(dx), abs(dy)));
                const int ux = dx * 3 / magnitude;
                const int uy = dy * 3 / magnitude;
                const int px = -dy * 2 / magnitude;
                const int py =  dx * 2 / magnitude;
                const int noseX = mx + ux / 2;
                const int noseY = my + uy / 2;
                const int baseX = mx - ux;
                const int baseY = my - uy;
                s_frame.fillTriangle(noseX, noseY,
                                     baseX + px, baseY + py,
                                     baseX - px, baseY - py, 0xC618);
                s_frame.drawLine(mx, my, noseX, noseY, WHITE);
                s_frame.drawPixel(noseX, noseY, CYAN);
                const int plume1X = baseX - ux / 2;
                const int plume1Y = baseY - uy / 2;
                const int plume2X = baseX - ux;
                const int plume2Y = baseY - uy;
                const int plume3X = baseX - ux - ux / 2;
                const int plume3Y = baseY - uy - uy / 2;
                s_frame.fillCircle(plume1X, plume1Y, 2, WHITE);
                s_frame.fillCircle(plume2X, plume2Y, 2, CYAN);
                s_frame.fillCircle(plume3X, plume3Y, 1, 0x03EF);
                s_frame.drawPixel(tx, ty, 0x0210);
            }
        }
    }
    else if (s_weapon == 2 && s_beamState == BeamState::FIRING)
    {
        // Ignition travels from the emitter to the reticle for 140 ms before
        // the sustained beam stabilizes, avoiding a full-length line popping
        // into existence on the first firing frame.
        const uint32_t beamAge = millis() - s_beamStateSinceMs;
        const int sweepX = (int)s_freeAimX;
        const int sweepY = (int)s_freeAimY;
        bool sweepImpact = false;
        for (int i = 0; i < TARGET_COUNT; ++i)
        {
            if (!s_targetBeamEligible[i]) continue;
            const int dx = s_targetScreenX[i] - sweepX;
            const int dy = s_targetScreenY[i] - sweepY;
            const int impactRadius = i == MISSION_TARGET ? 32 : 22;
            if (dx*dx + dy*dy <= impactRadius*impactRadius)
            {
                sweepImpact = true;
                break;
            }
        }
        const int ignition = min(255, (int)(beamAge * 255 / BEAM_IGNITION_MS));
        const int endX = sx + (sweepX - sx) * ignition / 255;
        const int endY = sy - 8 + (sweepY - (sy - 8)) * ignition / 255;
        const int endSpread = 4 * ignition / 255;
        const int pulse = (beamAge / 45) & 1;
        const int beamDx = endX - sx;
        const int beamDy = endY - (sy - 8);
        const int beamNorm = max(1, max(abs(beamDx), abs(beamDy)));
        const int haloPx = -beamDy * (pulse ? 3 : 2) / beamNorm;
        const int haloPy =  beamDx * (pulse ? 3 : 2) / beamNorm;

        // A compact emitter bloom makes charge release feel energetic.
        const int bloom = beamAge < BEAM_IGNITION_MS ? 4 + ignition / 64 : 3;
        s_frame.fillCircle(sx, sy - 8, bloom, 0x03EF);
        s_frame.fillCircle(sx, sy - 8, max(1, bloom - 2), WHITE);

        // A dim transverse sheath sits outside the cyan rails. Two extra
        // lines add apparent volume without expensive alpha blending.
        s_frame.drawLine(sx - 8 + haloPx, sy - 5 + haloPy,
                         endX - endSpread + haloPx, endY + haloPy, 0x01B6);
        s_frame.drawLine(sx + 8 - haloPx, sy - 5 - haloPy,
                         endX + endSpread - haloPx, endY - haloPy, 0x01B6);
        s_frame.drawLine(sx - 8, sy - 5, endX - endSpread, endY, 0x03EF);
        s_frame.drawLine(sx + 8, sy - 5, endX + endSpread, endY, 0x03EF);
        s_frame.drawLine(sx - 5, sy - 6, endX - endSpread / 2, endY, CYAN);
        s_frame.drawLine(sx + 5, sy - 6, endX + endSpread / 2, endY, CYAN);
        s_frame.drawLine(sx - 1, sy - 8, endX, endY, WHITE);
        s_frame.drawLine(sx + 1, sy - 8, endX, endY, WHITE);
        s_frame.drawLine(sx, sy - 9, endX, endY, pulse ? YELLOW : WHITE);

        // Three tiny energy knots travel down the stabilized beam. Their
        // staggered phase gives motion to a sustained shot for only a handful
        // of pixels per frame.
        if (ignition >= 250)
        {
            const int knotPhase = (beamAge * 3) & 255;
            for (int k = 0; k < 3; ++k)
            {
                const int p = (knotPhase + k * 85) & 255;
                const int side = (k & 1) ? 1 : -1;
                const int kx = sx + beamDx * p / 255 + haloPx * side;
                const int ky = sy - 8 + beamDy * p / 255 + haloPy * side;
                s_frame.drawPixel(kx, ky, k == 1 ? WHITE : CYAN);
                s_frame.drawPixel(kx + side, ky, 0x03EF);
            }
        }

        // Impact flare starts only once ignition actually reaches the target.
        if (ignition >= 250)
        {
            const int flare = pulse ? 7 : 5;
            s_frame.drawLine(sweepX - flare, sweepY, sweepX - 2, sweepY, WHITE);
            s_frame.drawLine(sweepX + 2, sweepY, sweepX + flare, sweepY, WHITE);
            s_frame.drawLine(sweepX, sweepY - flare, sweepX, sweepY - 2, CYAN);
            s_frame.drawLine(sweepX, sweepY + 2, sweepX, sweepY + flare, CYAN);

            if (sweepImpact)
            {
                // Sustained plasma contact: two breathing rings surround a
                // hot core while four alternating fragments spit outward.
                const int plasmaPhase = (beamAge / 38) & 3;
                s_frame.fillCircle(sweepX, sweepY, 3, plasmaPhase & 1 ? WHITE : CYAN);
                s_frame.drawCircle(sweepX, sweepY, 7 + plasmaPhase, WHITE);
                s_frame.drawCircle(sweepX, sweepY, 11 - plasmaPhase, 0x03EF);
                const int diamond = 5 + plasmaPhase;
                s_frame.drawLine(sweepX, sweepY - diamond,
                                 sweepX + diamond, sweepY, YELLOW);
                s_frame.drawLine(sweepX + diamond, sweepY,
                                 sweepX, sweepY + diamond, ORANGE);
                s_frame.drawLine(sweepX, sweepY + diamond,
                                 sweepX - diamond, sweepY, YELLOW);
                s_frame.drawLine(sweepX - diamond, sweepY,
                                 sweepX, sweepY - diamond, ORANGE);
                const int spit = 9 + plasmaPhase * 2;
                s_frame.drawPixel(sweepX + spit, sweepY + 2, CYAN);
                s_frame.drawPixel(sweepX - spit, sweepY - 2, WHITE);
                s_frame.drawPixel(sweepX + 2, sweepY - spit, YELLOW);
                s_frame.drawPixel(sweepX - 2, sweepY + spit, CYAN);
            }
        }
    }

    if (s_weapon == 4 && s_bladeSwingStartMs != 0)
    {
        const uint32_t bladeAge = drawNow - s_bladeSwingStartMs;
        const uint32_t swingDuration = s_bladeSwingCharged
            ? BLADE_CHARGED_SWING_MS
            : (s_bladeSwingStage == 2 ? BLADE_SWING_MS : BLADE_QUICK_SWING_MS);
        if (bladeAge < swingDuration)
        {
            // Complete the physical cut in the first 65% of the effect. The
            // short remainder is afterimage only, producing a fast "snap".
            const uint32_t motionDuration = swingDuration * 58 / 100;
            const int phase = min(255, (int)(bladeAge * 255 / max<uint32_t>(1, motionDuration)));
            const int baseX = sx;
            const int baseY = sy - 9;
            const int t = phase;
            auto drawBladeStroke = [&](int hx, int hy, int tx, int ty,
                                       int oldTx, int oldTy, uint16_t edge) {
                // Wide luminous edge and separated afterimages make the blade
                // read as a fast melee arc rather than a gun beam.
                s_frame.drawLine(oldTx - 2, oldTy, tx - 2, ty, 0x0320);
                s_frame.drawLine(oldTx + 2, oldTy, tx + 2, ty, 0x04C0);
                s_frame.drawLine(hx - 2, hy, tx - 2, ty, 0x0320);
                s_frame.drawLine(hx - 1, hy, tx - 1, ty, GREEN);
                s_frame.drawLine(hx, hy, tx, ty, WHITE);
                s_frame.drawLine(hx + 1, hy, tx + 1, ty, GREEN);
                s_frame.drawLine(hx + 2, hy, tx + 2, ty, 0x0320);
                s_frame.drawLine(oldTx, oldTy, tx, ty, edge);
                s_frame.fillCircle(tx, ty, 2, GREEN);
                s_frame.drawPixel(tx, ty, WHITE);
            };

            if (s_bladeSwingStage == 0)
            {
                const int tx = baseX + 39 - t * 72 / 255;
                const int ty = baseY - 38 + t * 72 / 255;
                const int oldT = max(0, t - 62);
                const int ox = baseX + 39 - oldT * 72 / 255;
                const int oy = baseY - 38 + oldT * 72 / 255;
                drawBladeStroke(baseX - 5, baseY + 2, tx, ty, ox, oy, GREEN);
                s_frame.drawLine(baseX + 44, baseY - 43,
                                 tx + 5, ty - 5, 0x04C0);
            }
            else if (s_bladeSwingStage == 1)
            {
                const int tx = baseX - 47 + t * 94 / 255;
                const int ty = baseY + (int)(sinf(t * 3.14159f / 255.0f) * -8.0f);
                const int oldT = max(0, t - 66);
                const int ox = baseX - 47 + oldT * 94 / 255;
                const int oy = baseY + (int)(sinf(oldT * 3.14159f / 255.0f) * -8.0f);
                drawBladeStroke(baseX + 5, baseY + 3, tx, ty, ox, oy, GREEN);
                s_frame.drawLine(baseX - 52, baseY + 5,
                                 tx - 5, ty + 5, 0x04C0);
            }
            else if (s_bladeSwingCharged)
            {
                // Charged-only finisher: the two sabers sweep far past one
                // another and leave a compact crossing shock at the target.
                // A handful of lines keeps this cheaper than particles.
                const int reach = 51;
                const int spread = 42 - t * 70 / 255;
                const int drop = -43 + t * 82 / 255;
                const int impactX = s_bladeDashTargetX >= 0
                    ? s_bladeDashTargetX : aimX;
                const int impactY = s_bladeDashTargetX >= 0
                    ? s_bladeDashTargetY : aimY;
                drawBladeStroke(baseX - 8, baseY + 4,
                                baseX + spread, baseY + drop,
                                baseX + spread + 14, baseY + drop - 14, GREEN);
                drawBladeStroke(baseX + 8, baseY + 4,
                                baseX - spread, baseY + drop,
                                baseX - spread - 14, baseY + drop - 14, GREEN);
                if (t > 112 && bladeAge < 260)
                {
                    const int slash = min(reach, 10 + (t - 112) * 41 / 143);
                    s_frame.drawLine(impactX - slash, impactY - slash,
                                     impactX + slash, impactY + slash, GREEN);
                    s_frame.drawLine(impactX - slash, impactY - slash + 2,
                                     impactX + slash, impactY + slash + 2, WHITE);
                    s_frame.drawLine(impactX + slash, impactY - slash,
                                     impactX - slash, impactY + slash, GREEN);
                    s_frame.drawLine(impactX + slash, impactY - slash + 2,
                                     impactX - slash, impactY + slash + 2, WHITE);
                    const int burst = 4 + (t - 112) / 24;
                    s_frame.drawCircle(impactX, impactY, burst, CYAN);
                    s_frame.fillCircle(impactX, impactY, 2, WHITE);
                    for (int lane = -1; lane <= 1; ++lane)
                        s_frame.drawLine(baseX + lane * 20, baseY + 34,
                                         impactX + lane * 6, impactY + 10,
                                         lane == 0 ? WHITE : DARKCYAN);
                }
            }
            else
            {
                const int reach = 50;
                const int spread = 42 - t * 70 / 255;
                const int drop = -34 + t * 68 / 255;
                const int impactX = s_bladeDashTargetX >= 0
                    ? s_bladeDashTargetX : aimX;
                const int impactY = s_bladeDashTargetX >= 0
                    ? s_bladeDashTargetY : aimY;
                drawBladeStroke(baseX - 7, baseY + 2,
                                baseX + spread, baseY + drop,
                                baseX + spread + 12, baseY + drop - 12, GREEN);
                drawBladeStroke(baseX + 7, baseY + 2,
                                baseX - spread, baseY + drop,
                                baseX - spread - 12, baseY + drop - 12, GREEN);
                if (t > 118)
                {
                    const int slash = min(reach + 12, 12 + (t - 118) * 50 / 137);
                    s_frame.drawLine(impactX - slash, impactY - slash,
                                     impactX + slash, impactY + slash, 0x0320);
                    s_frame.drawLine(impactX - slash, impactY - slash + 2,
                                     impactX + slash, impactY + slash + 2, WHITE);
                    s_frame.drawLine(impactX + slash, impactY - slash,
                                     impactX - slash, impactY + slash, GREEN);
                    s_frame.drawLine(impactX + slash, impactY - slash + 2,
                                     impactX - slash, impactY + slash + 2, WHITE);
                    // Short convergence streaks sell the charged gap-closing
                    // dash without adding persistent full-screen particles.
                    const int streak = 10 + (t - 118) / 5;
                    for (int k = -1; k <= 1; ++k)
                    {
                        const int lane = k * 18;
                        s_frame.drawLine(baseX + lane, baseY + 30,
                                         baseX + lane / 2, baseY + 30 - streak,
                                         k == 0 ? WHITE : DARKCYAN);
                    }
                }
            }

            if (s_bladeSwingStage == 2 && !s_bladeSwingCharged &&
                bladeAge > 72 && bladeAge < 145)
            {
                const int burst = 5 + (bladeAge - 72) / 10;
                const int impactX = s_bladeDashTargetX >= 0
                    ? s_bladeDashTargetX : aimX;
                const int impactY = s_bladeDashTargetX >= 0
                    ? s_bladeDashTargetY : aimY;
                s_frame.fillCircle(impactX, impactY, 3, WHITE);
                s_frame.drawLine(impactX - burst, impactY - burst,
                                 impactX + burst, impactY + burst, WHITE);
                s_frame.drawLine(impactX + burst, impactY - burst,
                                 impactX - burst, impactY + burst, CYAN);
                for (int spark = 0; spark < 6; ++spark)
                {
                    const int dirX = ((spark * 37) % 17) - 8;
                    const int dirY = ((spark * 23) % 15) - 7;
                    s_frame.drawLine(impactX + dirX, impactY + dirY,
                                     impactX + dirX * 2, impactY + dirY * 2,
                                     (spark & 1) ? YELLOW : CYAN);
                }
            }
        }
        else
        {
            s_bladeSwingStartMs = 0;
        }
    }

    // Lightweight independent multi-lock swarm. Each target receives a left
    // and right missile with a different lateral arc, then both converge.
    for (const MultiMissileShot& shot : s_multiShots)
    {
        if (shot.startMs == 0) continue;
        // Eight visible rounds already read as a dense swarm. Remaining
        // simulated rounds still home and hit without extra raster work.
        if (shot.ordinal >= 8) continue;
        const uint32_t age = drawNow - shot.startMs;
        if (age >= MULTI_MISSILE_FLIGHT_MS) continue;
        const float t = age / (float)MULTI_MISSILE_FLIGHT_MS;
        const float eased = 0.38f * t + 0.62f * t * t;
        const int p = min(255, (int)(eased * 255.0f));
        const int q = 255 - p;
        const int stagger = shot.ordinal % 3;
        const int startX = sx + shot.side * (7 + stagger * 3);
        const int startY = sy + 1 + stagger * 2;
        const int controlX = sx + shot.side * (34 + stagger * 7);
        const int controlY = sy + 18 - stagger * 5;
        const int mx = (q*q*startX + 2*q*p*controlX + p*p*shot.impactX) / 65025;
        const int my = (q*q*startY + 2*q*p*controlY + p*p*shot.impactY) / 65025;
        const int tailP = max(0, p - 22);
        const int tailQ = 255 - tailP;
        const int tx = (tailQ*tailQ*startX + 2*tailQ*tailP*controlX
                       + tailP*tailP*shot.impactX) / 65025;
        const int ty = (tailQ*tailQ*startY + 2*tailQ*tailP*controlY
                       + tailP*tailP*shot.impactY) / 65025;
        const int dx = mx - tx;
        const int dy = my - ty;
        const int mag = max(1, max(abs(dx), abs(dy)));
        const int ux = dx * 3 / mag;
        const int uy = dy * 3 / mag;
        s_frame.drawLine(mx, my, tx, ty,
                         (shot.ordinal & 1) ? MAGENTA : CYAN);
        s_frame.drawPixel(mx + ux, my + uy, WHITE);
        s_frame.drawPixel(mx, my, 0xC618);
        s_frame.drawPixel(mx - ux, my - uy, ORANGE);
    }

    // Missile detonations are independent persistent effects. A new salvo may
    // already be in flight while one or more previous blast rings remain.
    static const int8_t blastSparkX[6] = { 3, 2, -2, -3, -2, 2 };
    static const int8_t blastSparkY[6] = { 0, 2,  2,  0, -2,-2 };
    static const int8_t blastDebrisX[8] = { 4, 3, 0,-3,-4,-3, 0, 3 };
    static const int8_t blastDebrisY[8] = { 0, 3, 4, 3, 0,-3,-4,-3 };
    for (MissileBlast& blast : s_missileBlasts)
    {
        if (blast.startMs == 0) continue;
        const uint32_t blastAgeMs = drawNow - blast.startMs;
        if (blastAgeMs >= MISSILE_BLAST_MS) { blast.startMs = 0; continue; }
        const int age = min(18, (int)(blastAgeMs / 30));
        const int radius = 2 + age * 3 / 4;
        s_frame.fillCircle(blast.x, blast.y, max(1, 3 - age / 4), WHITE);
        s_frame.drawCircle(blast.x, blast.y, radius, age < 5 ? YELLOW : ORANGE);
        for (int i = 0; i < 6; ++i)
            s_frame.drawPixel(blast.x + blastSparkX[i] * age / 3,
                              blast.y + blastSparkY[i] * age / 3,
                              i & 1 ? ORANGE : YELLOW);
        if (blast.heavy)
        {
            const int outerRadius = radius + 4 + age / 2;
            s_frame.drawCircle(blast.x, blast.y, outerRadius,
                               age < 5 ? WHITE : 0xF980);
            s_frame.fillCircle(blast.x, blast.y, max(1, 4 - age / 2),
                               age < 4 ? WHITE : YELLOW);
            for (int i = 0; i < 8; ++i)
            {
                const int distance = 2 + age * (1 + (i & 1)) / 2;
                s_frame.drawPixel(blast.x + blastDebrisX[i] * distance / 4,
                                  blast.y + blastDebrisY[i] * distance / 4,
                                  i & 1 ? WHITE : ORANGE);
            }
        }
    }

    if (s_rearMissileActive)
    {
        const bool multiAxisCinematic = s_storyMode &&
            s_demoStoryPhase == DemoStoryPhase::IMPULSE_REVEAL &&
            s_demoRevealMissileAlerted;
        const int ttiTenths = max(0, (int)(REAR_MISSILE_APPROACH_MS -
            s_rearMissileElapsedMs) / 100);
        const uint16_t warning = ((drawNow / 95) & 1) ? RED : WHITE;
        s_frame.drawRect(1, 1, W - 2, H - 2, RED);
        s_frame.fillRect(CX - 55, 27, 110, 25, BLACK);
        s_frame.drawRect(CX - 55, 27, 110, 25, RED);
        s_frame.setTextColor(warning, BLACK);
        s_frame.setCursor(CX - 43, 29);
        s_frame.print("MISSILE ALERT");
        s_frame.setCursor(CX - 34, 40);
        if (s_flareInterceptPending) s_frame.print("FLARE TRACK");
        else if (multiAxisCinematic) s_frame.print("MULTI AXIS LOCK");
        else s_frame.printf("REAR %d.%ds", ttiTenths / 10, ttiTenths % 10);
        // A single rear approach cue avoids a new projectile sprite.
        const int cueY = H - 11 - (int)(s_rearMissileElapsedMs * 24 /
            REAR_MISSILE_APPROACH_MS);
        s_frame.drawLine(CX - 7, cueY + 5, CX, cueY - 3, warning);
        s_frame.drawLine(CX, cueY - 3, CX + 7, cueY + 5, warning);
        if (multiAxisCinematic)
        {
            // Four independent bearing brackets make the scripted threat read
            // as a coordinated salvo rather than one invisible rear missile.
            const int pulse = 3 + (drawNow / 95) % 4;
            s_frame.drawLine(CX - 7, 17 + pulse, CX, 10 + pulse, warning);
            s_frame.drawLine(CX + 7, 17 + pulse, CX, 10 + pulse, warning);
            s_frame.drawLine(CX - 7, H - 17 - pulse, CX, H - 10 - pulse, warning);
            s_frame.drawLine(CX + 7, H - 17 - pulse, CX, H - 10 - pulse, warning);
            s_frame.drawLine(17 + pulse, H / 2 - 7, 10 + pulse, H / 2, warning);
            s_frame.drawLine(17 + pulse, H / 2 + 7, 10 + pulse, H / 2, warning);
            s_frame.drawLine(W - 17 - pulse, H / 2 - 7, W - 10 - pulse,
                             H / 2, warning);
            s_frame.drawLine(W - 17 - pulse, H / 2 + 7, W - 10 - pulse,
                             H / 2, warning);
            s_frame.setTextColor(warning, BLACK);
            s_frame.setCursor(4, 15); s_frame.print("Y-");
            s_frame.setCursor(4, H - 24); s_frame.print("Y+");
            s_frame.setCursor(4, H / 2 - 4); s_frame.print("X-");
            s_frame.setCursor(W - 16, H / 2 - 4); s_frame.print("X+");
        }
    }
    if ((int32_t)(s_flareVisualUntilMs - drawNow) > 0)
    {
        // Rear-facing counterpart of the MULTI salvo: four left/right pairs
        // kick outward on quadratic arcs, then home into the inbound missile.
        // No projectile objects or additional canvas are allocated.
        const int flightMs = s_storyMode &&
            s_demoStoryPhase == DemoStoryPhase::IMPULSE_REVEAL ? 1100 : 590;
        const int age = min(flightMs + 250,
                            (int)(drawNow - s_flareLungeStartMs));
        const bool multiAxisFlare = s_storyMode &&
            s_demoStoryPhase == DemoStoryPhase::IMPULSE_REVEAL;
        static const int8_t targetDx[4] = {-42, 42, -32, 32};
        static const int8_t targetDy[4] = {-28, -24, 30, 34};
        for (int i = 0; i < 8; ++i)
        {
            const int lane = i & 3;
            const int shotAge = age - lane * 20;
            if (shotAge < 0 || shotAge > flightMs) continue;
            const int side = i < 4 ? -1 : 1;
            const float t = shotAge / (float)flightMs;
            const int p = min(255, (int)((0.38f * t + 0.62f * t * t) * 255.0f));
            const int q = 255 - p;
            const int tailP = max(0, p - 24);
            const int tailQ = 255 - tailP;
            const int startX = s_flareOriginX + side * (6 + lane * 2);
            const int startY = s_flareOriginY + 2 + lane;
            const int controlX = s_flareOriginX + side * (36 + lane * 8);
            const int controlY = s_flareOriginY + 15 + lane * 3;
            const int endX = multiAxisFlare
                ? constrain(s_flareOriginX + targetDx[lane], 12, W - 12)
                : s_flareHomingBurst ? s_flareTargetX :
                s_flareOriginX + side * (39 + lane * 8);
            const int endY = multiAxisFlare
                ? constrain(s_flareOriginY + targetDy[lane], 18, H - 12)
                : s_flareHomingBurst ? s_flareTargetY :
                s_flareOriginY + 23 + lane * 2;
            const int mx = (q*q*startX + 2*q*p*controlX + p*p*endX) / 65025;
            const int my = (q*q*startY + 2*q*p*controlY + p*p*endY) / 65025;
            const int tx = (tailQ*tailQ*startX + 2*tailQ*tailP*controlX +
                            tailP*tailP*endX) / 65025;
            const int ty = (tailQ*tailQ*startY + 2*tailQ*tailP*controlY +
                            tailP*tailP*endY) / 65025;
            s_frame.drawLine(tx, ty, mx, my, (i & 1) ? ORANGE : CYAN);
            s_frame.drawPixel(mx, my, WHITE);
            s_frame.drawPixel(mx + side, my - 1, YELLOW);
        }
        if (age < 190)
        {
            const int fx = s_flareOriginX;
            const int fy = s_flareOriginY + 6;
            const int ray = 5 + age / 17;
            s_frame.fillCircle(fx, fy, max(1, 5 - age / 45), WHITE);
            s_frame.drawLine(fx - ray, fy, fx + ray, fy,
                             age < 90 ? WHITE : YELLOW);
            s_frame.drawLine(fx, fy - ray / 2, fx, fy + ray / 2, YELLOW);
            s_frame.drawLine(fx - ray / 2, fy - ray / 2,
                             fx + ray / 2, fy + ray / 2, ORANGE);
            s_frame.drawLine(fx + ray / 2, fy - ray / 2,
                             fx - ray / 2, fy + ray / 2, ORANGE);
        }
    }
    if ((int32_t)(s_flareInterceptUntilMs - drawNow) > 0)
    {
        const int duration = s_storyMode &&
            s_demoStoryPhase == DemoStoryPhase::IMPULSE_REVEAL ? 1200 : 750;
        const int visibleMs = duration == 1200 ? 850 : 450;
        const int age = duration - (int)(s_flareInterceptUntilMs - drawNow);
        if (age < visibleMs)
        {
            const int x = s_flareDetonationX;
            const int y = s_flareDetonationY;
            const int r = 5 + age / 18;
            const uint16_t hot = age < 170 ? WHITE : YELLOW;
            if (age < 110)
                s_frame.drawRect(3, 3, W - 6, H - 6,
                                 age < 55 ? WHITE : YELLOW);
            // Two octagonal shock fronts plus diagonal spall read as a much
            // larger blast without costly alpha blending or wide fills.
            for (int side = -1; side <= 1; side += 2)
            {
                const int radius = r + (side > 0 ? 6 : 0);
                s_frame.drawLine(x - radius, y, x - radius / 2,
                                 y - radius / 2, hot);
                s_frame.drawLine(x - radius / 2, y - radius / 2,
                                 x + radius / 2, y - radius / 2, hot);
                s_frame.drawLine(x + radius / 2, y - radius / 2,
                                 x + radius, y, hot);
                s_frame.drawLine(x + radius, y, x + radius / 2,
                                 y + radius / 2, ORANGE);
                s_frame.drawLine(x + radius / 2, y + radius / 2,
                                 x - radius / 2, y + radius / 2, ORANGE);
                s_frame.drawLine(x - radius / 2, y + radius / 2,
                                 x - radius, y, ORANGE);
            }
        }
    }
    if ((int32_t)(s_flareVisualUntilMs - drawNow) > 0 ||
        (int32_t)(s_flareInterceptUntilMs - drawNow) > 0)
    {
        s_frame.fillRect(CX - 45, 30, 90, 12, BLACK);
        s_frame.setTextColor((int32_t)(s_flareInterceptUntilMs - drawNow) > 0
                             ? WHITE : YELLOW, BLACK);
        s_frame.setCursor(CX - 37, 32);
        s_frame.print((int32_t)(s_flareInterceptUntilMs - drawNow) > 0
                       ? "INTERCEPT" : "FLARE SALVO");
    }
    // A rear missile hit walks across the armor in separate bursts. Keep the
    // effect attached to the moving craft and use only a few fixed primitives
    // per frame; full-screen flashes would stall the Core2 LCD transfer.
    const uint32_t rearHitAge = drawNow - s_rearMissileHitStartMs;
    const bool rearHitVisible = s_rearMissileHitStartMs != 0 && rearHitAge < 720;
    if (rearHitVisible)
    {
        static const int8_t burstX[5] = {-22, 20, -8, 19, -3};
        static const int8_t burstY[5] = {-20, -15, -4, 8, 18};
        for (int i = 0; i < 5; ++i)
        {
            const int age = (int)rearHitAge - i * 95;
            if (age < 0 || age >= 280) continue;
            const int x = sx + burstX[i];
            const int y = sy + burstY[i];
            const int radius = 3 + age / 42;
            const uint16_t edge = age < 105 ? YELLOW : ORANGE;
            s_frame.drawCircle(x, y, radius, edge);
            if (age < 95) s_frame.fillCircle(x, y, 3, WHITE);
            s_frame.drawLine(x - radius - 3, y, x - radius, y, edge);
            s_frame.drawLine(x + radius, y, x + radius + 3, y, edge);
            s_frame.drawLine(x, y - radius - 3, x, y - radius, WHITE);
            s_frame.drawLine(x, y + radius, x, y + radius + 3, ORANGE);
        }
    }
    if (s_rollActive)
    {
        s_frame.drawRect(1, 1, W - 2, H - 2, ORANGE);
    }
    if (collisionShake)
    {
        const uint16_t hitColor = rearHitVisible
            ? (((drawNow / 70) & 1) ? WHITE : YELLOW)
            : (((drawNow / 70) & 1) ? RED : ORANGE);
        s_frame.drawRect(2, 2, W - 4, H - 4, hitColor);
        s_frame.drawRect(4, 4, W - 8, H - 8, hitColor);
        // Damage state and hull strength are persistent in the lower rail.
    }
}

uint32_t pushFullFrame()
{
    // Keep the single 49 KiB LCD transaction contiguous. At loop priority 1,
    // the priority-5 MAX3421 task could repeatedly pre-empt/reacquire the
    // shared SPI mutex and stretch a normal ~21 ms push to 160-360 ms. Raising
    // priority only around this already-mutex-protected transfer lets the
    // current SPI owner finish through priority inheritance, then completes
    // one LCD burst without fragmentation. USB work resumes immediately after.
    const UBaseType_t normalPriority = uxTaskPriorityGet(nullptr);
    const UBaseType_t transferPriority = normalPriority < 6 ? 6 : normalPriority;
    vTaskPrioritySet(nullptr, transferPriority);
    napetune_lcd_acquire();
    s_frame.pushSprite(VIEW_X, VIEW_Y);
    napetune_lcd_release();
    vTaskPrioritySet(nullptr, normalPriority);
    return W * H;
}

void drawExternalHud(bool force = false)
{
    const uint32_t now = millis();
    const bool enemyMissile = enemyMissileThreatActive();
    const bool missileAlert = s_rearMissileActive || enemyMissile;
    // The side HUD performs many small LCD transactions. Mission damage can
    // mark it dirty every 85-120 ms, but must not bypass this hard rate limit;
    // otherwise those redraws cause long SPI/LCD stalls during weapon fire.
    if (!force && now - s_lastExternalHudMs < 500 &&
        s_rearMissileActive == s_rearMissileHudDrawn &&
        enemyMissile == s_enemyMissileHudDrawn) return;
    s_lastExternalHudMs = now; s_externalHudDirty = false;
    s_rearMissileHudDrawn = s_rearMissileActive;
    s_enemyMissileHudDrawn = enemyMissile;

    s_hudSpeed += (s_currentSpeed * 10.0f - s_hudSpeed) * 0.24f;
    // ALT is the actual simulation altitude, not a camera-animation estimate.
    s_hudAltitude = s_altitudeM;
    const int speedValue = (int)s_hudSpeed;
    const int altitude = (int)s_hudAltitude;
    const int heading = ((int)(180 + s_courseHeadingOffset
                              + s_routeLeadX * 18.0f + s_shipX * 7.0f) + 720) % 360;

    // The HUD consists of many tiny display commands. Although it runs only
    // every 500 ms, leaving these commands at loop priority allowed the USB
    // task to fragment one HUD refresh into a long invisible frame gap. Keep
    // the complete HUD refresh atomic just like the central sprite transfer.
    const UBaseType_t normalPriority = uxTaskPriorityGet(nullptr);
    const UBaseType_t hudPriority = normalPriority < 6 ? 6 : normalPriority;
    vTaskPrioritySet(nullptr, hudPriority);
    napetune_lcd_acquire();
    M5.Display.fillRect(0, 0, 320, 24, BLACK);
    M5.Display.fillRect(0, 216, 320, 24, BLACK);
    M5.Display.fillRect(0, 24, 32, 192, BLACK);
    M5.Display.fillRect(288, 24, 32, 192, BLACK);
    if (s_flightHudEnabled)
    {
        const uint16_t hud = missileAlert ? RED : GREEN;
        M5.Display.setTextSize(1); M5.Display.setTextColor(hud, BLACK);
        M5.Display.setCursor(4, 8); M5.Display.printf("WPN:%s", weaponName());
        M5.Display.setCursor(73, 8);
        if (s_weapon == 0) M5.Display.print("AM:--");
        else M5.Display.print("AM:INF");
        M5.Display.setTextColor(missileAlert ? RED :
            (s_missionComplete ? GREEN : MAGENTA), BLACK);
        M5.Display.setCursor(113, 8);
        if (s_storyMode && s_demoStoryPhase == DemoStoryPhase::COMBAT)
            M5.Display.printf("K:%02u/%u", (unsigned)s_demoKills,
                              (unsigned)DEMO_KILLS_TO_UNLOCK);
        else if (s_storyMode && s_demoStoryPhase == DemoStoryPhase::ESCAPE)
            M5.Display.printf("EXIT:%02u", (unsigned)s_demoLastCountdown);
        else if (s_storyMode || s_demoMode) M5.Display.print("OBJ:---");
        else if (s_missionComplete) M5.Display.print("OBJ:CLEAR");
        else M5.Display.printf("OBJ:%02d", s_missionTargetHp);
        M5.Display.setTextColor(hud, BLACK);
        M5.Display.setCursor(182, 8); M5.Display.printf("HDG:%03d", heading);
        M5.Display.setCursor(250, 8); M5.Display.printf("THR:%d", (int)s_throttlePercent);
        M5.Display.setCursor(4, 30); M5.Display.print("SPD");
        M5.Display.setCursor(292, 30); M5.Display.print("ALT");
        const int centerY = 120;
        const int speedBase = (speedValue / 20) * 20;
        const int altitudeBase = (altitude / 200) * 200;
        for (int i = -3; i <= 3; ++i)
        {
            const int sv = speedBase + i * 20;
            const int sy = centerY + (int)((speedValue - sv) * 0.9f);
            if (sy > 46 && sy < 205) { M5.Display.drawFastHLine(1, sy, 8, hud); M5.Display.setCursor(4, sy - 7); M5.Display.printf("%d", sv); }
            const int av = altitudeBase + i * 200;
            const int ay = centerY + (altitude - av) * 18 / 200;
            if (ay > 46 && ay < 205) { M5.Display.drawFastHLine(311, ay, 8, hud); M5.Display.setCursor(289, ay - 7); M5.Display.printf("%d", av); }
        }
        M5.Display.drawRect(1, centerY - 8, 31, 17, hud);
        M5.Display.setCursor(4, centerY - 5); M5.Display.printf("%03d", speedValue);
        M5.Display.drawRect(288, centerY - 8, 32, 17, hud);
        M5.Display.setCursor(290, centerY - 5); M5.Display.printf("%04d", altitude);
        // Dedicated lower combat rail. Route/debug labels formerly shown here
        // (CT, DIVE ARC, GUIDE/FREE, GND/AIR-RDY) competed with actionable
        // information, so the rail now has one stable layout: hull, highest
        // priority alert/state, and current weapon.
        M5.Display.setTextColor(hud, BLACK);
        const uint16_t hullColor = s_playerHull <= 0
            ? (((now / 100) & 1) ? RED : ORANGE)
            : (s_playerHull == 1 ? RED : s_playerHull == 2 ? YELLOW : GREEN);
        M5.Display.setTextColor(hullColor, BLACK);
        M5.Display.setCursor(4, 217);
        M5.Display.printf("HULL %d/3", s_playerHull);
        // The old three 10x4-pixel marks were easy to miss during combat.
        // Keep the same three-hit model, but present it as one segmented,
        // high-contrast bar in the dedicated left side of the lower rail.
        constexpr int HULL_X = 4;
        constexpr int HULL_Y = 230;
        constexpr int HULL_W = 70;
        constexpr int HULL_H = 7;
        M5.Display.drawRect(HULL_X, HULL_Y, HULL_W, HULL_H, hullColor);
        M5.Display.fillRect(HULL_X + 2, HULL_Y + 2, HULL_W - 4,
                            HULL_H - 4, 0x2104);
        if (s_playerHull > 0)
        {
            const int filled = (HULL_W - 4) * s_playerHull / 3;
            M5.Display.fillRect(HULL_X + 2, HULL_Y + 2, filled,
                                HULL_H - 4, hullColor);
        }
        for (int i = 1; i < 3; ++i)
        {
            const int dividerX = HULL_X + 2 + (HULL_W - 4) * i / 3;
            M5.Display.drawFastVLine(dividerX, HULL_Y + 1,
                                     HULL_H - 2, BLACK);
        }

        const bool recentMissileHit = s_rearMissileHitStartMs != 0 &&
            now - s_rearMissileHitStartMs < 1100;
        const bool recentDamage = (int32_t)(now - s_collisionShakeUntilMs) < 0;
        const bool ramThreat = s_bossMovePhase == BossMovePhase::CHARGE_WINDUP ||
                               s_bossMovePhase == BossMovePhase::CHARGE;
        const char* combatStatus = "SYSTEM NOMINAL";
        uint16_t statusColor = hud;
        if (recentMissileHit)
        {
            combatStatus = "MISSILE HIT";
            statusColor = ((now / 100) & 1) ? WHITE : RED;
        }
        else if (missileAlert)
        {
            combatStatus = s_flareInterceptPending ? "FLARE TRACK" : "MISSILE ALERT";
            statusColor = ((now / 140) & 1) ? WHITE : RED;
        }
        else if (s_missileCutStartMs != 0 && now - s_missileCutStartMs < 900)
        {
            combatStatus = s_missileCutCount > 1 ? "MULTI CUT" : "MISSILE CUT";
            statusColor = GREEN;
        }
        else if (ramThreat)
        {
            combatStatus = s_bossMovePhase == BossMovePhase::CHARGE
                ? "RAM ATTACK" : "RAM WARNING";
            statusColor = ((now / 120) & 1) ? RED : ORANGE;
        }
        else if (recentDamage)
        {
            combatStatus = "ARMOR DAMAGE";
            statusColor = ORANGE;
        }
        else if ((int32_t)(s_playerHitConfirmUntilMs - now) > 0)
        {
            combatStatus = s_playerHitConfirmTarget == MISSION_TARGET
                ? "BOSS HIT" : "TARGET HIT";
            statusColor = ((now / 60) & 1) ? WHITE : GREEN;
        }
        else if (s_beamState == BeamState::READY)
        {
            combatStatus = "BEAM READY";
            statusColor = CYAN;
        }
        else if (s_beamState == BeamState::CHARGING)
        {
            combatStatus = "BEAM CHARGE";
            statusColor = ORANGE;
        }
        else if (s_beamState == BeamState::FIRING)
        {
            combatStatus = "BEAM FIRING";
            statusColor = CYAN;
        }
        M5.Display.setTextColor(statusColor, BLACK);
        M5.Display.setCursor(82, 220);
        M5.Display.print(combatStatus);
        if (s_beamState == BeamState::CHARGING || s_beamState == BeamState::READY)
        {
            const uint32_t elapsed = s_beamState == BeamState::READY
                ? BEAM_CHARGE_MS : min(BEAM_CHARGE_MS, now - s_beamStateSinceMs);
            const int chargeWidth = elapsed * 112 / BEAM_CHARGE_MS;
            M5.Display.drawRect(82, 231, 114, 5, statusColor);
            if (chargeWidth > 0)
                M5.Display.fillRect(83, 232, chargeWidth, 3, statusColor);
        }

        M5.Display.setTextColor(hud, BLACK);
        M5.Display.setCursor(210, 220);
        M5.Display.print("WPN");
        M5.Display.setCursor(236, 220);
        M5.Display.print(weaponName());
        M5.Display.setCursor(210, 231);
        if (s_lockedTarget >= 0)
            M5.Display.printf("RNG %02d", (int)s_lockedTargetRange);
        else
            M5.Display.print("NO LOCK");
    }

    core2_draw_battery_indicator();
    napetune_lcd_release();
    vTaskPrioritySet(nullptr, normalPriority);
}

void drawDemoExitGate()
{
    if (!s_storyMode || (s_demoStoryPhase != DemoStoryPhase::ESCAPE &&
        s_demoStoryPhase != DemoStoryPhase::FAIL_GRACE)) return;
    const float remaining = max(0.0f, s_demoExitTravel - s_travel);
    // Reuse the route-guide projection instead of scaling a screen-fixed box.
    // Eighteen travel units map to one scene-depth unit, so the gate follows
    // bends, pitch and camera bank while expanding naturally on approach.
    const float z = constrain(remaining / 18.0f, 0.85f, 43.0f);
    drawGate(z, 0);
    if (z < 24.0f)
    {
        float routeNowX, routeNowY, routeAheadX, routeAheadY;
        routePosition(s_travel, &routeNowX, &routeNowY);
        routePosition(s_travel + z, &routeAheadX, &routeAheadY);
        const float cx = routeAheadX - routeNowX;
        const float cy = routeAheadY - routeNowY;
        Point2 topLeft, topRight;
        const bool projected = project(cx - 4.2f, cy - 2.55f, z, &topLeft) &&
                               project(cx + 4.2f, cy - 2.55f, z, &topRight);
        const uint16_t c = ((millis() / 120) & 1) ? CYAN : 0x04D3;
        if (projected)
        {
            const int labelX = constrain((topLeft.x + topRight.x) / 2 - 27,
                                         2, W - 57);
            const int labelY = constrain(min(topLeft.y, topRight.y) - 11,
                                         2, H - 11);
            s_frame.fillRect(labelX - 2, labelY - 1, 58, 10, BLACK);
            s_frame.setTextColor(c, BLACK);
            s_frame.setCursor(labelX, labelY);
            s_frame.print("EXIT POINT");
        }
    }
}

void drawDemoUnlockGuide()
{
    napetune_lcd_acquire();
    M5.Display.fillScreen(BLACK);
    M5.Display.drawRect(5, 5, 310, 230, CYAN);
    M5.Display.setTextDatum(top_left);
    M5.Display.setTextSize(1);
    M5.Display.setFont(&fonts::lgfxJapanGothic_16);
    M5.Display.setTextColor(YELLOW, BLACK);
    M5.Display.drawString("IMPULSE FORM // UNLOCKED", 14, 10);
    M5.Display.drawFastHLine(14, 34, 292, CYAN);
    M5.Display.setFont(&fonts::lgfxJapanGothic_12);
    M5.Display.setTextColor(WHITE, BLACK);
    M5.Display.drawString("01ボタン：インパルスフォーム（高速飛行形態）", 14, 43);
    M5.Display.drawString("M2ボタンでロール回避", 14, 66);
    M5.Display.drawString("長押しでピッチロール", 14, 87);
    M5.Display.drawString("03ボタン：スロットル UP", 14, 108);
    M5.Display.drawString("04ボタン：スロットル DOWN", 14, 129);
    M5.Display.setFont(&fonts::lgfxJapanGothic_16);
    M5.Display.setTextColor(CYAN, BLACK);
    M5.Display.drawString("30秒以内にEXIT POINTへ到達せよ", 14, 166);
    M5.Display.setTextColor(YELLOW, BLACK);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString("M1/B : START", 160, 216);
    M5.Display.setTextDatum(top_left);
    M5.Display.setFont(&fonts::Font0);
    napetune_lcd_release();
}

void drawDemoCinematic(uint32_t now)
{
    const uint32_t age = now - s_demoPhaseStartedMs;
    const uint8_t page = age < 2400 ? 0 : age < 3600 ? 1 : age < 6100 ? 2
                       : age < 7600 ? 3 : age < 9000 ? 4 : 5;
    if (page != s_demoCinematicPage)
    {
        s_demoCinematicPage = page;
        static const char* const en[] = {
            "MISSILE ALERT", "IZU", "TERMINALIS", "IZU", "IZU", "IZU"
        };
        static const char* const ja[] = {
            "MULTIPLE TRACKS DETECTED", "やば……！",
            "IMPULSE FORM AVAILABLE / FRAME SHIFT AVAILABLE / PLASMA VECTOR SYNC STANDBY",
            "インパルスフォーム……？", "……TERMINALISは", "もっと……飛べる！！"
        };
        core2_glass2_paged_message(en[page], ja[page]);
    }
    napetune_lcd_acquire();
    M5.Display.fillScreen(BLACK);
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextSize(1);
    if (page == 0)
    {
        const uint16_t c = ((now / 105) & 1) ? WHITE : RED;
        M5.Display.drawRect(4, 4, 312, 232, c);
        M5.Display.setFont(&fonts::Font4);
        M5.Display.setTextColor(c, BLACK);
        M5.Display.drawString("MISSILE ALERT!!", 160, 83);
        M5.Display.setFont(&fonts::Font2);
        M5.Display.drawString("MULTIPLE TRACKS DETECTED", 160, 137);
    }
    else if (page == 1)
    {
        M5.Display.setFont(&fonts::lgfxJapanGothic_24);
        M5.Display.setTextColor(WHITE, BLACK);
        M5.Display.drawString("やば……！", 160, 120);
    }
    else if (page == 2)
    {
        M5.Display.setTextDatum(top_left);
        M5.Display.setFont(&fonts::Font2);
        M5.Display.setTextColor(CYAN, BLACK);
        M5.Display.drawString("IMPULSE FORM :", 18, 42);
        M5.Display.drawString("AVAILABLE", 42, 65);
        M5.Display.drawString("FRAME SHIFT :", 18, 94);
        M5.Display.drawString("AVAILABLE", 42, 117);
        M5.Display.drawString("PLASMA VECTOR SYNC : STANDBY", 18, 158);
    }
    else
    {
        M5.Display.setFont(&fonts::lgfxJapanGothic_24);
        M5.Display.setTextColor(page == 5 ? CYAN : WHITE, BLACK);
        const char* text = page == 3 ? "インパルスフォーム……？"
                         : page == 4 ? "……TERMINALISは"
                                     : "もっと……飛べる！！";
        if (M5.Display.textWidth(text) > 302)
            M5.Display.setFont(&fonts::lgfxJapanGothic_20);
        M5.Display.drawString(text, 160, 120);
    }
    M5.Display.setTextDatum(top_left);
    M5.Display.setFont(&fonts::Font0);
    napetune_lcd_release();
}

void drawDemoResult(bool success, uint32_t now)
{
    const uint32_t age = now - s_demoPhaseStartedMs;
    const uint16_t c = success ? GREEN : (((now / 100) & 1) ? RED : ORANGE);
    if (!success)
    {
        // Chain explosions remain attached to the actual craft position and
        // reuse the visual language of a rear-missile impact.
        const int sx = CX + (int)(s_shipX * 25.0f);
        const int sy = 153 + (int)(s_shipY * 25.0f);
        static const int8_t burstX[9] = {-22, 18, -7, 24, -19, 8, 0, 15, -12};
        static const int8_t burstY[9] = {-20, -16, -5, 5, 15, 20, -27, 29, 3};
        for (int i = 0; i < 9; ++i)
        {
            const int burstAge = (int)age - i * 145;
            if (burstAge < 0 || burstAge >= 680) continue;
            const int x = sx + burstX[i];
            const int y = sy + burstY[i];
            const int radius = 3 + burstAge / 42;
            const uint16_t hot = burstAge < 115 ? WHITE
                : burstAge < 320 ? YELLOW : ORANGE;
            s_frame.fillCircle(x, y, max(1, 5 - burstAge / 90), hot);
            s_frame.drawCircle(x, y, radius, burstAge < 360 ? ORANGE : RED);
            s_frame.drawLine(x - radius - 4, y, x - radius, y, hot);
            s_frame.drawLine(x + radius, y, x + radius + 4, y, hot);
            s_frame.drawLine(x, y - radius - 4, x, y - radius, WHITE);
        }
        if (age < 900 && ((now / 65) & 1))
            s_frame.drawRect(2, 2, W - 4, H - 4, age < 420 ? WHITE : RED);
    }
    const bool showLabel = success || age >= 1050;
    if (showLabel)
    {
        s_frame.fillRect(35, 76, W - 70, 38, BLACK);
        s_frame.drawRect(35, 76, W - 70, 38, c);
        s_frame.setTextDatum(middle_center);
        s_frame.setFont(&fonts::Font2);
        s_frame.setTextColor(c, BLACK);
        s_frame.drawString(success ? "MISSION COMPLETE" : "MISSION FAILED", CX, 95);
        if (age >= 800)
        {
            s_frame.setFont(&fonts::Font0);
            s_frame.setTextColor(YELLOW, BLACK);
            s_frame.drawString("M1/B : NEXT", CX, 123);
        }
        s_frame.setTextDatum(top_left);
        s_frame.setFont(&fonts::Font0);
    }
}

void drawDemoEndCard()
{
    M5.Display.setBrightness(128);
    napetune_lcd_acquire();
    M5.Display.fillScreen(BLACK);
    M5.Display.setTextDatum(middle_center);
    M5.Display.setFont(&fonts::Font4);
    M5.Display.setTextColor(CYAN, BLACK);
    M5.Display.drawString("THANK YOU FOR PLAYING", 160, 19);

    // Keep the QR at its native pixel size for reliable scanning. Placing the
    // copy beside it gives the Japanese guidance enough lines without
    // reducing either the code or the headline to an unreadable size.
    M5.Display.drawPng(demo_prologue_qr_start,
        (size_t)(demo_prologue_qr_end - demo_prologue_qr_start), 7, 45);
    M5.Display.setTextDatum(top_left);
    M5.Display.setFont(&fonts::lgfxJapanGothic_12);
    M5.Display.setTextColor(WHITE, BLACK);
    static const char* const guide[] = {
        "QRコードから",
        "プロローグ小説を",
        "お読みいただけます",
        "会場のKVカードも是非",
        "お持ち帰り下さい！",
        "SNSでも感想など",
        "頂けますと、",
        "とても嬉しいです！"
    };
    for (int i = 0; i < 8; ++i)
        M5.Display.drawString(guide[i], 146, 46 + i * 17);

    M5.Display.setTextDatum(middle_center);
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextColor(YELLOW, BLACK);
    M5.Display.drawString("M1/B : BACK TO MENU", 160, 222);
    M5.Display.setTextDatum(top_left);
    M5.Display.setFont(&fonts::Font0);
    napetune_lcd_release();
}

void drawDemoKvCard(uint32_t age)
{
    uint8_t brightness = s_showcaseMode ? 128 : 0;
    if (!s_showcaseMode && age < 2200)
        brightness = (uint8_t)min(128u, age * 128u / 2200u);
    else if (s_showcaseMode || age < 4200) brightness = 128;
    else if (!s_showcaseMode && age < 6400)
        brightness = (uint8_t)(128u - min(128u,
            (age - 4200u) * 128u / 2200u));

    if (s_demoKvLastFadeStep == 255)
    {
        M5.Display.setBrightness(s_showcaseMode ? 128 : 0);
        napetune_lcd_acquire();
        M5.Display.fillScreen(BLACK);
        M5.Display.drawPng(demo_terminalis_kv_start,
            (size_t)(demo_terminalis_kv_end - demo_terminalis_kv_start), 0, 0);
        napetune_lcd_release();
    }
    if (brightness != s_demoKvLastFadeStep)
    {
        s_demoKvLastFadeStep = brightness;
        M5.Display.setBrightness(brightness);
    }
}

void putLe16(uint8_t* p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

void putLe32(uint8_t* p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

bool saveCanvasBmp(const char* path)
{
    uint16_t* pixels = (uint16_t*)s_frame.getBuffer();
    if (!pixels) return false;

    constexpr uint32_t rowBytes = W * 3;
    constexpr uint32_t pixelBytes = rowBytes * H;
    uint8_t header[54] = {};
    header[0] = 'B'; header[1] = 'M';
    putLe32(header + 2, 54 + pixelBytes);
    putLe32(header + 10, 54);
    putLe32(header + 14, 40);
    putLe32(header + 18, W);
    putLe32(header + 22, H);
    putLe16(header + 26, 1);
    putLe16(header + 28, 24);
    putLe32(header + 34, pixelBytes);

    napetune_spi_acquire();
    SD.remove(path);
    File file = SD.open(path, FILE_WRITE);
    if (!file)
    {
        napetune_spi_release();
        return false;
    }
    file.write(header, sizeof(header));
    uint8_t row[rowBytes];
    for (int y = H - 1; y >= 0; --y)
    {
        for (int x = 0; x < W; ++x)
        {
            const uint16_t c = pixels[y * W + x];
            row[x * 3 + 0] = (uint8_t)((c & 0x1f) * 255 / 31);
            row[x * 3 + 1] = (uint8_t)(((c >> 5) & 0x3f) * 255 / 63);
            row[x * 3 + 2] = (uint8_t)(((c >> 11) & 0x1f) * 255 / 31);
        }
        file.write(row, sizeof(row));
    }
    file.close();
    napetune_spi_release();
    return true;
}

bool captureModelForm(bool flight)
{
    const float oldTransform = s_transformBlend;
    const float oldLean = s_forwardLean;
    const float oldBoostVisual = s_boostVisual;
    const float oldShipX = s_shipX, oldShipY = s_shipY;
    const float oldBoostKick = s_boostKick;
    const float oldPoseYaw = s_poseYaw, oldPosePitch = s_posePitch;
    const float oldCameraBank = s_cameraBank, oldCameraPitch = s_cameraPitch;
    const float oldActionCameraRoll = s_actionCameraRoll;
    const float oldBarrelSin = s_barrelSin, oldBarrelCos = s_barrelCos;
    const bool oldRoll = s_rollActive;

    s_transformBlend = flight ? 1.0f : 0.0f;
    s_forwardLean = flight ? 1.0f : 0.18f;
    s_boostVisual = flight ? 1.0f : 0.0f;
    s_shipX = 0.0f; s_shipY = 0.0f; s_boostKick = 0.0f;
    s_poseYaw = 0.0f; s_posePitch = 0.0f;
    s_cameraBank = 0.0f; s_cameraPitch = 0.0f; s_actionCameraRoll = 0.0f;
    s_barrelSin = 0.0f; s_barrelCos = 1.0f; s_rollActive = false;
    s_modelCaptureClean = true;
    s_frame.fillScreen(BLACK);
    drawShipAndHud(flight, flight ? 24.0f : 12.0f);
    s_modelCaptureClean = false;
    const bool saved = saveCanvasBmp(flight ? "/MECHA_FLIGHT.bmp" : "/MECHA_NORMAL.bmp");

    s_transformBlend = oldTransform; s_forwardLean = oldLean;
    s_boostVisual = oldBoostVisual; s_shipX = oldShipX; s_shipY = oldShipY;
    s_boostKick = oldBoostKick; s_poseYaw = oldPoseYaw; s_posePitch = oldPosePitch;
    s_cameraBank = oldCameraBank; s_cameraPitch = oldCameraPitch;
    s_actionCameraRoll = oldActionCameraRoll;
    s_barrelSin = oldBarrelSin; s_barrelCos = oldBarrelCos; s_rollActive = oldRoll;
    return saved;
}

void serviceModelCaptureRequest()
{
    const uint8_t mode = s_modelCaptureRequest;
    if (mode == 0) return;
    s_modelCaptureRequest = 0;
    const bool normalOk = mode == 2 ? true : captureModelForm(false);
    const bool flightOk = mode == 1 ? true : captureModelForm(true);
    Serial.printf("[MECHA CAPTURE] normal=%s flight=%s\n",
                  mode == 2 ? "SKIP" : (normalOk ? "OK" : "FAIL"),
                  mode == 1 ? "SKIP" : (flightOk ? "OK" : "FAIL"));
}
}

void mecha_game_request_model_capture(uint8_t mode)
{
    if (mode >= 1 && mode <= 3) s_modelCaptureRequest = mode;
}

bool mecha_game_get_hybrid_render()
{
    return s_hybridRenderEnabled;
}

void mecha_game_set_hybrid_render(bool enabled)
{
    s_hybridRenderEnabled = enabled;
    s_externalHudDirty = true;
}

void mecha_game_draw()
{
    if (!s_frameReady) return;
    const uint32_t frameStart = micros();
    const bool boost = s_boostActive;
    const float boostPercent = game_input_get_tuning(8) / 100.0f;
    float speed = (boost ? 24.0f * boostPercent * (s_loadout.thruster ? 1.08f : 1.0f) : 12.0f)
                * (s_throttlePercent / 100.0f) + s_rollThrust;
    if (s_beamState == BeamState::FIRING) speed *= 0.62f;
    const uint32_t drawStateNow = millis();
    const bool bladeTargetEngaged = s_weapon == 4 && s_bladeDashTargetX >= 0 &&
        ((s_bladeComboStep != 0 &&
          drawStateNow - s_bladeLastReleaseMs <= BLADE_COMBO_RESET_MS) ||
         s_bladeSwingStartMs != 0);
    if (bladeTargetEngaged) speed *= BLADE_COMBO_SPEED_SCALE;

    // The USB task shares Core 1. Give only the SRAM line-rendering slice a
    // temporary priority advantage so a frame cannot be fragmented by
    // hundreds of small USB/Serial scheduling intervals.
    const UBaseType_t normalPriority = uxTaskPriorityGet(nullptr);
    // MAX3421 interrupt service runs at priority 5. The previous +1 raised
    // Arduino loop only from 1 to 2, so a nominal 6 ms SRAM render could be
    // fragmented into 150-400 ms of wall time. Priority 6 protects only this
    // CPU/local-SRAM slice; it is restored before the SPI/LCD transfer.
    const UBaseType_t renderPriority = normalPriority < 6 ? 6 : normalPriority;
    vTaskPrioritySet(nullptr, renderPriority);
    s_frame.startWrite();
    drawFlightSpace(boost);
    drawDemoExitGate();
    const uint32_t sceneDone = micros();
    drawShipAndHud(boost, speed);
    if ((s_storyMode && s_demoStoryPhase == DemoStoryPhase::SUCCESS) ||
        ((s_storyMode || s_demoMode) &&
         s_demoStoryPhase == DemoStoryPhase::FAILED))
        drawDemoResult(s_demoStoryPhase == DemoStoryPhase::SUCCESS, millis());
    s_frame.endWrite();
    vTaskPrioritySet(nullptr, normalPriority);
    const uint32_t renderDone = micros();

    const uint32_t now = millis();
    const uint32_t drawStart = micros();
    const uint32_t transferred = pushFullFrame();
    const uint32_t frameDone = micros();
    s_perfRenderUs += renderDone - frameStart;
    s_perfSceneUs += sceneDone - frameStart;
    s_perfShipUs += renderDone - sceneDone;
    s_perfDrawUs += frameDone - drawStart;
    s_perfTotalUs += frameDone - frameStart;
    if (renderDone - frameStart > s_perfMaxRenderUs)
        s_perfMaxRenderUs = renderDone - frameStart;
    if (frameDone - drawStart > s_perfMaxLcdUs)
        s_perfMaxLcdUs = frameDone - drawStart;
    if (frameDone - frameStart > s_perfMaxTotalUs)
        s_perfMaxTotalUs = frameDone - frameStart;
    ++s_perfFrames;
    s_perfPixels += transferred;
    if (now - s_perfSinceMs >= 5000)
    {
        const uint32_t elapsed = now - s_perfSinceMs;
        const uint32_t fps10 = elapsed ? s_perfFrames * 10000u / elapsed : 0;
        Serial.printf("[MECHA ATOMIC PERF] fps=%u.%u scene=%uus ship=%uus render=%uus lcd=%uus total=%uus max=%uus maxR=%uus maxL=%uus pixels=%u\n",
                      (unsigned)(fps10 / 10), (unsigned)(fps10 % 10),
                      (unsigned)(s_perfFrames ? s_perfSceneUs / s_perfFrames : 0),
                      (unsigned)(s_perfFrames ? s_perfShipUs / s_perfFrames : 0),
                      (unsigned)(s_perfFrames ? s_perfRenderUs / s_perfFrames : 0),
                      (unsigned)(s_perfFrames ? s_perfDrawUs / s_perfFrames : 0),
                      (unsigned)(s_perfFrames ? s_perfTotalUs / s_perfFrames : 0),
                      (unsigned)s_perfMaxTotalUs,
                      (unsigned)s_perfMaxRenderUs,
                      (unsigned)s_perfMaxLcdUs,
                      (unsigned)(s_perfFrames ? s_perfPixels / s_perfFrames : 0));
        s_perfSinceMs = now; s_perfFrames = 0; s_perfRenderUs = 0;
        s_perfDrawUs = 0; s_perfTotalUs = 0; s_perfSceneUs = 0; s_perfShipUs = 0;
        s_perfMaxTotalUs = 0;
        s_perfMaxRenderUs = 0; s_perfMaxLcdUs = 0; s_perfPixels = 0;
    }
}

static uint16_t storyCheckpointForTravel(float travel)
{
    const float courseDistance = fmodf(travel, 240.0f);
    uint16_t checkpoint = 0;
    for (uint16_t i = 1; i < STORY_CHECKPOINT_COUNT; ++i)
    {
        if (courseDistance < STORY_CHECKPOINTS[i]) break;
        checkpoint = i;
    }
    return checkpoint;
}

static void drawExitConfirmation()
{
    napetune_lcd_acquire();
    M5.Display.fillScreen(BLACK);
    M5.Display.drawRect(10, 10, 300, 220, CYAN);
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextSize(1);
    M5.Display.setFont(&fonts::lgfxJapanGothic_16);
    M5.Display.setTextColor(WHITE, BLACK);
    M5.Display.drawString("メニューへ戻りますか？", 160, 48);
    M5.Display.setFont(&fonts::lgfxJapanGothic_12);
    M5.Display.setTextColor(CYAN, BLACK);
    M5.Display.drawString(
        s_storyMode ? "STORYは次回先頭から開始します" :
        "DEMOは進行を保存しません",
        160, 86
    );

    M5.Display.fillRect(45, 145, 95, 50, 0x2104);
    M5.Display.drawRect(45, 145, 95, 50, WHITE);
    M5.Display.setFont(&fonts::lgfxJapanGothic_16);
    M5.Display.setTextColor(WHITE, 0x2104);
    M5.Display.drawString("YES", 92, 170);

    // NO is the visual/default choice. A deliberate tap is still required.
    M5.Display.fillRect(180, 145, 95, 50, BLUE);
    M5.Display.drawRect(178, 143, 99, 54, YELLOW);
    M5.Display.setTextColor(YELLOW, BLUE);
    M5.Display.drawString("NO", 227, 170);
    M5.Display.setTextDatum(top_left);
    M5.Display.setFont(&fonts::Font0);
    napetune_lcd_release();
}

static void dismissExitConfirmation()
{
    s_exitConfirmVisible = false;
    s_exitConfirmArmed = false;
    s_exitConfirmTouchWasDown = false;
    s_exitHoldTracking = false;
    s_exitHoldStartedMs = 0;
    core2_glass2_set_demo_playing(1);
    napetune_lcd_acquire();
    M5.Display.fillScreen(BLACK);
    napetune_lcd_release();
    s_lastDrawMs = 0;
    s_externalHudDirty = true;
    s_lastUpdateMs = millis();
}

static bool serviceExitConfirmation()
{
    bool touchDown = false;
    int touchX = -1;
    int touchY = -1;
    if (M5.Touch.getCount() > 0)
    {
        auto touch = M5.Touch.getDetail(0);
        touchDown = touch.isPressed();
        touchX = touch.x;
        touchY = touch.y;
    }

    if (s_exitConfirmVisible)
    {
        if (!touchDown)
        {
            s_exitConfirmArmed = true;
        }
        else if (s_exitConfirmArmed && !s_exitConfirmTouchWasDown)
        {
            if (touchY >= 145 && touchY < 195 && touchX >= 45 && touchX < 140)
            {
                if (!s_demoMode && !s_storyMode)
                {
                    const uint16_t checkpoint = storyCheckpointForTravel(s_travel);
                    napetune_save_set_story_position(
                        STORY_SAVE_PACK, STORY_SAVE_CHAPTER, checkpoint
                    );
                    Serial.printf("[MECHA SAVE] checkpoint=%u travel=%.2f\n",
                                  (unsigned)checkpoint, s_travel);
                }
                core2_glass2_set_demo_playing(0);
                return true;
            }
            if (touchY >= 145 && touchY < 195 && touchX >= 180 && touchX < 275)
                dismissExitConfirmation();
        }
        s_exitConfirmTouchWasDown = touchDown;
        s_lastUpdateMs = millis();
        return false;
    }

    const bool exitHotspotDown = touchDown &&
        touchX >= 0 && touchX <= EXIT_HOTSPOT_MAX_X &&
        touchY >= 0 && touchY <= EXIT_HOTSPOT_MAX_Y;
    if (exitHotspotDown)
    {
        if (!s_exitHoldTracking)
        {
            s_exitHoldTracking = true;
            s_exitHoldStartedMs = millis();
        }
        else if (millis() - s_exitHoldStartedMs >= EXIT_HOLD_MS)
        {
            s_exitConfirmVisible = true;
            s_exitConfirmArmed = false;
            s_exitConfirmTouchWasDown = true;
            core2_glass2_set_demo_playing(0);
            drawExitConfirmation();
            Serial.println("[MECHA] MENU RETURN CONFIRM");
        }
    }
    else
    {
        s_exitHoldTracking = false;
        s_exitHoldStartedMs = 0;
    }
    return false;
}

void mecha_game_begin(bool demoMode)
{
    game_input_clear_nape();
    s_showcaseMode = false;
    s_showcaseTouchTracking = false;
    s_demoMode = demoMode;
    s_storyMode = !demoMode;
    s_loadout = game_loadout_load();
    s_cyanThrusterEquipped = s_loadout.thruster != 0;
    Serial.printf("[MECHA LOADOUT] mode=%s ARM=%u LEG=%u THR=%u W=%u/%u\n",
                  s_demoMode ? "DEMO" : "STORY",
                  s_loadout.arms, s_loadout.legs, s_loadout.thruster,
                  s_loadout.weapon1, s_loadout.weapon2);
    initializeSinLut();
    s_frame.setColorDepth(16); s_frame.setPsram(false);
    s_frameReady = s_frame.createSprite(W, H) != nullptr;
    s_hybridNormalCanvas.setColorDepth(16); s_hybridNormalCanvas.setPsram(false);
    s_hybridFlightCanvas.setColorDepth(16); s_hybridFlightCanvas.setPsram(false);
    const bool normalCanvasReady =
        s_hybridNormalCanvas.createSprite(MECHA_HYBRID_BODY_W,
                                          MECHA_HYBRID_BODY_H) != nullptr;
    const bool flightCanvasReady =
        s_hybridFlightCanvas.createSprite(MECHA_HYBRID_FLIGHT_W,
                                          MECHA_HYBRID_FLIGHT_H) != nullptr;
    s_hybridCanvasesReady = normalCanvasReady && flightCanvasReady;
    if (normalCanvasReady)
    {
        s_hybridNormalCanvas.setSwapBytes(true);
        s_hybridNormalCanvas.pushImage(0, 0, MECHA_HYBRID_BODY_W,
            MECHA_HYBRID_BODY_H, MECHA_HYBRID_BODY);
        s_hybridNormalCanvas.setSwapBytes(false);
        s_hybridNormalCanvas.setPivot(MECHA_HYBRID_BODY_W / 2, 40);
    }
    if (flightCanvasReady)
    {
        s_hybridFlightCanvas.setSwapBytes(true);
        s_hybridFlightCanvas.pushImage(0, 0, MECHA_HYBRID_FLIGHT_W,
            MECHA_HYBRID_FLIGHT_H, MECHA_HYBRID_FLIGHT);
        s_hybridFlightCanvas.setSwapBytes(false);
        s_hybridFlightCanvas.setPivot(MECHA_HYBRID_FLIGHT_W / 2, 35);
    }
    initializeBossSprite();
    napetune_lcd_acquire();
    M5.Display.fillScreen(BLACK);
    napetune_lcd_release();
    s_shipX = 0; s_shipY = 0; s_shipVx = 0; s_shipVy = 0; s_travel = 0;
    if (!s_storyMode && !s_demoMode && napetune_save_available())
    {
        const NapetuneSaveData& save = napetune_save_data();
        if (strncmp(save.content_pack, STORY_SAVE_PACK,
                    sizeof(save.content_pack)) == 0 &&
            save.chapter_id == STORY_SAVE_CHAPTER &&
            save.scene_id < STORY_CHECKPOINT_COUNT)
        {
            s_travel = STORY_CHECKPOINTS[save.scene_id];
            Serial.printf("[MECHA LOAD] checkpoint=%u travel=%.2f\n",
                          (unsigned)save.scene_id, s_travel);
        }
    }
    s_cameraX = 0; s_cameraY = 0; s_cameraBank = 0; s_cameraPitch = 0;
    s_actionCameraRoll = 0;
    s_routeBank = 0; s_routePitch = 0; s_routeLeadX = 0; s_routeLeadY = 0;
    s_courseBank = 0; s_coursePitch = 0; s_courseLoopSin = 0; s_courseLoopCos = 1;
    s_courseHeadingOffset = 0; s_courseSection = "STRAIGHT";
    s_fovScale = 1; s_boostKick = 0; s_boostVisual = 0;
    s_barrelSin = 0; s_barrelCos = 1; s_rollAngleDeg = 0; s_rollThrust = 0;
    s_rollStartMs = 0; s_rollCooldownUntilMs = 0; s_rollDirection = 1; s_rollActive = false;
    s_transformBlend = 0.0f;
    s_poseYaw = 0.0f; s_posePitch = 0.0f;
    s_forwardLean = 0.0f;
    s_useHoldPending = false; s_manualLoopActive = false;
    s_useHoldStartMs = 0; s_manualLoopStartMs = 0; s_manualLoopCooldownUntilMs = 0;
    s_loopBaseShipY = 0.0f; s_loopBaseAltitudeM = 1200.0f;
    s_loopDirection = 1;
    s_weapon = 0;
    s_weaponEffectStartMs = 0; s_lastWeaponShotMs = 0;
    s_missileAmmo = 8; s_beamAmmo = 3;
    s_beamState = BeamState::IDLE; s_beamStateSinceMs = 0; s_lastBeamDamageMs = 0;
    s_beamMessageUntilMs = 0;
    s_playerHitConfirmUntilMs = 0;
    s_playerHitConfirmX = CX; s_playerHitConfirmY = HORIZON_Y;
    s_playerHitConfirmDamage = 0; s_playerHitConfirmTarget = -1;
    s_bladeCharging = false; s_bladeDualMode = false;
    s_bladeSwingCharged = false; s_bladeSwingDual = false;
    s_bladeChargeStartMs = 0; s_bladeDualUntilMs = 0; s_bladeSwingStartMs = 0;
    s_missionTargetHp = BOSS_TARGET_HP; s_missionComplete = false;
    for (int i = 0; i < TARGET_COUNT; ++i)
    {
        s_targetHp[i] = i == MISSION_TARGET ? BOSS_TARGET_HP : NORMAL_TARGET_HP;
        s_targetDestroyedUntilMs[i] = 0;
        s_targetScreenRange[i] = 999.0f;
        s_enemyNextFireMs[i] = millis() + 2600 + i * 410;
    }
    for (EnemyShot& shot : s_enemyShots) shot = {};
    s_nextEnemyShot = 0;
    s_enemyQuietUntilMs = 0;
    s_enemyVolleyShots = 0;
    s_enemyExplosionTarget = -1; s_enemyExplosionStartMs = 0;
    s_missileDamageApplied = false; s_missileTarget = -1;
    s_missileFireLatched = false;
    for (MissileBlast& blast : s_missileBlasts) blast = {};
    s_nextMissileBlast = 0;
    s_multiLockCharging = false; s_multiLockStartMs = 0; s_multiLockCount = 0;
    for (int i = 0; i < TARGET_COUNT; ++i) s_multiLocked[i] = false;
    for (MultiMissileShot& shot : s_multiShots) shot = {};
    s_nextMultiShot = 0;
    s_autoPilotFireDown = false;
    s_autoPilotUseDown = false;
    s_autoPilotWasDriving = false;
    s_autoPilotRollDirection = 1;
    s_autoPilotManualUntilMs = 0;
    s_autoPilotLastWeaponMs = millis();
    s_autoPilotDemoStartMs = millis();
    s_autoPilotThrottleStage = 255;
    s_weaponBannerUntilMs = 0;
    randomSeed((uint32_t)esp_random() ^ micros());
    s_interceptorActive = false; s_interceptorWarned = false;
    s_interceptorX = 0; s_interceptorY = 0; s_interceptorZ = 34.0f;
    s_interceptorPattern = InterceptorPattern::DIRECT;
    s_interceptorClosure = 10.0f;
    s_nextInterceptorMs = millis() + 3000;
    s_rearMissileActive = false;
    s_rearMissileHitStartMs = 0;
    s_nextRearMissileMs = millis() + 12000;
    s_rearMissileElapsedMs = 0;
    s_rearMissileEscapeMs = 0;
    s_flareCooldownUntilMs = 0;
    s_flareVisualUntilMs = 0;
    s_flareLungeStartMs = 0;
    s_flareInterceptUntilMs = 0;
    s_flareInterceptAtMs = 0;
    s_flareInterceptPending = false;
    s_flareHomingBurst = false;
    s_flareOriginX = CX;
    s_flareOriginY = 151;
    s_flareTargetX = CX;
    s_flareTargetY = 170;
    s_flareDetonationX = CX;
    s_flareDetonationY = 151;
    s_rearMissileHudDrawn = false;
    s_enemyMissileHudDrawn = false;
    s_bossBattleActive = false;
    s_bossMovePhase = BossMovePhase::APPROACH;
    s_bossPhaseStartMs = millis();
    s_bossEncounterStarted = false;
    s_bossExitSide = 1;
    s_bossLateral = 0.0f;
    s_bossAltitudeM = 1650.0f;
    s_bossRange = 44.0f;
    s_bossVerticalOffset = 0.0f;
    s_bossCollisionCooldownUntilMs = 0;
    s_bossNextChargeMs = millis() + random(2600, 8200);
    s_bossChargeLineX = 0.0f;
    s_bossChargeLineAltitude = s_bossAltitudeM;
    s_collisionShakeUntilMs = 0; s_collisionSlowUntilMs = 0;
    s_missileCutStartMs = 0; s_missileCutX = CX; s_missileCutY = HORIZON_Y;
    s_missileCutCount = 0;
    s_terrainCollisionCooldownUntilMs = 0;
    s_routeEnemyCollisionCooldownUntilMs = 0; s_playerHull = 3;
    s_lastHitRejectLogMs = 0;
    s_routeGuideEnabled = false;
    s_tunnelRunActive = false;
    s_flightHudEnabled = true; s_hudSpeed = 160; s_hudAltitude = 7800;
    s_throttlePercent = 100.0f;
    s_effectiveThrottlePercent = 100.0f;
    s_altitudeM = 1200.0f; s_hudAltitude = s_altitudeM; s_hudSpeed = 120.0f;
    s_currentSpeed = 16; s_lastExternalHudMs = 0; s_externalHudDirty = true;
    s_freeAimX = CX; s_freeAimY = HORIZON_Y;
    s_altitudeControlAuthority = 0.0f;
    s_verticalInputEnvelope = 0.0f;
    s_lockedTarget = -1; s_lockedTargetX = CX; s_lockedTargetY = HORIZON_Y;
    s_lockedTargetGround = false;
    s_lockedTargetRange = 0.0f;
    s_lockedTargetHitEligible = false;
    s_lockedAimErrorPx = 999;
    s_lockCandidate = -1; s_lockCandidateX = CX; s_lockCandidateY = HORIZON_Y;
    s_lockCandidateGround = false;
    s_lockCandidateRange = 0.0f;
    s_cursorSwitchCandidate = -1;
    s_cursorSwitchSinceMs = 0;
    s_starCombatDim = 0.0f;
    for (int i = 0; i < STAR_COUNT; ++i) respawnStar(i);
    s_previousBoost = false;
    s_configWasActive = false;
    s_boostActive = false;
    s_fireUntilMs = 0;
    s_lastUpdateMs = millis(); s_lastDrawMs = 0;
    s_gameStartedMs = s_lastUpdateMs;
    // STORY owns the exhibition narrative. DEMO enters the flight loop
    // immediately with no launch dialogue or control-guide interruption.
    s_introCommsTriggered = s_demoMode;
    s_demoGuideVisible = false;
    s_demoGuideDismissed = s_demoMode;
    s_demoStoryPhase = DemoStoryPhase::COMBAT;
    core2_glass2_set_demo_impulse_unlocked(s_demoMode ? 1 : 0);
    s_demoKills = 0;
    s_demoPhaseStartedMs = 0;
    s_demoCinematicPage = 255;
    s_demoLastCountdown = 255;
    s_demoExitTravel = 0.0f;
    s_demoRevealFlareLaunched = false;
    s_demoRevealMissileAlerted = false;
    s_demoRevealTransformStartMs = 0;
    s_demoKvLastFadeStep = 255;
    s_exitHoldTracking = false;
    s_exitHoldStartedMs = 0;
    s_exitConfirmVisible = false;
    s_exitConfirmArmed = false;
    s_exitConfirmTouchWasDown = false;
    core2_glass2_set_demo_playing(s_demoMode ? 1 : 0);
    s_perfSinceMs = millis(); s_perfFrames = 0; s_perfRenderUs = 0;
    s_perfDrawUs = 0; s_perfTotalUs = 0; s_perfSceneUs = 0; s_perfShipUs = 0;
    s_perfMaxTotalUs = 0;
    s_perfMaxRenderUs = 0; s_perfMaxLcdUs = 0; s_perfPixels = 0;
    mecha_game_draw();
    drawExternalHud(true);
    s_lastDrawMs = millis();
    Serial.printf("[MECHA] high-speed rail flight begin framebuffer=%s\n", s_frameReady ? "OK" : "FAIL");
    Serial.println("[MECHA DISPLAY] mode=SRAM_VIEWPORT_256x192 SAFE_LARGE");
}

void mecha_game_begin_showcase()
{
    // Reuse the exact exhibition assets and fade timing from STORY without
    // entering combat or its opening communications.
    mecha_game_begin(false);
    s_showcaseMode = true;
    s_showcaseTouchTracking = false;
    s_introCommsTriggered = true;
    s_demoGuideVisible = false;
    s_demoGuideDismissed = true;
    s_demoStoryPhase = DemoStoryPhase::KV_CARD;
    s_demoPhaseStartedMs = millis();
    s_demoKvLastFadeStep = 255;
    core2_glass2_set_demo_playing(0);
    core2_glass2_context("TERMINALIS", "KEY VISUAL", "星間のTERMINALIS");
    drawDemoKvCard(2200);
    Serial.println("[MECHA SHOWCASE] KV -> QR");
}

static int8_t pollShowcaseSwipe()
{
    bool touchDown = false;
    int x = s_showcaseTouchLastX;
    int y = s_showcaseTouchLastY;
    if (M5.Touch.getCount() > 0)
    {
        auto touch = M5.Touch.getDetail(0);
        touchDown = touch.isPressed();
        x = touch.x;
        y = touch.y;
    }
    if (touchDown)
    {
        if (!s_showcaseTouchTracking)
        {
            s_showcaseTouchTracking = true;
            s_showcaseTouchStartX = x;
            s_showcaseTouchStartY = y;
        }
        s_showcaseTouchLastX = x;
        s_showcaseTouchLastY = y;
        return 0;
    }
    if (!s_showcaseTouchTracking) return 0;
    s_showcaseTouchTracking = false;
    const int dx = s_showcaseTouchLastX - s_showcaseTouchStartX;
    const int dy = s_showcaseTouchLastY - s_showcaseTouchStartY;
    if (abs(dx) < 55 || abs(dx) <= abs(dy)) return 0;
    return dx > 0 ? 1 : -1; // +1: next/right swipe, -1: back/left swipe
}

bool mecha_game_update()
{
    static uint32_t loopPerfSinceUs = 0;
    static uint32_t loopLastExitUs = 0;
    static uint64_t loopGapSumUs = 0, loopPollSumUs = 0;
    static uint64_t loopBodySumUs = 0, loopHudSumUs = 0;
    static uint32_t loopGapMaxUs = 0, loopPollMaxUs = 0;
    static uint32_t loopBodyMaxUs = 0, loopHudMaxUs = 0;
    static uint32_t loopSamples = 0;
    const uint32_t loopEnterUs = micros();
    if (loopPerfSinceUs == 0) loopPerfSinceUs = loopEnterUs;
    if (loopLastExitUs != 0)
    {
        const uint32_t gapUs = loopEnterUs - loopLastExitUs;
        loopGapSumUs += gapUs;
        if (gapUs > loopGapMaxUs) loopGapMaxUs = gapUs;
    }
    if (!s_frameReady)
    {
        Serial.println("[MECHA] framebuffer unavailable; returning to launcher");
        core2_glass2_set_demo_playing(0);
        return true;
    }
    serviceModelCaptureRequest();
    GameInputState input = {};
    const uint32_t pollStartUs = micros();
    game_input_poll(&input);
    const uint32_t pollUs = micros() - pollStartUs;
    loopPollSumUs += pollUs;
    if (pollUs > loopPollMaxUs) loopPollMaxUs = pollUs;
    if (core2_config_is_active())
    {
        s_configWasActive = true;
        core2_config_render();
        s_lastUpdateMs = millis();
        return false;
    }
    if (s_storyMode && !s_introCommsTriggered && core2_glass2_ready())
    {
        s_introCommsTriggered = true;
        core2_glass2_pilot_launch_message();
        Serial.println("[MECHA COMMS] DEMO INTRO");
    }
    static bool commPauseWasActive = false;
    static int commPauseIzuShown = -1;
    if (core2_glass2_dialogue_active())
    {
        const int izuSpeaking = core2_glass2_current_speaker_is_izu();
        if (!commPauseWasActive || commPauseIzuShown != izuSpeaking)
        {
            commPauseWasActive = true;
            commPauseIzuShown = izuSpeaking;
            // Every communication page keeps the paused flight frame visible.
            // Repainting on a speaker change also clears the previous Iz
            // portrait without ever replacing the game with a black screen.
            mecha_game_draw();
            s_lastDrawMs = millis();
            drawExternalHud();
            napetune_lcd_acquire();
            M5.Display.drawRect(3, 3, 314, 234, CYAN);
            M5.Display.setTextSize(1);
            M5.Display.setTextDatum(middle_center);
            if (izuSpeaking)
            {
                M5.Display.fillRect(82, 5, 156, 27, BLACK);
                M5.Display.drawRect(82, 5, 156, 27, CYAN);
                M5.Display.setFont(&fonts::lgfxJapanGothic_16);
                M5.Display.setTextColor(CYAN, BLACK);
                M5.Display.drawString("イズ / COMMS", 160, 18);
                M5.Display.drawPng(iz_comms_face_start,
                    (size_t)(iz_comms_face_end - iz_comms_face_start), 84, 34);
            }
            else
            {
                M5.Display.fillRect(66, 140, 188, 56, BLACK);
                M5.Display.drawRect(66, 140, 188, 56, CYAN);
                M5.Display.setFont(&fonts::lgfxJapanGothic_16);
                M5.Display.setTextColor(CYAN, BLACK);
                M5.Display.drawString("重要通信", 160, 156);
                M5.Display.drawString("GLASS2を確認", 160, 179);
            }
            M5.Display.fillRect(110, 202, 90, 29, BLACK);
            M5.Display.drawRect(110, 202, 90, 29, CYAN);
            M5.Display.setTextColor(WHITE, BLACK);
            M5.Display.drawString("NEXT [B]", 155, 216);
            M5.Display.setTextDatum(top_left);
            M5.Display.setFont(&fonts::Font0);
            napetune_lcd_release();
            Serial.println("[MECHA COMMS] AUTO PAUSE");
        }
        s_lastUpdateMs = millis();
        return false;
    }
    if (s_introCommsTriggered && core2_glass2_intro_complete() &&
        !s_demoGuideDismissed)
    {
        if (!s_demoGuideVisible)
        {
            s_demoGuideVisible = true;
            napetune_lcd_acquire();
            M5.Display.fillScreen(BLACK);
            M5.Display.drawRect(5, 5, 310, 230, CYAN);
            M5.Display.setTextDatum(top_left);
            M5.Display.setTextSize(1);
            M5.Display.setFont(&fonts::lgfxJapanGothic_16);
            M5.Display.setTextColor(CYAN, BLACK);
            M5.Display.drawString("操作ガイド", 14, 14);
            M5.Display.drawFastHLine(14, 39, 292, CYAN);
            static const char* const demoGuide[] = {
                "40機を撃破せよ",
                "M1ボタンで攻撃",
                "長押しで溜め攻撃",
                "M2ボタンでロール回避",
                "トラックボールで照準・高度変更",
                "ホイールで武器変更"
            };
            static const char* const storyGuide[] = {
                "M1ボタンで攻撃",
                "M2ボタンでロール回避",
                "長押しでピッチロール",
                "01ボタンでインパルスフォームへ変形",
                "トラックボールで照準・高度変更",
                "ホイールで武器変更"
            };
            const char* const* guide = s_storyMode ? demoGuide : storyGuide;
            const int guideCount = 6;
            const int guideStep = 25;
            M5.Display.setTextColor(WHITE, BLACK);
            for (int i = 0; i < guideCount; ++i)
            {
                M5.Display.setFont(&fonts::lgfxJapanGothic_16);
                if (M5.Display.textWidth(guide[i]) > 292)
                    M5.Display.setFont(&fonts::lgfxJapanGothic_12);
                M5.Display.setTextColor(
                    s_storyMode && i == 0 ? YELLOW : WHITE, BLACK);
                M5.Display.drawString(guide[i], 14, 47 + i * guideStep);
            }
            M5.Display.drawFastHLine(14, 193, 292, CYAN);
            M5.Display.setFont(&fonts::lgfxJapanGothic_16);
            M5.Display.setTextColor(YELLOW, BLACK);
            M5.Display.setTextDatum(middle_center);
            M5.Display.drawString("Press M1 / B!", 160, 215);
            M5.Display.setTextDatum(top_left);
            M5.Display.setFont(&fonts::Font0);
            napetune_lcd_release();
            Serial.println("[MECHA COMMS] CONTROL GUIDE; PRESS M1");
        }
        else if (input.pressed & GAME_INPUT_FIRE)
        {
            s_demoGuideDismissed = true;
            core2_glass2_set_demo_playing(1);
            s_lastDrawMs = 0;
            s_externalHudDirty = true;
            napetune_lcd_acquire();
            M5.Display.fillScreen(BLACK);
            napetune_lcd_release();
            Serial.println("[MECHA COMMS] CONTROL GUIDE COMPLETE");
        }
        s_lastUpdateMs = millis();
        return false;
    }
    if (commPauseWasActive)
    {
        commPauseWasActive = false;
        commPauseIzuShown = -1;
        s_lastUpdateMs = millis();
        s_lastDrawMs = 0;
        s_externalHudDirty = true;
        Serial.println("[MECHA COMMS] RESUME");
    }
    if (s_configWasActive)
    {
        // Config owns the full 320x240 LCD, while MECHA updates only the
        // centered viewport. Clear its outer remnants exactly once on exit.
        napetune_lcd_acquire();
        M5.Display.fillScreen(BLACK);
        napetune_lcd_release();
        s_configWasActive = false;
        s_lastDrawMs = 0;
        s_externalHudDirty = true;
    }
    // Menu return is intentionally isolated from gameplay buttons. Holding the
    // upper-left corner opens a touch-only YES/NO confirmation; the upper-right
    // remains reserved for the existing settings panel.
    if (serviceExitConfirmation()) return true;
    if (s_exitConfirmVisible) return false;

    const uint32_t now = millis();
    uint32_t elapsed = now - s_lastUpdateMs;
    s_lastUpdateMs = now;
    if (elapsed > 50) elapsed = 50;
    const float dt = elapsed / 1000.0f;

    if (s_storyMode && s_demoStoryPhase == DemoStoryPhase::UNLOCK_CINEMATIC)
    {
        // While the GLASS2 dialogue is active, the common communication-pause
        // branch above owns Core2 and shows only "重要通信 / GLASS2を確認".
        // Reaching this branch means the last M1 advance completed the scene.
        s_demoStoryPhase = DemoStoryPhase::UNLOCK_GUIDE;
        s_demoPhaseStartedMs = now;
        core2_glass2_set_demo_impulse_unlocked(1);
        drawDemoUnlockGuide();
        core2_glass2_context("IMPULSE FORM", "AVAILABLE",
            "M1で脱出ミッションを開始");
        s_lastUpdateMs = now;
        return false;
    }
    if (s_storyMode && s_demoStoryPhase == DemoStoryPhase::UNLOCK_GUIDE)
    {
        if (input.pressed & GAME_INPUT_FIRE)
        {
            s_demoStoryPhase = DemoStoryPhase::IMPULSE_REVEAL;
            s_demoPhaseStartedMs = now;
            s_demoRevealFlareLaunched = false;
            s_demoRevealMissileAlerted = false;
            s_demoRevealTransformStartMs = 0;
            s_rearMissileActive = false;
            s_rearMissileElapsedMs = 0;
            s_rearMissileEscapeMs = 0;
            s_flareInterceptPending = false;
            s_playerHull = 3;
            core2_glass2_set_demo_playing(0);
            core2_glass2_context("IMPULSE FORM", "01 TO TRANSFORM",
                "01ボタンで変形せよ");
            napetune_lcd_acquire();
            M5.Display.fillScreen(BLACK);
            napetune_lcd_release();
            s_lastDrawMs = 0;
            s_externalHudDirty = true;
            Serial.println("[MECHA DEMO] SCRIPTED IMPULSE / FLARE START");
        }
        s_lastUpdateMs = now;
        return false;
    }
    if (s_storyMode && s_demoStoryPhase == DemoStoryPhase::IMPULSE_REVEAL)
    {
        if (s_demoRevealTransformStartMs == 0)
        {
            s_boostActive = false;
            s_transformBlend = 0.0f;
            s_forwardLean = 0.0f;
            s_boostVisual = 0.0f;
            s_effectiveThrottlePercent = 100.0f;
            s_throttlePercent = 100.0f;
            // Hold the scripted missile behind the craft until the visitor
            // presses 01, so the prompt cannot cause an unavoidable hit.
            if (input.pressed & GAME_INPUT_RUN)
            {
                s_demoRevealTransformStartMs = now;
                core2_glass2_context("IMPULSE FORM", "TRANSFORM",
                    "インパルスフォーム起動");
                Serial.println("[MECHA DEMO] PLAYER TRANSFORM INPUT");
            }
            mecha_game_draw();
            s_lastDrawMs = millis();
            drawExternalHud();
            s_lastUpdateMs = now;
            return false;
        }

        const uint32_t age = now - s_demoRevealTransformStartMs;
        s_boostActive = true;
        const float reveal = constrain(age / 1800.0f, 0.0f, 1.0f);
        s_transformBlend = reveal;
        s_forwardLean = reveal;
        s_boostVisual = reveal;
        s_effectiveThrottlePercent = 145.0f;
        s_throttlePercent = 145.0f;

        // This cinematic still advances the actual route and warp field.
        // Previously the normal update path was bypassed, making the craft
        // appear suspended while the transformation and flare played.
        constexpr float revealSpeed = 34.0f;
        s_currentSpeed = revealSpeed;
        updateWarpStars(dt, revealSpeed);
        s_travel += revealSpeed * dt;
        updateCourseMap();

        if (!s_demoRevealMissileAlerted && age >= 2600)
        {
            s_demoRevealMissileAlerted = true;
            s_rearMissileActive = true;
            s_rearMissileElapsedMs = 2800;
            s_rearMissileEscapeMs = 0;
            s_externalHudDirty = true;
            core2_glass2_context("MISSILE ALERT", "REAR MISSILE",
                "背後からミサイル接近");
            Serial.println("[MECHA DEMO] POST-TRANSFORM MISSILE ALERT");
        }

        // Let the transformed craft establish forward speed before performing
        // a full vertical loop. This rotates the route itself around the craft
        // instead of merely spinning the sprite around its longitudinal axis.
        if (age >= 3300 && age < 4800)
        {
            if (!s_manualLoopActive)
            {
                s_manualLoopActive = true;
                s_manualLoopStartMs = now;
                s_loopBaseShipY = s_shipY;
                s_loopBaseAltitudeM = s_altitudeM;
                s_loopDirection = 1;
            }
            const float loopPhase = constrain((age - 3300) / 1500.0f,
                                              0.0f, 1.0f);
            const float eased = smoothCoursePhase(loopPhase);
            const float angle = TWO_PI * eased;
            constexpr float cameraFollow = 0.72f;
            s_courseLoopSin = sinf(angle) * cameraFollow;
            s_courseLoopCos = 1.0f + (cosf(angle) - 1.0f) * cameraFollow;
            s_shipY = constrain(s_loopBaseShipY - sinf(angle) * 0.78f,
                                -1.65f, 1.55f);
            s_altitudeM = constrain(s_loopBaseAltitudeM +
                (1.0f - cosf(angle)) * 620.0f,
                MIN_ALTITUDE_M, MAX_ALTITUDE_M);
        }
        else if (age >= 4800 && s_manualLoopActive)
        {
            s_manualLoopActive = false;
            s_courseLoopSin = 0.0f;
            s_courseLoopCos = 1.0f;
            s_shipY = constrain(s_loopBaseShipY, -1.65f, 1.55f);
        }

        if (!s_demoRevealFlareLaunched && age >= 5050)
        {
            s_demoRevealFlareLaunched = true;
            s_flareVisualUntilMs = now + 1400;
            s_flareLungeStartMs = now;
            s_flareOriginX = constrain(CX + (int)(s_shipX * 25.0f), 45, W - 45);
            s_flareOriginY = constrain(153 + (int)(s_shipY * 25.0f), 118, 147);
            s_flareTargetX = constrain(s_flareOriginX + 42, 12, W - 12);
            s_flareTargetY = constrain(s_flareOriginY - 24, 18, H - 12);
            s_flareHomingBurst = true;
            s_flareInterceptPending = true;
            s_flareInterceptAtMs = now + 1150;
            core2_glass2_context("FLARE", "AUTO INTERCEPT",
                "フレア展開　迎撃");
        }
        // This scripted interception must not enter the normal threat update:
        // autopilot suppression there can discard the cinematic missile and
        // its pending flare before the detonation deadline.
        if (s_demoRevealMissileAlerted)
        {
            if (!s_demoRevealFlareLaunched)
                s_rearMissileElapsedMs = 3000;
            else if (s_flareInterceptPending &&
                     (int32_t)(now - s_flareInterceptAtMs) >= 0)
                resolveRearMissile(now, "INTERCEPTED FLARE", false);
        }
        mecha_game_draw();
        s_lastDrawMs = millis();
        drawExternalHud();
        if (age >= 8200)
        {
            s_demoStoryPhase = DemoStoryPhase::ESCAPE;
            s_demoPhaseStartedMs = now;
            s_demoExitTravel = s_travel + DEMO_EXIT_DISTANCE;
            s_demoLastCountdown = 255;
            s_nextRearMissileMs = now + 1900;
            s_boostActive = false;
            s_previousBoost = false;
            s_rollActive = false;
            s_rollAngleDeg = 0.0f;
            s_barrelSin = 0.0f;
            s_barrelCos = 1.0f;
            s_manualLoopActive = false;
            s_courseLoopSin = 0.0f;
            s_courseLoopCos = 1.0f;
            Serial.printf("[MECHA DEMO] ESCAPE START exit=%.1f\n", s_demoExitTravel);
        }
        s_lastUpdateMs = now;
        return false;
    }
    if (s_storyMode && s_demoStoryPhase == DemoStoryPhase::SUCCESS_COMMS)
    {
        // The final GLASS2 page has just been advanced. Keep it visible and
        // resume the live result screen without replacing it with a banner.
        s_demoStoryPhase = DemoStoryPhase::SUCCESS;
        s_demoPhaseStartedMs = now;
        s_lastUpdateMs = now;
        return false;
    }
    if ((s_storyMode && s_demoStoryPhase == DemoStoryPhase::SUCCESS) ||
        ((s_storyMode || s_demoMode) &&
         s_demoStoryPhase == DemoStoryPhase::FAILED))
    {
        mecha_game_draw();
        s_lastDrawMs = millis();
        drawExternalHud();
        if (now - s_demoPhaseStartedMs >= 800 &&
            (input.pressed & GAME_INPUT_FIRE))
        {
            if (s_demoMode)
            {
                core2_glass2_set_demo_playing(0);
                return true;
            }
            s_demoStoryPhase = DemoStoryPhase::KV_CARD;
            s_demoPhaseStartedMs = now;
            s_demoKvLastFadeStep = 255;
            core2_glass2_context("TERMINALIS", "KEY VISUAL", "星間のTERMINALIS");
        }
        s_lastUpdateMs = now;
        return false;
    }
    if (s_storyMode && s_demoStoryPhase == DemoStoryPhase::KV_CARD)
    {
        const uint32_t age = now - s_demoPhaseStartedMs;
        // Menu SHOWCASE is an instant page viewer. STORY alone keeps the
        // cinematic fade-in/out timing.
        drawDemoKvCard(s_showcaseMode ? 2200 : age);
        const int8_t swipe = s_showcaseMode ? pollShowcaseSwipe() : 0;
        if (s_showcaseMode && swipe < 0)
        {
            core2_glass2_set_demo_playing(0);
            return true;
        }
        if ((s_showcaseMode && swipe > 0) ||
            (!s_showcaseMode && age >= 6600))
        {
            s_demoStoryPhase = DemoStoryPhase::END_CARD;
            core2_glass2_context("THANK YOU", "CONTINUE ONLINE",
                "QRコードからプロローグへ");
            drawDemoEndCard();
        }
        s_lastUpdateMs = now;
        return false;
    }
    if (s_storyMode && s_demoStoryPhase == DemoStoryPhase::END_CARD)
    {
        const int8_t swipe = s_showcaseMode ? pollShowcaseSwipe() : 0;
        if (s_showcaseMode && swipe < 0)
        {
            s_demoStoryPhase = DemoStoryPhase::KV_CARD;
            s_demoPhaseStartedMs = now;
            s_demoKvLastFadeStep = 255;
            core2_glass2_context("TERMINALIS", "KEY VISUAL",
                "右スワイプでプロローグQRへ");
            drawDemoKvCard(2200);
            s_lastUpdateMs = now;
            return false;
        }
        if (input.pressed & GAME_INPUT_FIRE)
        {
            core2_glass2_set_demo_playing(0);
            return true;
        }
        s_lastUpdateMs = now;
        return false;
    }

    if (s_storyMode && s_demoStoryPhase == DemoStoryPhase::ESCAPE)
    {
        const uint32_t age = now - s_demoPhaseStartedMs;
        const uint8_t seconds = age >= DEMO_ESCAPE_MS ? 0
            : (uint8_t)((DEMO_ESCAPE_MS - age + 999) / 1000);
        if (seconds != s_demoLastCountdown)
        {
            s_demoLastCountdown = seconds;
            char countdown[48];
            snprintf(countdown, sizeof(countdown),
                     "EXIT POINTへ到達せよ　残り%u秒", (unsigned)seconds);
            core2_glass2_context("ESCAPE MISSION", "REACH EXIT POINT", countdown);
        }
        if (s_travel >= s_demoExitTravel)
        {
            s_demoStoryPhase = DemoStoryPhase::SUCCESS_COMMS;
            s_demoPhaseStartedMs = now;
            s_rearMissileActive = false;
            for (EnemyShot& shot : s_enemyShots) shot.startMs = 0;
            core2_glass2_demo_success_message();
            Serial.println("[MECHA DEMO] MISSION COMPLETE");
            return false;
        }
        if (age >= DEMO_ESCAPE_MS)
        {
            s_demoStoryPhase = DemoStoryPhase::FAIL_GRACE;
            s_demoPhaseStartedMs = now;
            // Replace the countdown immediately instead of queuing behind its
            // last frame, then leave enough time to reveal the full line.
            core2_glass2_paged_message("IZU:", "もう・・・だめかも・・・");
            Serial.println("[MECHA DEMO] ESCAPE TIMEOUT");
        }
    }
    else if ((s_storyMode || s_demoMode) &&
             s_demoStoryPhase == DemoStoryPhase::FAIL_GRACE)
    {
        if (now - s_demoPhaseStartedMs >= 1900)
        {
            s_demoStoryPhase = DemoStoryPhase::FAILED;
            s_demoPhaseStartedMs = now;
            s_rearMissileActive = false;
            s_rearMissileHitStartMs = now;
            s_collisionShakeUntilMs = now + 1100;
            for (EnemyShot& shot : s_enemyShots) shot.startMs = 0;
        }
        else
        {
            mecha_game_draw();
            s_lastDrawMs = millis();
            drawExternalHud();
        }
        s_lastUpdateMs = now;
        return false;
    }

    // Demo autopilot yields immediately to any real control activity, then
    // resumes after a short quiet period. It writes only the local frame's
    // GameInputState; USB and the shared input bridge remain untouched.
    const bool manualActivity = abs(input.aim_x) > 2 || abs(input.aim_y) > 2 ||
        input.buttons != 0 || input.pressed != 0 || input.released != 0 ||
        input.wheel != 0;
    if (s_autoPilotEnabled && manualActivity)
    {
        const bool leavingAutoControl = s_autoPilotWasDriving;
        s_autoPilotManualUntilMs = now + 2500;
        s_autoPilotWasDriving = false;
        s_autoPilotFireDown = false;
        if (s_autoPilotUseDown)
        {
            input.buttons &= ~GAME_INPUT_USE;
            input.released |= GAME_INPUT_USE;
        }
        s_autoPilotUseDown = false;
        if (leavingAutoControl)
        {
            // Demo choreography owns throttle and stunt state. Restore the
            // player's pre-demo throttle immediately when real input takes
            // over; otherwise the demo's 135% exit stage stays just above the
            // automatic flight-form threshold indefinitely.
            s_throttlePercent = s_autoPilotSavedThrottle;
            s_effectiveThrottlePercent = min(s_effectiveThrottlePercent,
                                              (float)s_throttlePercent);
            s_manualLoopActive = false;
            s_useHoldPending = false;
            s_rollActive = false;
            s_barrelSin = 0.0f;
            s_barrelCos = 1.0f;
            s_rollAngleDeg = 0.0f;
            s_rollThrust = 0.0f;
            s_externalHudDirty = true;
            Serial.printf("[MECHA AUTOPILOT] MANUAL RECOVER throttle=%d form=RELEASE\n",
                          (int)s_throttlePercent);
        }
    }
    const bool autoPilotDriving = s_autoPilotEnabled && !manualActivity &&
        (int32_t)(now - s_autoPilotManualUntilMs) >= 0;
    if (autoPilotDriving)
    {
        // Resume from a clean act boundary. Previously the 7-second weapon
        // clock and 28-second maneuver clock drifted apart after AUTO HOLD,
        // allowing cannon fire, loops and interceptors to overlap.
        if (!s_autoPilotWasDriving)
        {
            s_autoPilotWasDriving = true;
            s_autoPilotLastWeaponMs = now;
            s_autoPilotDemoStartMs = now;
            s_autoPilotThrottleStage = 255;
            s_autoPilotFireDown = false;
            s_autoPilotUseDown = false;
            s_weapon = 0;
            s_weaponEffectStartMs = 0;
            s_missileDamageApplied = false;
            s_missileTarget = -1;
            s_beamState = BeamState::IDLE;
            s_beamStateSinceMs = now;
            s_multiLockCharging = false;
            s_multiLockCount = 0;
            for (int i = 0; i < TARGET_COUNT; ++i) s_multiLocked[i] = false;
            s_weaponBannerUntilMs = now + 700;
            s_externalHudDirty = true;
            Serial.printf("[MECHA DEMO] REEL SYNC weapon=%s mode=%s\n",
                          weaponName(), "ALL");
        }
        if (s_autoPilotLastWeaponMs == 0) s_autoPilotLastWeaponMs = now;
        if (s_autoPilotDemoStartMs == 0) s_autoPilotDemoStartMs = now;
        if (now - s_autoPilotLastWeaponMs >= 7000)
        {
            input.wheel = 1;
            s_autoPilotLastWeaponMs = now;
            s_autoPilotFireDown = false;
            s_autoPilotThrottleStage = 255;
        }

        const uint32_t weaponAge = now - s_autoPilotLastWeaponMs;
        const uint32_t demoAge = (now - s_autoPilotDemoStartMs) % 28000;
        int steerX = 0, steerY = 0;
        if (s_lockCandidate >= 0)
        {
            steerX = constrain((s_lockCandidateX - (int)s_freeAimX) * 2, -32, 32);
            steerY = constrain((s_lockCandidateY - (int)s_freeAimY) * 2, -28, 28);
        }
        else
        {
            const float targetX = sinf(s_travel * 0.055f) * 1.15f;
            const float targetY = sinf(s_travel * 0.031f) * 0.42f;
            steerX = constrain((int)((targetX - s_shipX) * 23.0f - s_shipVx * 2.5f), -28, 28);
            steerY = constrain((int)((targetY - s_shipY) * 20.0f - s_shipVy * 2.2f), -24, 24);
        }

        // Broad, sparse altitude choreography. It adds visible climb/dive
        // attitude without constantly fighting the target-tracking correction.
        if (s_altitudeM < 1900.0f) steerY -= 18;
        if ((demoAge >= 1800 && demoAge < 3400) ||
            (demoAge >= 14500 && demoAge < 16000)) steerY -= 9;
        if ((demoAge >= 9000 && demoAge < 10600) ||
            (demoAge >= 18800 && demoAge < 20300)) steerY += 10;
        steerY = constrain(steerY, -30, 30);
        input.aim_x = (int16_t)steerX;
        input.aim_y = (int16_t)steerY;

        // Fast approach, deliberate firing pass, then boosted exit. Only two
        // transitions per act keep the rhythm clear and serial output quiet.
        const uint8_t throttleStage = weaponAge < 1400 ? 0
                                    : weaponAge < 5400 ? 1 : 2;
        if (throttleStage != s_autoPilotThrottleStage)
        {
            s_autoPilotThrottleStage = throttleStage;
            s_throttlePercent = throttleStage == 0 ? 120.0f
                                : throttleStage == 1 ? 75.0f : 135.0f;
            s_externalHudDirty = true;
            Serial.printf("[MECHA DEMO] THROTTLE %d%%\n", (int)s_throttlePercent);
        }

        // Two quick evasive rolls and one full loop per 28-second reel. The
        // loop sits after MULTI release so it never obscures the swarm payoff.
        const bool firstRoll = demoAge >= 6200 && demoAge < 6360;
        const bool secondRoll = demoAge >= 13200 && demoAge < 13360;
        const bool desiredUse = firstRoll || secondRoll ||
                                (demoAge >= 25000 && demoAge < 25900);

        bool desiredFire = false;
        if (s_weapon == 0) desiredFire = weaponAge >= 4700 && weaponAge < 6000;
        else if (s_weapon == 1) desiredFire = weaponAge >= 2700 && weaponAge < 3000;
        else if (s_weapon == 2) desiredFire = weaponAge >= 1800 && weaponAge < 3000;
        else desiredFire = weaponAge >= 1500 && weaponAge < 3600;
        // Never fire through a stunt. Cannon also waits for an actual target
        // candidate, preventing repeated NO_LOCK work and serial stalls.
        if (desiredUse || s_rollActive || s_manualLoopActive) desiredFire = false;
        if (s_weapon == 0 && s_lockedTarget < 0 && s_lockCandidate < 0)
            desiredFire = false;

        if (desiredFire) input.buttons |= GAME_INPUT_FIRE;
        else input.buttons &= ~GAME_INPUT_FIRE;
        if (desiredFire && !s_autoPilotFireDown) input.pressed |= GAME_INPUT_FIRE;
        if (!desiredFire && s_autoPilotFireDown) input.released |= GAME_INPUT_FIRE;
        s_autoPilotFireDown = desiredFire;

        if (desiredUse) input.buttons |= GAME_INPUT_USE;
        else input.buttons &= ~GAME_INPUT_USE;
        if (desiredUse && !s_autoPilotUseDown)
        {
            input.pressed |= GAME_INPUT_USE;
            if (firstRoll) s_autoPilotRollDirection = -1;
            else if (secondRoll) s_autoPilotRollDirection = 1;
        }
        if (!desiredUse && s_autoPilotUseDown)
        {
            input.released |= GAME_INPUT_USE;
            input.aim_x = s_autoPilotRollDirection * 20;
        }
        s_autoPilotUseDown = desiredUse;

        // Boost is reserved for entrance/exit, keeping the slower firing pass
        // readable and making each acceleration feel intentional.
        if (weaponAge < 950 || weaponAge >= 5700) input.buttons |= GAME_INPUT_RUN;
        else input.buttons &= ~GAME_INPUT_RUN;
    }
    if (!autoPilotDriving && !s_demoMode && !s_storyMode &&
        s_weapon != s_loadout.weapon1 &&
        s_weapon != s_loadout.weapon2)
    {
        s_weapon = s_loadout.weapon1;
        s_weaponEffectStartMs = 0;
        s_beamState = BeamState::IDLE;
        s_multiLockCharging = false;
        s_bladeCharging = false;
        s_externalHudDirty = true;
    }
    const float xSensitivity = game_input_get_tuning(6) / 100.0f;
    const float ySensitivity = game_input_get_tuning(7) / 100.0f;
    const bool demoImpulseUnlocked = s_demoMode ||
        (s_storyMode &&
         (s_demoStoryPhase == DemoStoryPhase::ESCAPE ||
          s_demoStoryPhase == DemoStoryPhase::FAIL_GRACE));
    const bool boostInput = (input.buttons & GAME_INPUT_RUN) != 0 && demoImpulseUnlocked;
    // Normal form is the agile close-control mode. Compact flight trades some
    // lateral authority for steadier high-speed inertia. Aileron roll keeps
    // the agile profile even while a previous transformation is settling.
    const bool flightHandling = !s_rollActive &&
        (boostInput || s_manualLoopActive || s_transformBlend > 0.55f);
    // Ball reports define a desired steering velocity. Moving toward that
    // velocity instead of adding every packet suppresses left/right chatter,
    // while the zero-input decay leaves a short, controllable coast.
    const float steeringGain = flightHandling ? 0.105f : 0.126f;
    const float steeringLimit = flightHandling ? 5.0f : 5.8f;
    const float steeringResponse = (flightHandling ? 0.30f : 0.40f) *
        (s_loadout.legs ? 1.14f : 1.0f);
    const float steeringCoast = flightHandling ? 0.28f : 0.12f;
    if (input.aim_x != 0)
    {
        const float desiredVx = constrain(input.aim_x * steeringGain * xSensitivity,
                                          -steeringLimit, steeringLimit);
        s_shipVx += (desiredVx - s_shipVx) * steeringResponse;
    }
    else
    {
        s_shipVx *= powf(steeringCoast, dt);
    }
    // Preserve the earlier, stable vertical aiming feel. Altitude authority
    // is separated from it: tiny aiming corrections move the reticle/craft
    // attitude but contribute little or nothing to actual altitude.
    if (input.aim_y != 0)
    {
        const float desiredVy = constrain(input.aim_y * steeringGain * ySensitivity,
                                          -steeringLimit, steeringLimit);
        s_shipVy += (desiredVy - s_shipVy) * steeringResponse;
    }
    else
    {
        s_shipVy *= powf(steeringCoast, dt);
    }

    constexpr float ALTITUDE_INPUT_DEADZONE = 3.0f;
    constexpr float ALTITUDE_FULL_INPUT = 18.0f;
    const float altitudeInputRaw = fabsf(input.aim_y) * ySensitivity;
    // HID motion packets naturally contain short zero gaps. A fast-attack,
    // slow-release envelope prevents those gaps from pulsing altitude and
    // background motion while preserving immediate strong input.
    if (altitudeInputRaw >= s_verticalInputEnvelope)
        s_verticalInputEnvelope = altitudeInputRaw;
    else
        s_verticalInputEnvelope = max(altitudeInputRaw,
            s_verticalInputEnvelope - dt * 32.0f);
    const float altitudeAuthorityTarget = constrain(
        (s_verticalInputEnvelope - ALTITUDE_INPUT_DEADZONE) /
        (ALTITUDE_FULL_INPUT - ALTITUDE_INPUT_DEADZONE), 0.0f, 1.0f);
    const float altitudeAuthorityResponse = altitudeAuthorityTarget > s_altitudeControlAuthority
        ? 10.0f : 4.5f;
    s_altitudeControlAuthority +=
        (altitudeAuthorityTarget - s_altitudeControlAuthority) *
        min(1.0f, dt * altitudeAuthorityResponse);
    s_shipX += s_shipVx * dt;
    // Vertical ball motion primarily changes attitude/altitude. Keep only a
    // small screen-space translation so dives look down at terrain instead of
    // merely sliding the craft toward the bottom edge.
    s_shipY += s_shipVy * dt * 0.35f;
    // Screen Y grows downward, therefore negative pitch velocity is climb.
    // Altitude continues changing while the craft holds pitch, independently
    // from its limited on-screen position.
    s_altitudeM -= s_shipVy * (flightHandling ? 270.0f : 320.0f)
                 * s_altitudeControlAuthority * dt;
    if (s_altitudeM <= MIN_ALTITUDE_M)
    {
        if (s_shipVy > 0.35f)
        {
            applyTerrainCollision("GROUND", now, 0.0f);
            s_altitudeM = MIN_ALTITUDE_M + 45.0f;
            s_shipVy = -1.5f;
        }
        else
        {
            s_altitudeM = MIN_ALTITUDE_M;
            if (s_shipVy > 0.0f) s_shipVy = 0.0f;
        }
    }
    if (s_altitudeM >= MAX_ALTITUDE_M)
    {
        s_altitudeM = MAX_ALTITUDE_M;
        if (s_shipVy < 0.0f) s_shipVy = 0.0f;
    }
    if (s_shipX < -2.45f) { s_shipX = -2.45f; s_shipVx = 0; }
    if (s_shipX > 2.45f) { s_shipX = 2.45f; s_shipVx = 0; }
    if (s_shipY < -1.65f) { s_shipY = -1.65f; s_shipVy = 0; }
    if (s_shipY > 1.55f) { s_shipY = 1.55f; s_shipVy = 0; }

    const bool boost = boostInput;
    s_boostActive = boost;
    if (boost && !s_previousBoost) s_boostKick = 1.0f;
    s_previousBoost = boost;

    // Boss encounters are intentionally absent from both exhibition STORY
    // and free-play DEMO. Both modes focus on normal-enemy flight handling.
    updateInterceptor(dt, now);

    if (input.pressed & GAME_INPUT_USE)
    {
        s_useHoldPending = true;
        s_useHoldStartMs = now;
    }
    if (s_useHoldPending && (input.buttons & GAME_INPUT_USE) &&
        now - s_useHoldStartMs >= 550)
    {
        s_useHoldPending = false;
        if (!s_manualLoopActive &&
            (int32_t)(now - s_manualLoopCooldownUntilMs) >= 0)
        {
            s_manualLoopActive = true;
            s_manualLoopStartMs = now;
            s_manualLoopCooldownUntilMs = now + 2200;
            s_loopBaseShipY = s_shipY;
            s_loopBaseAltitudeM = s_altitudeM;
            s_loopDirection = 1;
            if (s_transformBlend < 0.72f) s_transformBlend = 0.72f;
            Serial.println("[MECHA SPECIAL] VERTICAL LOOP LONG HOLD");
        }
    }
    if ((input.released & GAME_INPUT_USE) && s_useHoldPending)
    {
        s_useHoldPending = false;
        // A short tap remains the immediate, repeatable left/right roll.
        s_rollDirection = (input.aim_x < 0 || (input.aim_x == 0 && s_shipVx < 0)) ? -1 : 1;
        s_rollStartMs = now;
        s_rollActive = true;
        if (s_transformBlend < 0.68f) s_transformBlend = 0.68f;
        Serial.printf("[MECHA SPECIAL] AILERON ROLL dir=%d\n", s_rollDirection);
    }

    if (input.wheel != 0)
    {
        int next = 0;
        if (s_demoMode || s_storyMode)
        {
            next = (int)s_weapon + (input.wheel > 0 ? 1 : -1);
            if (next < 0) next = GAME_WEAPON_COUNT - 1;
            if (next >= GAME_WEAPON_COUNT) next = 0;
        }
        else next = s_weapon == s_loadout.weapon1 ? s_loadout.weapon2 : s_loadout.weapon1;
        s_weapon = (uint8_t)next;
        s_weaponBannerUntilMs = now + 700;
        s_weaponEffectStartMs = 0;
        s_missileDamageApplied = false;
        s_missileTarget = -1;
        s_beamState = BeamState::IDLE;
        s_multiLockCharging = false;
        s_bladeCharging = false;
        s_multiLockCount = 0;
        for (int i = 0; i < TARGET_COUNT; ++i) s_multiLocked[i] = false;
        s_beamStateSinceMs = now;
        s_externalHudDirty = true;
        Serial.printf("[MECHA WEAPON] %s ammo=INF\n", weaponName());
    }
    if (input.pressed & GAME_INPUT_VIEW_TOGGLE)
    {
        s_routeGuideEnabled = !s_routeGuideEnabled;
        s_externalHudDirty = true;
        Serial.printf("[MECHA GUIDE] %s\n", s_routeGuideEnabled ? "ON" : "OFF");
    }
    if (input.pressed & GAME_INPUT_PAUSE)
    {
        s_flightHudEnabled = !s_flightHudEnabled;
        s_externalHudDirty = true;
        Serial.printf("[MECHA HUD] %s\n", s_flightHudEnabled ? "ON" : "OFF");
    }
    // A tap reacts immediately, while a held key changes continuously at
    // 38 percent per second. Opposing keys cancel each other cleanly.
    const float oldThrottle = s_throttlePercent;
    const bool throttleUpHeld = (input.buttons & GAME_INPUT_FORWARD) != 0;
    const bool throttleDownHeld = (input.buttons & GAME_INPUT_BACKWARD) != 0;
    if ((input.pressed & GAME_INPUT_FORWARD) && !throttleDownHeld)
        s_throttlePercent += 2.0f;
    if ((input.pressed & GAME_INPUT_BACKWARD) && !throttleUpHeld)
        s_throttlePercent -= 2.0f;
    if (throttleUpHeld != throttleDownHeld)
        s_throttlePercent += (throttleUpHeld ? 1.0f : -1.0f) * 38.0f * dt;
    s_throttlePercent = constrain(s_throttlePercent, 0.0f, 160.0f);
    if ((int)oldThrottle != (int)s_throttlePercent)
        s_externalHudDirty = true;
    if ((int)(oldThrottle / 10.0f) != (int)(s_throttlePercent / 10.0f) ||
        (input.released & (GAME_INPUT_FORWARD | GAME_INPUT_BACKWARD)))
        Serial.printf("[MECHA THROTTLE] %d%%\n", (int)s_throttlePercent);
    const bool missileInFlight = s_weapon == 1 && s_weaponEffectStartMs != 0 &&
        now - s_weaponEffectStartMs < MISSILE_FLIGHT_MS;
    const bool firePressed = (input.pressed & GAME_INPUT_FIRE) != 0;
    const bool fireHeld = (input.buttons & GAME_INPUT_FIRE) != 0;
    if (input.pressed & GAME_INPUT_FLARE)
    {
        if ((int32_t)(now - s_flareCooldownUntilMs) >= 0)
        {
            s_flareCooldownUntilMs = now + FLARE_COOLDOWN_MS;
            s_flareVisualUntilMs = now + 850;
            s_flareLungeStartMs = now;
            s_flareOriginX = constrain(CX + (int)(s_shipX * 25.0f), 45, W - 45);
            s_flareOriginY = constrain(153 + (int)(s_shipY * 25.0f), 118, 147);
            s_flareTargetX = constrain(s_flareOriginX + 21, 15, W - 15);
            s_flareTargetY = constrain(s_flareOriginY + 22, 25, H - 15);
            s_flareHomingBurst = s_rearMissileActive;
            s_fireUntilMs = now + 100;
            if (s_rearMissileActive)
            {
                s_flareInterceptPending = true;
                s_flareInterceptAtMs = now + 650;
                Serial.println("[MECHA FLARE] REAR SALVO - HOMING INTERCEPT");
            }
            else Serial.println("[MECHA FLARE] REAR SALVO - NO INBOUND MISSILE");
        }
    }
    if (!fireHeld) s_missileFireLatched = false;
    const bool missileTrigger = s_weapon == 1 && fireHeld && !s_missileFireLatched;
    if (missileTrigger) s_missileFireLatched = true;
    const bool acquireWhileHeld = fireHeld && s_lockedTarget < 0;
    const bool switchOnPress = firePressed &&
        s_lockCandidate != s_lockedTarget;
    // Once a lock exists, deliberately placing the free cursor over another
    // candidate transfers the lock after a short stability dwell. This keeps
    // normal steering jitter from flickering between nearby contacts.
    const bool beamIsFiring = s_weapon == 2 && s_beamState == BeamState::FIRING;
    if (!missileInFlight && !beamIsFiring &&
        s_lockedTarget >= 0 &&
        s_lockCandidate >= 0 && s_lockCandidate != s_lockedTarget)
    {
        if (s_cursorSwitchCandidate != s_lockCandidate)
        {
            s_cursorSwitchCandidate = s_lockCandidate;
            s_cursorSwitchSinceMs = now;
        }
    }
    else
    {
        s_cursorSwitchCandidate = -1;
        s_cursorSwitchSinceMs = 0;
    }
    const bool switchByCursor = s_cursorSwitchCandidate >= 0 &&
        now - s_cursorSwitchSinceMs >= 140;
    if (!missileInFlight && s_lockCandidate >= 0 &&
        (acquireWhileHeld || switchOnPress || switchByCursor))
    {
        s_lockedTarget = s_lockCandidate;
        s_lockedTargetX = s_lockCandidateX;
        s_lockedTargetY = s_lockCandidateY;
        s_lockedTargetGround = s_lockCandidateGround;
        s_lockedTargetRange = s_lockCandidateRange;
        // A candidate exists only after altitude/range/visibility checks, so
        // a lock acquired this frame is immediately valid for hitscan fire.
        s_lockedTargetHitEligible = true;
        const int acquireDx = s_lockCandidateX - (int)s_freeAimX;
        const int acquireDy = s_lockCandidateY - (int)s_freeAimY;
        s_lockedAimErrorPx = (int)sqrtf((float)(acquireDx*acquireDx + acquireDy*acquireDy));
        s_externalHudDirty = true;
        Serial.printf("[MECHA LOCK] %s target=%d type=%s range=%.1f\n",
                      switchByCursor ? "CURSOR-SWITCH" :
                      (acquireWhileHeld && !firePressed ? "AUTO-HOLD" : "MANUAL"),
                      s_lockedTarget, s_lockedTargetGround ? "GND" : "AIR",
                      s_lockedTargetRange);
        core2_glass2_message("TARGET LOCKED", "目標捕捉");
        s_cursorSwitchCandidate = -1;
        s_cursorSwitchSinceMs = 0;
    }
    if (s_weapon == 3)
    {
        if (firePressed)
        {
            s_multiLockCharging = true;
            s_multiLockStartMs = now;
            s_multiLockCount = 0;
            for (int i = 0; i < TARGET_COUNT; ++i) s_multiLocked[i] = false;
            Serial.println("[MECHA MULTI] SCAN START");
        }

        if (s_multiLockCharging && fireHeld &&
            now - s_multiLockStartMs >= MULTI_LOCK_ARM_MS)
        {
            // Targets are accumulated, not replaced. Sweeping the ball across
            // a formation while holding FIRE therefore builds a real salvo.
            constexpr int MULTI_CAPTURE_RADIUS = 76;
            for (int i = 0; i < TARGET_COUNT && s_multiLockCount < MULTI_LOCK_MAX; ++i)
            {
                if (!s_targetLockEligible[i] || s_multiLocked[i] ||
                    s_targetDestroyedUntilMs[i] != 0) continue;
                const int dx = s_targetScreenX[i] - (int)s_freeAimX;
                const int dy = s_targetScreenY[i] - (int)s_freeAimY;
                if (dx*dx + dy*dy > MULTI_CAPTURE_RADIUS * MULTI_CAPTURE_RADIUS) continue;
                s_multiLocked[i] = true;
                ++s_multiLockCount;
                Serial.printf("[MECHA MULTI] LOCK target=%d count=%u range=%.1f\n",
                              i, (unsigned)s_multiLockCount, s_targetScreenRange[i]);
            }
        }

        if (s_multiLockCharging && !fireHeld)
        {
            s_multiLockCharging = false;
            uint8_t launched = 0;
            for (int target = 0; target < TARGET_COUNT; ++target)
            {
                if (!s_multiLocked[target]) continue;
                for (int side = -1; side <= 1; side += 2)
                {
                    MultiMissileShot& shot = s_multiShots[s_nextMultiShot];
                    s_nextMultiShot = (uint8_t)((s_nextMultiShot + 1) % MULTI_SHOT_MAX);
                    // Start the visual clock slightly ahead so the first frame
                    // after release is already visibly clear of the launcher.
                    shot.startMs = now > 70 ? now - 70 : 1;
                    shot.target = (int8_t)target;
                    shot.side = (int8_t)side;
                    shot.ordinal = launched++;
                    shot.impactX = s_targetScreenX[target];
                    shot.impactY = s_targetScreenY[target];
                    shot.damageApplied = false;
                }
            }
            s_fireUntilMs = now + 90;
            s_externalHudDirty = true;
            Serial.printf("[MECHA MULTI] RELEASE locks=%u missiles=%u\n",
                          (unsigned)s_multiLockCount, (unsigned)launched);
            s_multiLockCount = 0;
            for (int i = 0; i < TARGET_COUNT; ++i) s_multiLocked[i] = false;
        }
    }
    else
    {
        s_multiLockCharging = false;
    }
    if (s_weapon == 2)
    {
        if (s_beamState == BeamState::IDLE && (input.pressed & GAME_INPUT_FIRE))
        {
            s_beamState = BeamState::CHARGING;
            s_beamStateSinceMs = now;
            s_beamMessageUntilMs = 0;
            Serial.println("[MECHA BEAM] CHARGE START ammo=INF");
        }
        else if (s_beamState == BeamState::CHARGING)
        {
            if (!(input.buttons & GAME_INPUT_FIRE))
            {
                s_beamState = BeamState::IDLE;
                s_beamMessageUntilMs = now + 500;
                Serial.println("[MECHA BEAM] CHARGE CANCEL");
            }
            else if (now - s_beamStateSinceMs >= BEAM_CHARGE_MS)
            {
                s_beamState = BeamState::READY;
                s_beamStateSinceMs = now;
                Serial.println("[MECHA BEAM] READY - RELEASE TO FIRE");
            }
        }
        else if (s_beamState == BeamState::READY &&
                 !(input.buttons & GAME_INPUT_FIRE))
        {
            // A completed charge is always releasable. Lock only assists the
            // initial aim; after release the beam follows the free cursor and
            // may be swept across any eligible target.
            s_beamState = BeamState::FIRING;
            s_beamStateSinceMs = now;
            s_lastBeamDamageMs = now - 180;
            s_externalHudDirty = true;
            Serial.printf("[MECHA BEAM] RELEASE FIRE ammo=INF lock=%d\n",
                          s_lockedTarget);
        }
        else if (s_beamState == BeamState::FIRING &&
                 now - s_beamStateSinceMs >= BEAM_FIRE_MS)
        {
            s_beamState = BeamState::COOLDOWN;
            s_beamStateSinceMs = now;
            Serial.println("[MECHA BEAM] END");
        }
        else if (s_beamState == BeamState::COOLDOWN &&
                 now - s_beamStateSinceMs >= BEAM_COOLDOWN_MS)
        {
            s_beamState = BeamState::IDLE;
        }
    }
    else
    {
        s_beamState = BeamState::IDLE;
    }
    if (s_bladeDualMode && !s_bladeCharging &&
        (int32_t)(now - s_bladeDualUntilMs) >= 0)
    {
        s_bladeDualMode = false;
        s_externalHudDirty = true;
        Serial.println("[MECHA BLADE] DUAL MODE END");
    }
    if (s_weapon == 4)
    {
        if (firePressed && !s_bladeCharging)
        {
            s_bladeCharging = true;
            s_bladeChargeStartMs = now;
            Serial.println("[MECHA BLADE] CHARGE START");
        }
        if (s_bladeCharging && fireHeld && !s_bladeDualMode &&
            now - s_bladeChargeStartMs >= BLADE_DUAL_DEPLOY_MS)
        {
            s_bladeDualMode = true;
            s_bladeDualUntilMs = now + BLADE_DUAL_DURATION_MS;
            s_externalHudDirty = true;
            Serial.println("[MECHA BLADE] DUAL MODE DEPLOY");
        }
        if (s_bladeCharging && !fireHeld)
        {
            const uint32_t heldMs = now - s_bladeChargeStartMs;
            s_bladeCharging = false;
            s_bladeSwingCharged = heldMs >= BLADE_CHARGE_MS;
            if (s_bladeSwingCharged)
            {
                s_bladeSwingStage = 2;
                s_bladeComboStep = 0;
            }
            else
            {
                const bool newCombo = s_bladeComboStep == 0 ||
                    now - s_bladeLastReleaseMs > BLADE_COMBO_RESET_MS;
                if (newCombo)
                {
                    s_bladeComboStep = 0;
                    const bool hasLock = s_lockedTarget >= 0 &&
                        s_targetDestroyedUntilMs[s_lockedTarget] == 0;
                    s_bladeDashTargetX = hasLock ? s_lockedTargetX : -1;
                    s_bladeDashTargetY = hasLock ? s_lockedTargetY : HORIZON_Y;
                }
                s_bladeSwingStage = s_bladeComboStep;
                s_bladeComboStep = (s_bladeComboStep + 1) % 3;
            }
            s_bladeSwingDual = s_bladeSwingStage == 2;
            if (s_bladeSwingStage == 2 && s_bladeSwingCharged)
            {
                const bool hasDashTarget = s_lockedTarget >= 0 &&
                    s_targetDestroyedUntilMs[s_lockedTarget] == 0;
                s_bladeDashTargetX = hasDashTarget
                    ? s_lockedTargetX : (int)s_freeAimX;
                s_bladeDashTargetY = hasDashTarget
                    ? s_lockedTargetY : (int)s_freeAimY;
            }
            s_bladeSwingStartMs = now;
            s_missileCutCount = 0;
            s_bladeLastReleaseMs = now;
            s_fireUntilMs = now + (s_bladeSwingCharged
                ? BLADE_CHARGED_SWING_MS
                : (s_bladeSwingStage == 2
                    ? BLADE_SWING_MS : BLADE_QUICK_SWING_MS));
            s_boostKick = max(s_boostKick, s_bladeSwingStage == 2 ? 0.75f : 0.22f);
            if (s_bladeSwingStage == 2)
                s_bladeDualUntilMs = now + BLADE_DUAL_DURATION_MS;
            const int damage = s_bladeSwingStage == 2
                ? (s_bladeSwingCharged ? 8 : 5) : 2;
            damageLockedTarget(damage,
                s_bladeSwingCharged ? "BLADE-CHARGE" :
                (s_bladeSwingStage == 2 ? "BLADE-COMBO" : "BLADE"));
            Serial.printf("[MECHA BLADE] stage=%u %s damage=%d\n",
                          (unsigned)(s_bladeSwingStage + 1),
                          s_bladeSwingCharged ? "HOLD LUNGE" : "COMBO", damage);
        }
    }
    else
    {
        s_bladeCharging = false;
    }
    float actionRollTarget = 0.0f;
    if (s_rollActive)
    {
        const float phase = (now - s_rollStartMs) / (s_loadout.legs ? 480.0f : 560.0f);
        if (phase >= 1.0f)
        {
            s_rollActive = false; s_barrelSin = 0; s_barrelCos = 1;
            s_rollAngleDeg = 0; s_rollThrust = 0;
        }
        else
        {
            const float eased = phase * phase * (3.0f - 2.0f * phase);
            const float angle = s_rollDirection * TWO_PI * eased;
            s_barrelSin = sinf(angle); s_barrelCos = cosf(angle);
            s_rollAngleDeg = s_rollDirection * 360.0f * eased;
            s_rollThrust = (s_loadout.legs ? 15.0f : 13.0f) * sinf(PI * phase);
            // The ship makes a complete axial roll, while the chase camera
            // only leans into the manoeuvre and naturally returns to level.
            // Boost gets a stronger follow to sell high-speed momentum.
            actionRollTarget = s_rollDirection * sinf(PI * phase) *
                (boostInput ? 0.72f : 0.42f);
            if (s_boostKick < 0.7f * sinf(PI * phase))
                s_boostKick = 0.7f * sinf(PI * phase);
        }
    }
    s_actionCameraRoll += (actionRollTarget - s_actionCameraRoll) *
        min(1.0f, dt * (s_rollActive ? 8.0f : 5.0f));

    // Camera follows the ship with deliberate lag. Horizontal velocity rolls
    // the world in the opposite direction, while boost smoothly widens FOV.
    const float cameraFollow = min(1.0f, dt * 7.0f);
    s_cameraX += (s_shipX * 0.50f - s_cameraX) * cameraFollow;
    s_cameraY += (s_shipY * 0.22f - s_cameraY) * cameraFollow;
    // Coordinated bank: the wing on the requested turn side drops. Positive
    // X therefore lowers the right wing while the craft moves right.
    float targetBank = s_shipVx * (flightHandling ? 0.105f : 0.145f);
    if (targetBank > 0.65f) targetBank = 0.65f;
    if (targetBank < -0.65f) targetBank = -0.65f;
    s_cameraBank += (targetBank - s_cameraBank)
                  * min(1.0f, dt * (flightHandling ? 8.0f : 12.0f));
    float targetPitch = -s_shipVy * (flightHandling ? 0.105f : 0.140f);
    if (targetPitch > 0.62f) targetPitch = 0.62f;
    if (targetPitch < -0.62f) targetPitch = -0.62f;
    s_cameraPitch += (targetPitch - s_cameraPitch)
                   * min(1.0f, dt * (flightHandling ? 9.0f : 12.0f));
    const float targetPoseYaw = constrain(s_shipVx / 4.8f, -1.0f, 1.0f);
    const float targetPosePitch = constrain(s_shipVy / 4.8f, -1.0f, 1.0f);
    s_poseYaw += (targetPoseYaw - s_poseYaw) * min(1.0f, dt * 11.0f);
    s_posePitch += (targetPosePitch - s_posePitch) * min(1.0f, dt * 9.0f);
    // The free reticle follows the craft and steering velocity only. Route
    // curvature no longer pushes it away from the player's ball direction.
    const float desiredAimX = CX + s_shipX * 20.0f + s_shipVx * 10.0f;
    const float desiredAimY = HORIZON_Y + s_shipY * 14.0f + s_shipVy * 15.0f;
    s_freeAimX += (constrain(desiredAimX, 42.0f, W - 42.0f) - s_freeAimX)
                * min(1.0f, dt * 12.0f);
    s_freeAimY += (constrain(desiredAimY, 12.0f, H - 12.0f) - s_freeAimY)
                * min(1.0f, dt * 12.0f);
    float routeX0, routeY0, routeX1, routeY1;
    routePosition(s_travel, &routeX0, &routeY0);
    routePosition(s_travel + 9.0f, &routeX1, &routeY1);
    s_routeLeadX += ((routeX1 - routeX0) - s_routeLeadX) * min(1.0f, dt * 3.0f);
    s_routeLeadY += ((routeY1 - routeY0) - s_routeLeadY) * min(1.0f, dt * 2.7f);
    float targetRouteBank = -(routeX1 - routeX0) * 0.105f;
    if (targetRouteBank > 0.38f) targetRouteBank = 0.38f;
    if (targetRouteBank < -0.38f) targetRouteBank = -0.38f;
    float targetRoutePitch = -(routeY1 - routeY0) * 0.095f;
    if (targetRoutePitch > 0.23f) targetRoutePitch = 0.23f;
    if (targetRoutePitch < -0.23f) targetRoutePitch = -0.23f;
    s_routeBank += (targetRouteBank - s_routeBank) * min(1.0f, dt * 3.6f);
    s_routePitch += (targetRoutePitch - s_routePitch) * min(1.0f, dt * 3.1f);
    s_boostVisual += ((boost ? 1.0f : 0.0f) - s_boostVisual)
                   * min(1.0f, dt * (boost ? 7.0f : 4.0f));
    const float targetFov = 1.0f + s_boostVisual * 0.32f
                           + (s_rollActive ? 0.10f : 0.0f)
                           - (s_beamState == BeamState::FIRING ? 0.08f : 0.0f);
    s_fovScale += (targetFov - s_fovScale) * min(1.0f, dt * (boost ? 8.0f : 3.2f));
    s_boostKick *= max(0.0f, 1.0f - dt * 6.5f);
    const float throttleResponse = s_throttlePercent < s_effectiveThrottlePercent
        ? (s_loadout.thruster ? 3.2f : 2.4f) : 3.2f;
    s_effectiveThrottlePercent +=
        (s_throttlePercent - s_effectiveThrottlePercent) *
        min(1.0f, dt * throttleResponse);
    if (s_throttlePercent <= 0.01f && s_effectiveThrottlePercent < 0.35f)
        s_effectiveThrottlePercent = 0.0f;
    float speed = (boost ? 24.0f * game_input_get_tuning(8) / 100.0f *
                  (s_loadout.thruster ? 1.08f : 1.0f) : 12.0f)
                * (s_effectiveThrottlePercent / 100.0f) + s_rollThrust;
    if (s_beamState == BeamState::FIRING) speed *= 0.62f;
    if ((int32_t)(now - s_collisionSlowUntilMs) < 0) speed *= 0.50f;
    const bool bladeTargetEngaged = s_weapon == 4 && s_bladeDashTargetX >= 0 &&
        ((s_bladeComboStep != 0 &&
          now - s_bladeLastReleaseMs <= BLADE_COMBO_RESET_MS) ||
         s_bladeSwingStartMs != 0);
    if (bladeTargetEngaged)
    {
        // Slow only forward rail travel. Steering and reticle response remain
        // untouched, so the machine sticks to its victim without feeling inert.
        speed *= BLADE_COMBO_SPEED_SCALE;
    }
    s_currentSpeed = speed;
    // A normal aileron roll is a humanoid-form evasive action, but boost has
    // priority and keeps the compact flight form through the axial rotation.
    // Speed-only automatic flight never forces a roll into flight form.
    const bool automaticFlight = s_manualLoopActive ||
        speed * 10.0f > FLIGHT_FORM_HUD_SPEED_THRESHOLD;
    const bool flightForm = boost || (!s_rollActive && automaticFlight);
    const float transformResponse = s_rollActive ? 18.0f : (flightForm ? 5.8f : 4.0f);
    s_transformBlend += ((flightForm ? 1.0f : 0.0f) - s_transformBlend)
                      * min(1.0f, dt * transformResponse);
    const float leanTarget = constrain((speed - 8.0f) / 9.0f
                          + fabsf(s_poseYaw) * 0.38f
                          + fabsf(s_posePitch) * 0.20f
                          + (s_rollActive ? 0.75f : 0.0f), 0.0f, 1.0f);
    s_forwardLean += (leanTarget - s_forwardLean)
                   * min(1.0f, dt * (leanTarget > s_forwardLean ? 10.0f : 5.0f));
    updateWarpStars(dt, speed);
    const float previousTravel = s_travel;
    s_travel += speed * dt;
    updateTunnelRouteTrigger(now);
    enforceTunnelPassage(previousTravel, now);
    if (!s_tunnelRunActive)
    {
        updateTerrainCollisions(now);
        updateRouteEnemyCollisions(now);
        if (!s_demoMode && !s_storyMode) updateBossCollision(now);
    }
    updateCourseMap();
    if (s_manualLoopActive)
    {
        const float phase = (now - s_manualLoopStartMs) / 1350.0f;
        if (phase >= 1.0f)
        {
            s_manualLoopActive = false;
            s_courseLoopSin = 0.0f; s_courseLoopCos = 1.0f;
            s_shipY = constrain(s_loopBaseShipY, -1.65f, 1.55f);
            s_altitudeM = constrain(s_loopBaseAltitudeM, MIN_ALTITUDE_M, MAX_ALTITUDE_M);
            Serial.println("[MECHA SPECIAL] VERTICAL LOOP COMPLETE");
        }
        else
        {
            const float eased = smoothCoursePhase(phase);
            const float angle = s_loopDirection * TWO_PI * eased;
            // Blend the loop camera toward the craft instead of attaching it
            // rigidly. Mixing the rotation matrix with identity keeps both
            // endpoints level, avoids a reset snap, and preserves orientation.
            constexpr float LOOP_CAMERA_FOLLOW = 0.58f;
            s_courseLoopSin = sinf(angle) * LOOP_CAMERA_FOLLOW;
            s_courseLoopCos = 1.0f + (cosf(angle) - 1.0f) * LOOP_CAMERA_FOLLOW;
            // The craft now travels around the loop instead of remaining
            // pinned to one screen coordinate while only the world rotates.
            s_shipY = constrain(s_loopBaseShipY - sinf(angle) * 0.92f,
                                -1.65f, 1.55f);
            const float altitudeArc = s_loopDirection > 0 ? 1050.0f
                : -min(720.0f, max(0.0f, s_loopBaseAltitudeM - MIN_ALTITUDE_M - 80.0f));
            s_altitudeM = constrain(s_loopBaseAltitudeM + sinf(PI * eased) * altitudeArc,
                                    MIN_ALTITUDE_M, MAX_ALTITUDE_M);
            s_courseSection = "VERT LOOP";
            s_courseHeadingOffset = eased < 0.5f ? 0 : 180;
        }
    }
    if (!s_tunnelRunActive)
    {
        updateRearMissile(elapsed, now, boost);
        updateEnemyWeapons(now);
    }
    // Multi-lock missiles resolve independently so one formation can receive
    // several staggered impacts without occupying the normal missile state.
    for (MultiMissileShot& shot : s_multiShots)
    {
        if (shot.startMs == 0) continue;
        const uint32_t age = now - shot.startMs;
        // Refresh screen-space homing at roughly half rate; interpolation is
        // still performed every draw frame and remains visually smooth.
        if (((now >> 5) + shot.ordinal) % 2 == 0 &&
            shot.target >= 0 && shot.target < TARGET_COUNT &&
            s_targetDestroyedUntilMs[shot.target] == 0)
        {
            shot.impactX = s_targetScreenX[shot.target];
            shot.impactY = s_targetScreenY[shot.target];
        }
        if (!shot.damageApplied && age >= MULTI_MISSILE_FLIGHT_MS)
        {
            shot.damageApplied = true;
            // Both missiles deal damage, but one shared blast per target pair
            // avoids drawing duplicate circles at identical coordinates.
            if (shot.side < 0)
            {
                MissileBlast& blast = s_missileBlasts[s_nextMissileBlast];
                s_nextMissileBlast = (uint8_t)((s_nextMissileBlast + 1) % 8);
                blast.startMs = now;
                blast.x = shot.impactX;
                blast.y = shot.impactY;
                blast.heavy = true;
            }
            if (shot.target >= 0 && shot.target < TARGET_COUNT)
                applyTargetDamage(shot.target, 2, "MULTI", s_targetScreenRange[shot.target],
                                  shot.impactX, shot.impactY,
                                  s_targetScreenGround[shot.target]);
        }
        if (age >= MULTI_MISSILE_FLIGHT_MS + 40) shot.startMs = 0;
    }

    // Resolve arrival separately from the fire-selection chain. This allows a
    // fresh click on the exact impact frame to launch the next salvo.
    if (s_weapon == 1 && s_weaponEffectStartMs != 0 &&
        !s_missileDamageApplied &&
        now - s_weaponEffectStartMs >= MISSILE_FLIGHT_MS)
    {
        s_missileDamageApplied = true;
        const bool validTarget = s_missileTarget >= 0 && s_missileTarget < TARGET_COUNT;
        const int targetX = validTarget ? s_targetScreenX[s_missileTarget] : s_lockedTargetX;
        const int targetY = validTarget ? s_targetScreenY[s_missileTarget] : s_lockedTargetY;
        MissileBlast& blast = s_missileBlasts[s_nextMissileBlast];
        s_nextMissileBlast = (uint8_t)((s_nextMissileBlast + 1) % 8);
        blast.startMs = now;
        blast.x = targetX + (CX - targetX) * 18 / 100;
        blast.y = targetY + (HORIZON_Y - targetY) * 28 / 100;
        blast.heavy = s_missileTarget == s_lockedTarget;
        if (s_missileTarget == s_lockedTarget)
            damageLockedTarget(4, "MISSILE");
        else
            Serial.printf("[MECHA MISS] weapon=MISSILE launchTarget=%d current=%d\n",
                          s_missileTarget, s_lockedTarget);
    }

    if (s_weapon == 2 && s_beamState == BeamState::FIRING)
    {
        s_weaponEffectStartMs = s_beamStateSinceMs;
        s_fireUntilMs = now + 80;
        if (now - s_beamStateSinceMs >= BEAM_IGNITION_MS &&
            now - s_lastBeamDamageMs >= 180)
        {
            s_lastBeamDamageMs = now;
            damageBeamSweep();
        }
    }
    else if (s_weapon == 0 && (input.buttons & GAME_INPUT_FIRE))
    {
        if (now - s_lastWeaponShotMs >= 120)
        {
            s_lastWeaponShotMs = now;
            s_weaponEffectStartMs = now;
            damageLockedTarget(1, "CANNON");
        }
        s_fireUntilMs = now + 65;
    }
    else if (s_weapon == 1 && missileTrigger)
    {
        // One press launches one twin-missile salvo. Holding FIRE cannot
        // retrigger it. A new press is accepted as soon as the previous salvo
        // reaches impact; its persistent blast continues in a separate slot.
        const bool missileActive = s_weaponEffectStartMs != 0 &&
            now - s_weaponEffectStartMs < MISSILE_FLIGHT_MS;
        if (!missileActive)
        {
            s_lastWeaponShotMs = now;
            s_weaponEffectStartMs = now;
            s_missileDamageApplied = false;
            s_missileTarget = s_lockedTarget;
            s_fireUntilMs = now + 65;
            s_externalHudDirty = true;
            Serial.println("[MECHA MISSILE] TWIN SALVO ammo=INF");
        }
        else
        {
            Serial.println("[MECHA MISSILE] BUSY - WAIT IMPACT");
        }
    }

    // Any lethal hit in STORY or DEMO enters the same live-scene failure
    // sequence used by the timed escape mission. GLASS2 gets Iz's line first;
    // after the grace period the craft visibly chain-explodes and the normal
    // MISSION FAILED overlay takes over.
    if (PLAYER_HULL_ENABLED && (s_storyMode || s_demoMode) && s_playerHull <= 0 &&
        s_demoStoryPhase != DemoStoryPhase::FAIL_GRACE &&
        s_demoStoryPhase != DemoStoryPhase::FAILED &&
        s_demoStoryPhase != DemoStoryPhase::SUCCESS &&
        s_demoStoryPhase != DemoStoryPhase::SUCCESS_COMMS &&
        s_demoStoryPhase != DemoStoryPhase::KV_CARD &&
        s_demoStoryPhase != DemoStoryPhase::END_CARD)
    {
        s_demoStoryPhase = DemoStoryPhase::FAIL_GRACE;
        s_demoPhaseStartedMs = now;
        s_rearMissileActive = false;
        s_flareInterceptPending = false;
        for (EnemyShot& shot : s_enemyShots) shot.startMs = 0;
        core2_glass2_paged_message("IZU:", "もう・・・無理かも・・・");
        Serial.println("[MECHA HULL] DEPLETED - FAILURE SEQUENCE");
        s_externalHudDirty = true;
        s_lastUpdateMs = now;
        return false;
    }

    const uint32_t bodyDoneUs = micros();
    const uint32_t bodyUs = bodyDoneUs - loopEnterUs;
    loopBodySumUs += bodyUs;
    if (bodyUs > loopBodyMaxUs) loopBodyMaxUs = bodyUs;

    // Avoid back-to-back catch-up frames after an expensive frame.
    if (now - s_lastDrawMs >= 6)
    {
        mecha_game_draw();
        s_lastDrawMs = millis();
    }

    const uint32_t hudStartUs = micros();
    drawExternalHud();
    const uint32_t hudUs = micros() - hudStartUs;
    loopHudSumUs += hudUs;
    if (hudUs > loopHudMaxUs) loopHudMaxUs = hudUs;
    ++loopSamples;
    const uint32_t loopExitUs = micros();
    loopLastExitUs = loopExitUs;
    if (loopExitUs - loopPerfSinceUs >= 30000000u && loopSamples != 0)
    {
        Serial.printf("[MECHA LOOP PERF] samples=%u gap=%uus poll=%uus body=%uus hud=%uus maxGap=%uus maxPoll=%uus maxBody=%uus maxHud=%uus\n",
                      (unsigned)loopSamples,
                      (unsigned)(loopGapSumUs / loopSamples),
                      (unsigned)(loopPollSumUs / loopSamples),
                      (unsigned)(loopBodySumUs / loopSamples),
                      (unsigned)(loopHudSumUs / loopSamples),
                      (unsigned)loopGapMaxUs, (unsigned)loopPollMaxUs,
                      (unsigned)loopBodyMaxUs, (unsigned)loopHudMaxUs);
        loopPerfSinceUs = loopExitUs;
        loopGapSumUs = loopPollSumUs = loopBodySumUs = loopHudSumUs = 0;
        loopGapMaxUs = loopPollMaxUs = loopBodyMaxUs = loopHudMaxUs = 0;
        loopSamples = 0;
    }
    return false;
}

void mecha_game_end()
{
    game_input_clear_nape();
    M5.Display.setBrightness(128);
    if (s_hybridNormalCanvas.getBuffer()) s_hybridNormalCanvas.deleteSprite();
    if (s_hybridFlightCanvas.getBuffer()) s_hybridFlightCanvas.deleteSprite();
    if (s_bossCanvas.getBuffer()) s_bossCanvas.deleteSprite();
    s_hybridCanvasesReady = false;
    s_bossCanvasReady = false;
    if (s_frameReady) { s_frame.deleteSprite(); s_frameReady = false; }
    Serial.println("[MECHA] high-speed rail flight end");
}

bool mecha_game_get_autopilot()
{
    return s_autoPilotEnabled;
}

void mecha_game_set_autopilot(bool enabled)
{
    const bool wasEnabled = s_autoPilotEnabled;
    if (enabled && !wasEnabled)
        s_autoPilotSavedThrottle = (int)s_throttlePercent;
    s_autoPilotEnabled = enabled;
    s_autoPilotWasDriving = false;
    s_autoPilotFireDown = false;
    s_autoPilotUseDown = false;
    s_autoPilotRollDirection = 1;
    s_autoPilotManualUntilMs = 0;
    s_autoPilotLastWeaponMs = millis();
    s_autoPilotDemoStartMs = millis();
    s_autoPilotThrottleStage = 255;
    if (!enabled && wasEnabled)
    {
        s_throttlePercent = s_autoPilotSavedThrottle;
        s_effectiveThrottlePercent = min(s_effectiveThrottlePercent,
                                          (float)s_throttlePercent);
        s_boostActive = false;
        s_previousBoost = false;
        s_manualLoopActive = false;
        s_useHoldPending = false;
        s_rollActive = false;
        s_barrelSin = 0.0f;
        s_barrelCos = 1.0f;
        s_rollAngleDeg = 0.0f;
        s_rollThrust = 0.0f;
        // Do not snap the artwork. Force a deterministic fast return so the
        // transformation remains visible but can never stay latched.
        if (s_transformBlend > 0.68f) s_transformBlend = 0.68f;
        Serial.printf("[MECHA AUTOPILOT] EXIT RECOVER throttle=%d form=RELEASE\n",
                      (int)s_throttlePercent);
    }
    s_weaponBannerUntilMs = millis() + 700;
    Serial.printf("[MECHA AUTOPILOT] %s\n", enabled ? "ON" : "OFF");
}
