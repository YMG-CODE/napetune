#include <Arduino.h>
#include <M5Unified.h>

#include "game_input.h"
#include "core2_bridge.h"
#include "nape_game_source.h"
#include "spi_guard.h"
#include "tuning/tuning_mode.h"

namespace
{
uint8_t s_viewLayer = 0;
bool s_dirty = true;
bool s_touchWasDown = false;
bool s_touchTracking = false;
int s_touchStartX = 0;
int s_touchStartY = 0;
int s_touchLastX = 0;
int s_touchLastY = 0;
bool s_layerChangePending = false;
bool s_layerSelectedByCore2 = false;
uint8_t s_layerChangeTarget = 0;
uint32_t s_layerChangeStartedMs = 0;
NapeTuningSnapshot s_last = {};
bool s_keyEditor = false;
bool s_keyConfirm = false;
bool s_selectingCategory = true;
uint8_t s_editKey = 4;
uint8_t s_editCategory = 0;
uint8_t s_editChoice = 0;
uint8_t s_keyRowSelection = 0;
uint32_t s_lastBallNavMs = 0;
uint8_t s_glassTutorialPage = 0;
uint32_t s_lastGlassTutorialMs = 0;
uint32_t s_glassTutorialHoldUntilMs = 0;
static const uint8_t kKeyOrder[7] = {4, 5, 2, 3, 0, 1, 6};
static const char* const kKeyLabels[7] =
    {"M1", "M2", "K01", "K02", "K03", "K04", "ROUND"};
static constexpr uint8_t kSelectableRows = 9;
static constexpr uint8_t kLayerSelection = kSelectableRows;

const char* selectedRowLabel(uint8_t row)
{
    if (row < 7) return kKeyLabels[row];
    return row == 7 ? "WHEEL+" : "WHEEL-";
}
static const char* const kCategoryNames[] = {
    "BASIC", "MOUSE", "SHORTCUT", "LAYER", "TUNING", "MACRO", "FUNCTION",
    "ALPHA/NUM", "SYMBOL", "MEDIA"
};
static constexpr uint8_t kCategoryCount =
    sizeof(kCategoryNames) / sizeof(kCategoryNames[0]);
static const uint16_t kBasicCodes[] = {
    0x0000, 0x0001, 0x0028, 0x0029, 0x002B, 0x002C,
    0x002A, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E,
    0x004F, 0x0050, 0x0051, 0x0052,
    0x00E0, 0x00E1, 0x00E2, 0x00E3
};
static const uint16_t kMouseCodes[] = {
    0x00D1, 0x00D2, 0x00D3, 0x00D4, 0x00D5,
    0x00D9, 0x00DA, 0x00DB, 0x00DC, 0x5229, 0x522A, 0x522B
};
static const uint16_t kShortcutCodes[] = {
    0x0106, 0x0119, 0x011B, 0x011D, 0x0104,
    0x0207, 0x0209, 0x0213
};
static const uint16_t kLayerCodes[] = {
    0x5201, 0x5202, 0x5203, 0x5204, 0x5205, 0x5206, 0x5207, 0x5208,
    0x5221, 0x5222, 0x5223, 0x5224, 0x5225, 0x5226, 0x5227, 0x5228
};
static const uint16_t kTuningCodes[] = {
    0x7E25, 0x7E26, 0x7E2C, 0x7E2F, 0x7E2D, 0x7E27, 0x7E28,
    0x7E2B, 0x7E2E, 0x7E30
};
static const uint16_t kMacroCodes[] = {
    0x7700, 0x7701, 0x7702, 0x7703, 0x7704, 0x7705, 0x7706, 0x7707,
    0x7708, 0x7709, 0x770A, 0x770B, 0x770C, 0x770D, 0x770E, 0x770F
};
static const uint16_t kFunctionCodes[] = {
    0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,
    0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045
};
static const uint16_t kAlphaNumericCodes[] = {
    0x0004, 0x0005, 0x0006, 0x0007, 0x0008, 0x0009, 0x000A,
    0x000B, 0x000C, 0x000D, 0x000E, 0x000F, 0x0010, 0x0011,
    0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017, 0x0018,
    0x0019, 0x001A, 0x001B, 0x001C, 0x001D,
    0x001E, 0x001F, 0x0020, 0x0021, 0x0022,
    0x0023, 0x0024, 0x0025, 0x0026, 0x0027
};
static const uint16_t kSymbolCodes[] = {
    0x002D, 0x002E, 0x002F, 0x0030, 0x0031,
    0x0033, 0x0034, 0x0035, 0x0036, 0x0037, 0x0038
};
static const uint16_t kMediaCodes[] = {
    0x00A8, 0x00A9, 0x00AA, 0x00AB, 0x00AC, 0x00AD,
    0x00AE, 0x00AF, 0x00B0, 0x00BB, 0x00BC, 0x00C1, 0x00C2
};

const uint16_t* editCodes(uint8_t category, uint8_t* count)
{
    switch (category)
    {
        case 0: *count = sizeof(kBasicCodes) / 2; return kBasicCodes;
        case 1: *count = sizeof(kMouseCodes) / 2; return kMouseCodes;
        case 2: *count = sizeof(kShortcutCodes) / 2; return kShortcutCodes;
        case 3: *count = sizeof(kLayerCodes) / 2; return kLayerCodes;
        case 4: *count = sizeof(kTuningCodes) / 2; return kTuningCodes;
        case 5: *count = sizeof(kMacroCodes) / 2; return kMacroCodes;
        case 6: *count = sizeof(kFunctionCodes) / 2; return kFunctionCodes;
        case 7: *count = sizeof(kAlphaNumericCodes) / 2; return kAlphaNumericCodes;
        case 8: *count = sizeof(kSymbolCodes) / 2; return kSymbolCodes;
        default: *count = sizeof(kMediaCodes) / 2; return kMediaCodes;
    }
}

uint16_t selectedEditCode()
{
    uint8_t count = 0;
    const uint16_t* codes = editCodes(s_editCategory, &count);
    return codes[s_editChoice % count];
}

void updateGlass2TuningDetail(uint8_t layer)
{
    char title[32];
    snprintf(title, sizeof(title), "KEY ASSIGN / LAYER %u", (unsigned)layer);
    core2_glass2_context(
        "TUNING", title, "ボール上下：キーを選択"
    );
    s_glassTutorialPage = 0;
    s_lastGlassTutorialMs = millis();
}

void showGlass2Tutorial(bool advance)
{
    if (advance) ++s_glassTutorialPage;
    const char* english = "TUNING CONTROLS";
    const char* japanese = "";
    uint8_t pageCount = 4;
    if (!s_keyEditor)
    {
        static const char* const pages[] = {
            "ボール上下：キー／LAYER選択",
            "LAYER上でB/ or A/C：レイヤー切替",
            "画面SYNC：設定を再読込",
            "右上タップ：メニューへ戻る"
        };
        japanese = pages[s_glassTutorialPage % 4];
    }
    else if (s_keyConfirm)
    {
        static const char* const pages[] = {
            "B：この設定を保存",
            "ボール／A／C：選び直す",
            "右上タップ：一段戻る"
        };
        pageCount = 3;
        japanese = pages[s_glassTutorialPage % pageCount];
        english = "KEY ASSIGN / CONFIRM";
    }
    else
    {
        static const char* const pages[] = {
            "ボール／A／C：項目を選択",
            "B：選択項目を決定",
            "右上タップ：一段戻る"
        };
        pageCount = 3;
        japanese = pages[s_glassTutorialPage % pageCount];
        english = s_selectingCategory ? "SELECT CATEGORY" : "SELECT KEY";
    }
    s_glassTutorialPage %= pageCount;
    core2_glass2_context("TUNING", english, japanese);
    s_lastGlassTutorialMs = millis();
}

void openKeyEditor(uint8_t row, const NapeTuningSnapshot& state)
{
    if (row >= kSelectableRows) return;
    s_keyRowSelection = row;
    s_editKey = row < 7 ? kKeyOrder[row] : (uint8_t)(7 + (row - 7));
    s_editChoice = 0;
    s_editCategory = 0;
    const uint16_t current = row < 7
        ? state.keymap[s_viewLayer][s_editKey]
        : state.wheel[s_viewLayer][row - 7];
    for (uint8_t category = 0; category < kCategoryCount; ++category)
    {
        uint8_t count = 0;
        const uint16_t* codes = editCodes(category, &count);
        for (uint8_t i = 0; i < count; ++i)
            if (codes[i] == current)
            {
                s_editCategory = category;
                s_editChoice = i;
            }
    }
    s_keyEditor = true;
    s_keyConfirm = false;
    s_selectingCategory = true;
    core2_glass2_context(
        "KEY ASSIGN", selectedRowLabel(row),
        "ボール／A／C：カテゴリ選択");
    s_dirty = true;
}

void appendModNames(char* out, size_t outSize, uint8_t mods, bool longNames)
{
    if (outSize == 0) return;
    out[0] = '\0';
    const bool right = (mods & 0x10) != 0;
    const uint8_t bits = mods & 0x0F;
    const char* const shortLeft[4] = {"C", "S", "A", "G"};
    const char* const shortRight[4] = {"RC", "RS", "RA", "RG"};
    const char* const longLeft[4] = {"CTRL", "SHIFT", "ALT", "GUI"};
    const char* const longRight[4] = {"RCTRL", "RSHIFT", "RALT", "RGUI"};
    for (uint8_t bit = 0; bit < 4; ++bit)
    {
        if ((bits & (1u << bit)) == 0) continue;
        if (out[0] != '\0') strlcat(out, "+", outSize);
        const char* name = longNames
            ? (right ? longRight[bit] : longLeft[bit])
            : (right ? shortRight[bit] : shortLeft[bit]);
        strlcat(out, name, outSize);
    }
}

bool visibleStateChanged(
    const NapeTuningSnapshot& before,
    const NapeTuningSnapshot& after
)
{
    if (before.connected != after.connected ||
        before.syncing != after.syncing ||
        before.runtime_valid != after.runtime_valid ||
        before.current_layer != after.current_layer ||
        before.angle_index != after.angle_index ||
        before.dpi_stage != after.dpi_stage ||
        before.dpi != after.dpi ||
        before.custom_dpi_valid != after.custom_dpi_valid ||
        before.custom_dpi != after.custom_dpi ||
        before.polling_rate_valid != after.polling_rate_valid ||
        before.polling_rate_hz != after.polling_rate_hz ||
        before.key_write_status != after.key_write_status ||
        before.dpi_cycle_valid_mask != after.dpi_cycle_valid_mask)
        return true;

    for (uint8_t stage = 0; stage < 5; ++stage)
        if (before.dpi_cycle[stage] != after.dpi_cycle[stage])
            return true;

    const uint8_t layerBit = (uint8_t)(1u << s_viewLayer);
    if ((before.keymap_valid_mask & layerBit) !=
        (after.keymap_valid_mask & layerBit))
        return true;

    if (after.keymap_valid_mask & layerBit)
    {
        for (uint8_t key = 0; key < 7; ++key)
            if (before.keymap[s_viewLayer][key] !=
                after.keymap[s_viewLayer][key])
                return true;
    }

    if (before.tap_hold_valid[s_viewLayer] !=
        after.tap_hold_valid[s_viewLayer])
        return true;
    for (uint8_t key = 0; key < 7; ++key)
    {
        if (before.tap_key[s_viewLayer][key] !=
                after.tap_key[s_viewLayer][key] ||
            before.hold_key[s_viewLayer][key] !=
                after.hold_key[s_viewLayer][key])
            return true;
    }

    if (before.wheel_valid[s_viewLayer] != after.wheel_valid[s_viewLayer])
        return true;
    for (uint8_t slot = 0; slot < 2; ++slot)
        if (before.wheel[s_viewLayer][slot] !=
            after.wheel[s_viewLayer][slot])
            return true;

    return false;
}

const char* keyName(uint16_t code, char* scratch, size_t scratchSize)
{
    // QMK shortcut: modifier bits in the high byte plus a basic keycode.
    if (code >= 0x0100 && code <= 0x1FFF)
    {
        char mods[28];
        char key[28];
        appendModNames(mods, sizeof(mods), (uint8_t)((code >> 8) & 0x1F), true);
        const char* keyText =
            keyName((uint8_t)(code & 0xFF), key, sizeof(key));
        snprintf(scratch, scratchSize, "%s+%s", mods, keyText);
        return scratch;
    }

    // QMK Mod-Tap: tap sends the low-byte key; hold acts as modifier(s).
    if (code >= 0x2000 && code <= 0x3FFF)
    {
        char mods[20];
        char tap[24];
        appendModNames(mods, sizeof(mods), (uint8_t)((code >> 8) & 0x1F), false);
        const char* tapText =
            keyName((uint8_t)(code & 0xFF), tap, sizeof(tap));
        snprintf(scratch, scratchSize, "T:%s H:%s", tapText, mods);
        return scratch;
    }

    // QMK Layer-Tap: tap sends the low-byte key; hold selects the layer.
    if (code >= 0x4000 && code <= 0x4FFF)
    {
        char tap[24];
        const char* tapText =
            keyName((uint8_t)(code & 0xFF), tap, sizeof(tap));
        snprintf(scratch, scratchSize, "T:%s H:L%u", tapText,
                 (unsigned)((code >> 8) & 0x0F));
        return scratch;
    }

    if (code >= 0x0004 && code <= 0x001D)
    {
        scratch[0] = (char)('A' + code - 0x0004);
        scratch[1] = '\0';
        return scratch;
    }
    if (code >= 0x001E && code <= 0x0026)
    {
        scratch[0] = (char)('1' + code - 0x001E);
        scratch[1] = '\0';
        return scratch;
    }
    if (code == 0x0027) return "0";
    if (code >= 0x003A && code <= 0x0045)
    {
        snprintf(scratch, scratchSize, "F%u", (unsigned)(code - 0x0039));
        return scratch;
    }
    if (code >= 0x5201 && code <= 0x5208)
    {
        snprintf(scratch, scratchSize, "TO(%u)", (unsigned)(code - 0x5201));
        return scratch;
    }
    if (code >= 0x5221 && code <= 0x5228)
    {
        snprintf(scratch, scratchSize, "MO(%u)", (unsigned)(code - 0x5221));
        return scratch;
    }
    if (code >= 0x5261 && code <= 0x5268)
    {
        snprintf(scratch, scratchSize, "TG(%u)", (unsigned)(code - 0x5261));
        return scratch;
    }
    if (code >= 0x7700 && code <= 0x770F)
    {
        snprintf(scratch, scratchSize, "M%u", (unsigned)(code - 0x7700));
        return scratch;
    }

    switch (code)
    {
        case 0x0000: return "NONE";
        case 0x0001: return "TRNS";
        case 0x0028: return "ENTER";
        case 0x0029: return "ESC";
        case 0x002A: return "BACKSPACE";
        case 0x002B: return "TAB";
        case 0x002C: return "SPACE";
        case 0x002D: return "-";
        case 0x002E: return "=";
        case 0x002F: return "[";
        case 0x0030: return "]";
        case 0x0031: return "BACKSLASH";
        case 0x0033: return ";";
        case 0x0034: return "QUOTE";
        case 0x0035: return "GRAVE";
        case 0x0036: return ",";
        case 0x0037: return ".";
        case 0x0038: return "/";
        case 0x0039: return "CAPS LOCK";
        case 0x0046: return "PRINT SCR";
        case 0x0047: return "SCROLL LOCK";
        case 0x0048: return "PAUSE";
        case 0x0049: return "INSERT";
        case 0x004A: return "HOME";
        case 0x004B: return "PAGE UP";
        case 0x004C: return "DELETE";
        case 0x004D: return "END";
        case 0x004E: return "PAGE DOWN";
        case 0x004F: return "RIGHT";
        case 0x0050: return "LEFT";
        case 0x0051: return "DOWN";
        case 0x0052: return "UP";
        case 0x0053: return "NUM LOCK";
        case 0x0054: return "KP /";
        case 0x0055: return "KP *";
        case 0x0056: return "KP -";
        case 0x0057: return "KP +";
        case 0x0058: return "KP ENTER";
        case 0x0059: return "KP 1";
        case 0x005A: return "KP 2";
        case 0x005B: return "KP 3";
        case 0x005C: return "KP 4";
        case 0x005D: return "KP 5";
        case 0x005E: return "KP 6";
        case 0x005F: return "KP 7";
        case 0x0060: return "KP 8";
        case 0x0061: return "KP 9";
        case 0x0062: return "KP 0";
        case 0x0063: return "KP .";
        case 0x00A8: return "MUTE";
        case 0x00A9: return "VOL+";
        case 0x00AA: return "VOL-";
        case 0x00AB: return "NEXT";
        case 0x00AC: return "PREVIOUS";
        case 0x00AD: return "MEDIA STOP";
        case 0x00AE: return "PLAY/PAUSE";
        case 0x00AF: return "MEDIA SELECT";
        case 0x00B0: return "EJECT";
        case 0x00BB: return "FAST FWD";
        case 0x00BC: return "REWIND";
        case 0x00C1: return "MISSION CTRL";
        case 0x00C2: return "LAUNCHPAD";
        case 0x00D1: return "MOUSE1";
        case 0x00D2: return "MOUSE2";
        case 0x00D3: return "MOUSE3";
        case 0x00D4: return "BACK";
        case 0x00D5: return "FORWARD";
        case 0x00D9: return "SCROLL+";
        case 0x00DA: return "SCROLL-";
        case 0x00DB: return "SCROLL LEFT";
        case 0x00DC: return "SCROLL RIGHT";
        case 0x00E0: return "LEFT CTRL";
        case 0x00E1: return "LEFT SHIFT";
        case 0x00E2: return "LEFT ALT";
        case 0x00E3: return "LEFT WIN";
        case 0x00E4: return "RIGHT CTRL";
        case 0x00E5: return "RIGHT SHIFT";
        case 0x00E6: return "RIGHT ALT";
        case 0x00E7: return "RIGHT WIN";
        case 0x7E25: return "NEXT DPI";
        case 0x7E26: return "PREV DPI";
        case 0x7E2B: return "8DIR";
        case 0x7E2C: return "CYCLE DPI";
        case 0x7E2D: return "POLL CYCLE";
        case 0x7E2E: return "DOUBLE CLICK";
        case 0x7E2F: return "CUSTOM DPI";
        case 0x7E30: return "LEFT CLICK LOCK";
        case 0x7E27: return "POLL NEXT";
        case 0x7E28: return "POLL PREV";
        case 0x5229: return "BALL GESTURE";
        case 0x522A: return "BALL SCROLL";
        case 0x522B: return "ROUND BTN";
        default:
            snprintf(scratch, scratchSize, "0x%04X", code);
            return scratch;
    }
}

const char* tapHoldName(uint16_t tap, uint16_t hold,
                        char* scratch, size_t scratchSize)
{
    char tapName[28];
    char holdName[28];
    const char* tapText = keyName(tap, tapName, sizeof(tapName));
    const char* holdText = keyName(hold, holdName, sizeof(holdName));
    snprintf(scratch, scratchSize, "T:%s H:%s", tapText, holdText);
    return scratch;
}

void requestActiveLayer(uint8_t layer)
{
    layer &= 0x07;
    s_viewLayer = layer;
    s_layerChangeTarget = layer;
    s_layerChangeStartedMs = millis();
    s_layerChangePending = true;
    s_layerSelectedByCore2 = true;
    nape_game_source_request_layer(layer);
    updateGlass2TuningDetail(layer);
    s_dirty = true;
}

void refreshKeepingActiveLayer()
{
    // Refresh only re-reads device state. A7 2D changes the active layer, but
    // a persistent NapePro-side default-layer write is not yet confirmed.
    nape_tuning_request_sync();
    s_dirty = true;
}

void drawTuning(const NapeTuningSnapshot& state)
{
    napetune_lcd_acquire();
    M5.Display.fillScreen(BLACK);

    M5.Display.fillRect(0, 0, 320, 28, 0x0186);
    M5.Display.fillRoundRect(5, 4, 72, 20, 3,
                             state.syncing ? 0x4208 : NAVY);
    M5.Display.drawRoundRect(5, 4, 72, 20, 3,
                             state.syncing ? YELLOW : CYAN);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(state.syncing ? YELLOW : WHITE,
                            state.syncing ? 0x4208 : NAVY);
    M5.Display.setCursor(state.syncing ? 13 : 17, 10);
    M5.Display.print(state.syncing ? "SYNC... >" : "SYNC >");
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(CYAN, 0x0186);
    M5.Display.setCursor(124, 6);
    M5.Display.print("TUNING");
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(WHITE, 0x0186);
    M5.Display.setCursor(270, 10);
    M5.Display.print("< BACK");

    M5.Display.setTextColor(state.connected ? GREEN : RED, BLACK);
    M5.Display.setCursor(10, 34);
    M5.Display.print(state.connected ? "NAPE // ONLINE" : "NAPE // OFFLINE");

    M5.Display.fillRect(8, 47, 304, 24, NAVY);
    M5.Display.drawRect(8, 47, 304, 24,
                        s_keyRowSelection == kLayerSelection ? YELLOW : CYAN);
    if (s_keyRowSelection == kLayerSelection)
        M5.Display.drawRect(10, 49, 300, 20, YELLOW);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(CYAN, NAVY);
    M5.Display.setCursor(17, 51);
    M5.Display.printf("<  LAYER %02u  >", s_viewLayer);
    if (state.runtime_valid && state.current_layer == s_viewLayer)
    {
        M5.Display.setTextSize(1);
        M5.Display.setTextColor(GREEN, NAVY);
        M5.Display.setCursor(261, 55);
        M5.Display.print("ACTIVE");
    }

    if (s_keyEditor)
    {
        const bool wheelEdit = s_editKey >= 7;
        const uint8_t wheelSlot = wheelEdit ? s_editKey - 7 : 0;
        const bool valid = wheelEdit
            ? (state.wheel_valid[s_viewLayer] & (1u << wheelSlot)) != 0
            : (state.keymap_valid_mask & (1u << s_viewLayer)) != 0;
        char currentScratch[40];
        char newScratch[40];
        M5.Display.setTextSize(2);
        M5.Display.setTextColor(YELLOW, BLACK);
        M5.Display.setCursor(14, 84);
        M5.Display.printf("L%u / %s", s_viewLayer,
                          wheelEdit ? (wheelSlot == 0 ? "WHEEL+" : "WHEEL-") :
                          kKeyLabels[s_editKey == 4 ? 0 :
                          s_editKey == 5 ? 1 : s_editKey == 2 ? 2 :
                          s_editKey == 3 ? 3 : s_editKey == 0 ? 4 :
                          s_editKey == 1 ? 5 : 6]);
        M5.Display.setTextSize(1);
        M5.Display.setTextColor(CYAN, BLACK);
        M5.Display.setCursor(18, 118);
        M5.Display.print("CURRENT");
        M5.Display.setTextColor(WHITE, BLACK);
        M5.Display.setCursor(90, 118);
        const uint16_t currentCode = wheelEdit
            ? state.wheel[s_viewLayer][wheelSlot]
            : state.keymap[s_viewLayer][s_editKey];
        M5.Display.print(valid
            ? keyName(currentCode, currentScratch,
                      sizeof(currentScratch)) : "----");
        M5.Display.setTextColor(CYAN, BLACK);
        M5.Display.setCursor(18, 145);
        M5.Display.print(s_selectingCategory ? "CATEGORY" : "NEW");
        M5.Display.setTextSize(2);
        M5.Display.setTextColor(s_keyConfirm ? YELLOW : GREEN, BLACK);
        M5.Display.setCursor(90, 140);
        if (s_selectingCategory)
            M5.Display.print(kCategoryNames[s_editCategory]);
        else
            M5.Display.print(keyName(selectedEditCode(), newScratch,
                                    sizeof(newScratch)));
        M5.Display.setTextSize(1);
        M5.Display.setTextColor(WHITE, BLACK);
        M5.Display.setCursor(18, 181);
        if (state.key_write_status == 1) M5.Display.print("WRITING...");
        else if (state.key_write_status == 2) M5.Display.print("VERIFYING...");
        else if (state.key_write_status == 3)
        { M5.Display.setTextColor(GREEN, BLACK); M5.Display.print("SAVED / VERIFIED"); }
        else if (state.key_write_status == 4)
        { M5.Display.setTextColor(RED, BLACK); M5.Display.print("FAILED / NOT CHANGED"); }
        else if (s_selectingCategory) M5.Display.print("SELECT CATEGORY");
        else M5.Display.print(s_keyConfirm ? "PRESS B AGAIN TO APPLY" : "SELECT ASSIGNMENT");
        M5.Display.setTextColor(WHITE, BLACK);
        M5.Display.setCursor(10, 231);
        M5.Display.print("BALL/A/C:MOVE B:SELECT  TOP-R:BACK");
        napetune_lcd_release();
        return;
    }

    const bool valid = (state.keymap_valid_mask & (1u << s_viewLayer)) != 0;
    M5.Display.setTextSize(1);
    for (uint8_t row = 0; row < kSelectableRows; ++row)
    {
        const bool wheelRow = row >= 7;
        const uint8_t key = wheelRow ? 0 : kKeyOrder[row];
        const int x = 10;
        const int y = 78 + row * 16;
        if (row == s_keyRowSelection)
        {
            M5.Display.fillRoundRect(6, y - 3, 158, 16, 3, 0x0186);
            M5.Display.drawRoundRect(6, y - 3, 158, 16, 3, CYAN);
        }
        M5.Display.setTextColor(YELLOW, BLACK);
        if (row == s_keyRowSelection)
            M5.Display.setTextColor(YELLOW, 0x0186);
        M5.Display.setCursor(x, y);
        M5.Display.print(selectedRowLabel(row));
        M5.Display.setTextColor(WHITE,
            row == s_keyRowSelection ? 0x0186 : BLACK);
        M5.Display.setCursor(x + 38, y);
        char scratch[40];
        const bool tapHoldValid = !wheelRow &&
            (state.tap_hold_valid[s_viewLayer] & (1u << key)) != 0 &&
            (state.tap_key[s_viewLayer][key] != 0 ||
             state.hold_key[s_viewLayer][key] != 0);
        if (tapHoldValid)
            M5.Display.print(tapHoldName(
                state.tap_key[s_viewLayer][key],
                state.hold_key[s_viewLayer][key],
                scratch, sizeof(scratch)));
        else if (wheelRow)
        {
            const uint8_t slot = row - 7;
            M5.Display.print((state.wheel_valid[s_viewLayer] & (1u << slot))
                ? keyName(state.wheel[s_viewLayer][slot], scratch, sizeof(scratch))
                : "----");
        }
        else M5.Display.print(valid
            ? keyName(state.keymap[s_viewLayer][key], scratch, sizeof(scratch))
            : "----");
        M5.Display.drawFastHLine(x, y + 11, 130, DARKGREY);
    }

    // Restore the cockpit-style ANGLE HUD from the original Tuning app.
    const int cx = 238, cy = 118, r = 28;
    static const uint16_t dpiColors[5] = {
        M5.Display.color565(55, 95, 210),
        M5.Display.color565(30, 175, 220),
        M5.Display.color565(55, 205, 115),
        M5.Display.color565(245, 175, 45),
        M5.Display.color565(235, 70, 60)
    };
    M5.Display.setTextColor(CYAN, BLACK);
    M5.Display.setCursor(199, 77);
    M5.Display.print("ANGLE");
    M5.Display.drawCircle(cx, cy, r, DARKGREY);
    M5.Display.drawCircle(cx, cy, r - 8, DARKGREY);
    M5.Display.drawLine(cx - 10, cy, cx - 3, cy, DARKGREY);
    M5.Display.drawLine(cx + 3, cy, cx + 10, cy, DARKGREY);
    M5.Display.drawLine(cx, cy - 10, cx, cy - 3, DARKGREY);
    M5.Display.drawLine(cx, cy + 3, cx, cy + 10, DARKGREY);

    // Dual segmented rails preserve the clean ANGLE reticle: DPI occupies
    // the left arc (five steps), polling rate the right arc (four steps).
    // Only the selected segment glows, like a compact cockpit instrument.
    for (uint8_t i = 0; i < 5; ++i)
    {
        const int start = 198 + i * 18;
        const uint16_t color = state.dpi_stage == i
            ? dpiColors[i] : M5.Display.color565(28, 42, 55);
        M5.Display.drawArc(cx, cy, r + 6, r + 3,
                           start, start + 12, color);
    }
    uint16_t pollColor = DARKGREY;
    uint8_t pollStage = 0xFF;
    if (state.polling_rate_valid)
    {
        if (state.polling_rate_hz <= 125)
        { pollStage = 0; pollColor = M5.Display.color565(125, 80, 220); }
        else if (state.polling_rate_hz <= 250)
        { pollStage = 1; pollColor = M5.Display.color565(40, 155, 235); }
        else if (state.polling_rate_hz <= 500)
        { pollStage = 2; pollColor = M5.Display.color565(60, 220, 145); }
        else
        { pollStage = 3; pollColor = M5.Display.color565(255, 120, 35); }
    }
    for (uint8_t i = 0; i < 4; ++i)
    {
        const int start = 18 + i * 22;
        const uint16_t color = pollStage == i
            ? pollColor : M5.Display.color565(28, 42, 55);
        M5.Display.drawArc(cx, cy, r + 6, r + 3,
                           start, start + 15, color);
    }
    if (state.runtime_valid)
    {
        const int angle = (int)state.angle_index * 45;
        const float rad = ((float)angle - 90.0f) * DEG_TO_RAD;
        const float ux = cosf(rad), uy = sinf(rad);
        const float vx = -uy, vy = ux;
        const float halfLen = 18.0f, halfWid = 6.0f;
        const int x1 = cx + (int)( ux*halfLen + vx*halfWid);
        const int y1 = cy + (int)( uy*halfLen + vy*halfWid);
        const int x2 = cx + (int)( ux*halfLen - vx*halfWid);
        const int y2 = cy + (int)( uy*halfLen - vy*halfWid);
        const int x3 = cx + (int)(-ux*halfLen - vx*halfWid);
        const int y3 = cy + (int)(-uy*halfLen - vy*halfWid);
        const int x4 = cx + (int)(-ux*halfLen + vx*halfWid);
        const int y4 = cy + (int)(-uy*halfLen + vy*halfWid);
        M5.Display.drawLine(x1,y1,x2,y2,GREEN);
        M5.Display.drawLine(x2,y2,x3,y3,GREEN);
        M5.Display.drawLine(x3,y3,x4,y4,GREEN);
        M5.Display.drawLine(x4,y4,x1,y1,GREEN);
        const float mr = (float)r + 10.0f;
        const float mx = (float)cx + ux * mr, my = (float)cy + uy * mr;
        const int tx = (int)(mx - ux * 5.0f), ty = (int)(my - uy * 5.0f);
        const int bx = (int)(mx + ux * 3.0f), by = (int)(my + uy * 3.0f);
        M5.Display.fillTriangle(tx, ty,
            bx + (int)(vx * 5.0f), by + (int)(vy * 5.0f),
            bx - (int)(vx * 5.0f), by - (int)(vy * 5.0f), GREEN);
        M5.Display.setTextSize(2);
        M5.Display.setTextColor(GREEN, BLACK);
        M5.Display.setCursor(194, 160);
        M5.Display.printf("%3d deg", angle);
    }
    else
    {
        M5.Display.setTextSize(2);
        M5.Display.setTextColor(DARKGREY, BLACK);
        M5.Display.setCursor(215, 160);
        M5.Display.print("---");
    }

    M5.Display.setTextSize(1);
    M5.Display.setTextColor(WHITE, BLACK);
    M5.Display.setCursor(174, 184);
    M5.Display.print("DPI ");
    if (state.dpi) M5.Display.print(state.dpi);
    else M5.Display.print("----");

    M5.Display.setTextColor(WHITE, BLACK);
    M5.Display.setCursor(174, 197);
    M5.Display.print("POLL ");
    if (state.polling_rate_valid)
        M5.Display.printf("%uHz", state.polling_rate_hz);
    else
        M5.Display.print("FIXED/--");

    // Keep CUSTOM DPI below the gauge and clear of the angle readout.
    M5.Display.setTextColor(CYAN, BLACK);
    M5.Display.setCursor(174, 212);
    M5.Display.print("CUSTOM DPI ");
    M5.Display.setTextColor(WHITE, BLACK);
    if (state.custom_dpi_valid) M5.Display.print(state.custom_dpi);
    else M5.Display.print("----");
    M5.Display.setCursor(10, 231);
    M5.Display.print("BALL:KEY A/C:LAYER B:EDIT TAP:SYNC");
    napetune_lcd_release();
}
}

void tuning_mode_begin()
{
    s_viewLayer = 0;
    s_layerChangePending = false;
    s_layerSelectedByCore2 = false;
    s_keyEditor = false;
    s_keyConfirm = false;
    s_selectingCategory = true;
    s_keyRowSelection = 0;
    s_lastBallNavMs = 0;
    s_dirty = true;
    s_touchWasDown = M5.Touch.getCount() > 0;
    s_touchTracking = false;
    game_input_clear_nape();
    nape_tuning_request_sync();
    nape_tuning_get_snapshot(&s_last);
    if (s_last.runtime_valid) s_viewLayer = s_last.current_layer & 0x07;
    drawTuning(s_last);
    updateGlass2TuningDetail(s_viewLayer);
    Serial.println("[TUNING] read-only mode ready");
}

bool tuning_mode_update()
{
    const uint32_t now = millis();
    GameInputState input = {};
    game_input_poll(&input);

    NapeTuningSnapshot state = {};
    nape_tuning_get_snapshot(&state);
    const bool wasConnected = s_last.connected;
    const uint8_t previousWriteStatus = s_last.key_write_status;
    const bool visibleChanged = visibleStateChanged(s_last, state);
    if (memcmp(&state, &s_last, sizeof(state)) != 0)
    {
        s_last = state;
        if (visibleChanged) s_dirty = true;
    }
    if (state.key_write_status != previousWriteStatus)
    {
        if (state.key_write_status == 3)
            core2_glass2_message("KEY ASSIGN // SAVED",
                                 "書込みと読み戻し検証に成功しました");
        else if (state.key_write_status == 4)
            core2_glass2_message("KEY ASSIGN // FAILED",
                                 "一致しません。設定は確定していません");
    }

    if (wasConnected && !state.connected)
    {
        s_layerChangePending = false;
    }


    // Keep the page synchronized with changes made on NapePro itself. While
    // a Core2-originated request is awaiting confirmation, retain its target
    // page so the previous runtime value does not make the UI jump backward.
    if (s_layerChangePending)
    {
        if (state.runtime_valid &&
            state.current_layer == s_layerChangeTarget)
        {
            s_layerChangePending = false;
            s_viewLayer = state.current_layer & 0x07;
            s_dirty = true;
        }
        else if (now - s_layerChangeStartedMs >= 4500)
        {
            s_layerChangePending = false;
            // Keep the user's selected page. A missing USB acknowledgement
            // must not visibly undo a Core2-originated layer selection.
            s_dirty = true;
        }
    }
    else if (!s_layerSelectedByCore2 && state.runtime_valid &&
             s_viewLayer != (state.current_layer & 0x07))
    {
        s_viewLayer = state.current_layer & 0x07;
        s_dirty = true;
    }

    // GLASS2 is only 128 px wide. Rotate short, complete hints instead of
    // squeezing the whole control scheme into one clipped paragraph.
    if ((int32_t)(now - s_glassTutorialHoldUntilMs) >= 0 &&
        now - s_lastGlassTutorialMs >= 4200)
        showGlass2Tutorial(true);

    // The ball is safe navigation input because it has no assignable keycode.
    // Assignable NapePro buttons (including ROUND) are not UI commands here.
    const bool ballReady = now - s_lastBallNavMs >= 170;
    const bool ballVertical = abs((int)input.aim_y) >= 8 &&
        abs((int)input.aim_y) >= abs((int)input.aim_x);
    if (ballReady && ballVertical)
    {
        const int direction = input.aim_y < 0 ? -1 : 1;
        s_lastBallNavMs = now;
        if (s_selectingCategory)
        {
            if (s_keyEditor)
                s_editCategory = (uint8_t)((s_editCategory + kCategoryCount +
                                            direction) % kCategoryCount);
            else
            {
                if (direction < 0)
                    s_keyRowSelection = s_keyRowSelection == 0
                        ? kLayerSelection
                        : (uint8_t)(s_keyRowSelection - 1);
                else
                    s_keyRowSelection = s_keyRowSelection == kLayerSelection
                        ? 0
                        : (uint8_t)((s_keyRowSelection + 1) % kSelectableRows);
            }
        }
        else if (s_keyEditor)
        {
            uint8_t count = 0;
            editCodes(s_editCategory, &count);
            s_editChoice = (uint8_t)((s_editChoice + count + direction) % count);
        }
        s_keyConfirm = false;
        s_dirty = true;
    }
    else if (M5.BtnA.wasPressed())
    {
        if (!s_keyEditor)
            requestActiveLayer((uint8_t)((s_viewLayer + 7) & 7));
        else if (s_selectingCategory)
        {
            s_editCategory = (uint8_t)((s_editCategory +
                                        kCategoryCount - 1) % kCategoryCount);
        }
        else
        {
            uint8_t count = 0;
            editCodes(s_editCategory, &count);
            s_editChoice = (uint8_t)((s_editChoice + count - 1) % count);
        }
        if (s_keyEditor)
        {
            s_keyConfirm = false;
            s_glassTutorialPage = 0;
            showGlass2Tutorial(false);
        }
        s_dirty = true;
    }
    else if (M5.BtnC.wasPressed())
    {
        if (!s_keyEditor)
            requestActiveLayer((uint8_t)((s_viewLayer + 1) & 7));
        else if (s_selectingCategory)
            s_editCategory = (uint8_t)((s_editCategory + 1) % kCategoryCount);
        else
        {
            uint8_t count = 0;
            editCodes(s_editCategory, &count);
            s_editChoice = (uint8_t)((s_editChoice + 1) % count);
        }
        if (s_keyEditor)
        {
            s_keyConfirm = false;
            s_glassTutorialPage = 0;
            showGlass2Tutorial(false);
        }
        s_dirty = true;
    }
    // Tuning decisions must not depend on the assignment of NapePro's M1.
    // Core2 B remains available on every layer and is the sole confirm key.
    const bool selectPressed = M5.BtnB.wasPressed();
    if (s_keyEditor && selectPressed)
    {
        if (s_selectingCategory)
        {
            s_selectingCategory = false;
            s_editChoice = 0;
            s_keyConfirm = false;
            s_dirty = true;
            s_glassTutorialPage = 0;
            showGlass2Tutorial(false);
        }
        else if (!s_keyConfirm)
        {
            s_keyConfirm = true;
            s_dirty = true;
            s_glassTutorialPage = 0;
            showGlass2Tutorial(false);
        }
        else if ((s_editKey >= 7
                    ? nape_tuning_write_wheel(
                        s_viewLayer, s_editKey - 7, selectedEditCode())
                    : nape_tuning_write_key(
                        s_viewLayer, s_editKey, selectedEditCode())))
        {
            s_keyConfirm = false;
            s_dirty = true;
            core2_glass2_message("KEY ASSIGN", "書込み後に読み戻して検証します");
        }
    }
    else if (!s_keyEditor && selectPressed)
    {
        if (s_keyRowSelection == kLayerSelection)
        {
            requestActiveLayer((uint8_t)((s_viewLayer + 1) & 7));
            core2_glass2_message("TUNING // LAYER", "次のレイヤーへ切り替えます");
        }
        else openKeyEditor(s_keyRowSelection, state);
    }

    bool touchDown = false;
    int touchX = s_touchLastX;
    int touchY = s_touchLastY;
    if (M5.Touch.getCount() > 0)
    {
        auto touch = M5.Touch.getDetail(0);
        touchDown = touch.isPressed();
        touchX = touch.x;
        touchY = touch.y;
    }
    if (touchDown)
    {
        if (!s_touchWasDown)
        {
            s_touchTracking = true;
            s_touchStartX = touchX;
            s_touchStartY = touchY;
        }
        if (s_touchTracking)
        {
            s_touchLastX = touchX;
            s_touchLastY = touchY;
        }
    }
    else if (s_touchWasDown && s_touchTracking)
    {
        s_touchTracking = false;
        const int dx = s_touchLastX - s_touchStartX;
        const int dy = s_touchLastY - s_touchStartY;
        if (dx >= 55 && abs(dx) > abs(dy))
        {
            core2_glass2_message("GAMING MODE", "ゲームメニューへ戻る");
            return true;
        }
        if (abs(dx) < 30 && abs(dy) < 30)
        {
            if (s_touchLastY < 28 && s_touchLastX >= 240)
            {
                if (s_keyEditor)
                {
                    if (!s_selectingCategory)
                    {
                        s_selectingCategory = true;
                        s_keyConfirm = false;
                        core2_glass2_context(
                            "KEY ASSIGN / CATEGORY", "カテゴリを選択",
                            "ボール／A／C：項目選択");
                    }
                    else
                    {
                        s_keyEditor = false;
                        s_keyConfirm = false;
                        updateGlass2TuningDetail(s_viewLayer);
                    }
                    s_dirty = true;
                }
                else return true;
            }
            else if (!s_keyEditor && s_touchLastX >= 0 &&
                     s_touchLastX < 90 && s_touchLastY < 28)
            {
                refreshKeepingActiveLayer();
                core2_glass2_paged_message(
                    "TUNING // SYNC",
                    "NapeProの設定を再取得しています。"
                    "不具合の場合は、NapeProのスイッチをBT or USBに"
                    "切り替えてから戻し、再度、画面左上のSYNCを"
                    "タップしてください");
                s_lastGlassTutorialMs = millis();
                // Six short GLASS2 pages need about 24 seconds. Keep the
                // rotating tutorial from replacing them before the final page.
                s_glassTutorialHoldUntilMs = millis() + 30000;
            }
            else if (!s_keyEditor && s_touchLastY >= 47 &&
                     s_touchLastY < 72)
            {
                s_keyRowSelection = kLayerSelection;
                if (s_touchLastX < 106)
                    requestActiveLayer((uint8_t)((s_viewLayer + 7) & 7));
                else if (s_touchLastX > 214)
                    requestActiveLayer((uint8_t)((s_viewLayer + 1) & 7));
                else requestActiveLayer(s_viewLayer);
                s_dirty = true;
            }
            else if (!s_keyEditor && s_touchLastX < 165 &&
                     s_touchLastY >= 74 && s_touchLastY < 224)
            {
                const uint8_t row =
                    (uint8_t)((s_touchLastY - 74) / 16);
                if (row < kSelectableRows)
                    openKeyEditor(row, state);
            }
        }
    }
    s_touchWasDown = touchDown;

    if (s_dirty)
    {
        drawTuning(state);
        s_dirty = false;
    }
    return false;
}

