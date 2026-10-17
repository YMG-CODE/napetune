// Software logic: MIT. Embedded creative content: see CONTENT_LICENSE.md and CONTENT_MANIFEST.json.
#include <Arduino.h>
#include <M5Unified.h>

#include "game_input.h"
#include "core2_bridge.h"
#include "nape_game_source.h"
#include "spi_guard.h"
#include "launcher/game_launcher.h"

namespace
{
constexpr uint16_t COLOR_TUNING = 0x981F;
constexpr uint16_t COLOR_PANEL = 0x0861;
constexpr int SWIPE_THRESHOLD = 55;

// 0/1: GAMING/TUNING tabs, 2..4: STORY, HANGAR, LOUNGE cards.
uint8_t s_selectedItem = 2;
bool s_launcherDirty = true;
uint32_t s_lastNapeNavMs = 0;
uint32_t s_lastBatteryDrawMs = 0;
bool s_touchTracking = false;
bool s_ignoreTouchGesture = false;
int s_touchStartX = 0;
int s_touchStartY = 0;
int s_touchLastX = 0;
int s_touchLastY = 0;

void drawCentered(const char* text, int x, int y, uint16_t color, uint8_t size = 1)
{
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextSize(size);
    M5.Display.setTextColor(color);
    M5.Display.drawString(text, x, y);
    M5.Display.setTextDatum(top_left);
}

void drawTab(int x, int width, const char* label, bool selected,
             uint16_t accent, uint16_t selectedBackground)
{
    if (selected)
    {
        M5.Display.fillRect(x, 34, width, 29, selectedBackground);
        M5.Display.drawRect(x, 34, width, 29, accent);
        M5.Display.fillRect(x + 8, 60, width - 16, 3, accent);
    }
    else M5.Display.drawRect(x, 34, width, 29, DARKGREY);
    drawCentered(label, x + width / 2, 48, selected ? accent : LIGHTGREY);
}

void drawGamingCard(int x, int y, int width, int height,
                    const char* title, const char* subtitle, bool selected)
{
    const uint16_t background = selected ? NAVY : COLOR_PANEL;
    const uint16_t border = selected ? CYAN : DARKGREY;
    M5.Display.fillRect(x, y, width, height, background);
    M5.Display.drawRect(x, y, width, height, border);
    if (selected) M5.Display.fillRect(x, y, 5, height, CYAN);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(selected ? CYAN : WHITE, background);
    M5.Display.setCursor(x + 12, y + 9);
    M5.Display.print(title);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(selected ? WHITE : LIGHTGREY, background);
    M5.Display.setCursor(x + 12, y + height - 16);
    M5.Display.print(subtitle);
}

void drawGamingPage()
{
    drawGamingCard(10, 70, 300, 58, "STORY", "EXHIBITION", s_selectedItem == 2);
    drawGamingCard(10, 136, 146, 58, "HANGAR", "LOADOUT", s_selectedItem == 3);
    drawGamingCard(164, 136, 146, 58, "LOUNGE", "IZ ROOM", s_selectedItem == 4);
    const bool psramReady = ESP.getPsramSize() != 0;
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(psramReady ? DARKGREY : YELLOW, BLACK);
    M5.Display.setCursor(14, 198);
    M5.Display.print(psramReady ? "MISSION CONTROL // READY" :
                                  "PSRAM ERROR - SHOOT DISABLED");
}

LauncherSelection selectedGamingMode()
{
    if (s_selectedItem == 1) return LauncherSelection::TUNING_MODE;
    if (s_selectedItem == 0) { s_selectedItem = 2; return LauncherSelection::NONE; }
    if (s_selectedItem == 2) return LauncherSelection::MECHA_PROTOCOL;
    if (s_selectedItem == 3) return LauncherSelection::HANGAR;
    return LauncherSelection::IZ_LOUNGE;
}

void updateGlass2MenuDetail()
{
    switch (s_selectedItem)
    {
        case 0:
            core2_glass2_context("GAMING", "GAME MENU", "M1：ゲーム項目へ移動");
            break;
        case 1:
            core2_glass2_context("TUNING", "KEY ASSIGN", "M1：チューニングを開く");
            break;
        case 2:
            core2_glass2_context("GAMING", "STORY / EXHIBITION", "展示会ストーリーを開始します");
            break;
        case 3:
            core2_glass2_context("GAMING", "HANGAR / LOADOUT", "機体装備を変更します");
            break;
        default:
            core2_glass2_context("GAMING", "IZ / LOUNGE", "イズと会話します");
            break;
    }
}

LauncherSelection handleTap(int x, int y)
{
    if (y >= 32 && y <= 66)
    {
        if (x >= 140 && x <= 252)
        {
            core2_glass2_message("TUNING / KEY ASSIGN", "キー割り当てを編集");
            return LauncherSelection::TUNING_MODE;
        }
        return LauncherSelection::NONE;
    }
    if (y >= 70 && y < 130 && x >= 10 && x <= 310)
    {
        s_selectedItem = 2;
        return LauncherSelection::MECHA_PROTOCOL;
    }
    if (y >= 134 && y <= 196)
    {
        if (x >= 10 && x < 160)
        {
            s_selectedItem = 3;
            return LauncherSelection::HANGAR;
        }
        if (x >= 160 && x <= 310)
        {
            s_selectedItem = 4;
            return LauncherSelection::IZ_LOUNGE;
        }
    }
    return LauncherSelection::NONE;
}
}

void launcher_draw()
{
    napetune_lcd_acquire();
    M5.Display.fillScreen(BLACK);
    M5.Display.setTextColor(CYAN, BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(10, 7);
    M5.Display.print("NAPETUNE");
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(DARKGREY, BLACK);
    M5.Display.setCursor(116, 13);
    M5.Display.print("DUAL SYSTEM");
    drawTab(8, 124, "GAMING", s_selectedItem == 0, CYAN, NAVY);
    drawTab(140, 112, "TUNING >", s_selectedItem == 1,
            COLOR_TUNING, 0x3008);
    drawGamingPage();
    M5.Display.drawFastHLine(8, 213, 304, DARKGREY);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(WHITE, BLACK);
    M5.Display.setCursor(14, 218);
    M5.Display.print("BALL:MOVE  M1/B:SELECT  TOUCH:OK");
    core2_draw_battery_indicator();
    napetune_lcd_release();
    s_lastBatteryDrawMs = millis();
    s_launcherDirty = false;
}

void launcher_begin()
{
    // KV/QR pages may use their own brightness. Always restore the normal
    // launcher level before drawing the menu.
    M5.Display.setBrightness(128);
    s_launcherDirty = true;
    s_lastNapeNavMs = 0;
    s_touchTracking = false;
    s_ignoreTouchGesture = false;
    game_input_clear_nape();
    launcher_draw();
    updateGlass2MenuDetail();
    Serial.println("[LAUNCHER] ready page=GAMING");
}

LauncherSelection launcher_update()
{
    GameInputState input = {};
    game_input_poll(&input);
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
    bool touchGestureHandled = false;
    if (touchDown)
    {
        if (!s_touchTracking)
        {
            s_touchTracking = true;
            s_touchStartX = touchX;
            s_touchStartY = touchY;
            s_ignoreTouchGesture = touchX >= 255 && touchY < 50;
        }
        s_touchLastX = touchX;
        s_touchLastY = touchY;
    }
    else if (s_touchTracking)
    {
        s_touchTracking = false;
        touchGestureHandled = true;
        if (!s_ignoreTouchGesture)
        {
            const int dx = s_touchLastX - s_touchStartX;
            const int dy = s_touchLastY - s_touchStartY;
            if (abs(dx) >= SWIPE_THRESHOLD && abs(dx) > abs(dy))
            {
                if (dx > 0)
                {
                    core2_glass2_context(
                        "TERMINALIS", "KEY VISUAL / PROLOGUE",
                        "KVとプロローグQRを表示します");
                    return LauncherSelection::SHOWCASE;
                }
                core2_glass2_message(
                    "TUNING / KEY ASSIGN", "キー割り当てを編集");
                return LauncherSelection::TUNING_MODE;
            }
            else
            {
                const LauncherSelection tapped = handleTap(s_touchLastX, s_touchLastY);
                if (tapped != LauncherSelection::NONE) return tapped;
            }
        }
        s_ignoreTouchGesture = false;
    }

    const uint32_t now = millis();
    const bool digitalNavigation = (input.pressed &
        (GAME_INPUT_TURN_LEFT | GAME_INPUT_TURN_RIGHT |
         GAME_INPUT_FORWARD | GAME_INPUT_BACKWARD)) != 0;
    const bool ballNavigation =
        (abs((int)input.aim_x) >= 8 || abs((int)input.aim_y) >= 8 ||
         input.wheel != 0) && now - s_lastNapeNavMs >= 180;
    if (digitalNavigation || ballNavigation)
    {
        int dx = 0, dy = 0;
        if (digitalNavigation)
        {
            dx = (input.pressed & GAME_INPUT_TURN_LEFT) ? -1 :
                 ((input.pressed & GAME_INPUT_TURN_RIGHT) ? 1 : 0);
            dy = (input.pressed & GAME_INPUT_FORWARD) ? -1 :
                 ((input.pressed & GAME_INPUT_BACKWARD) ? 1 : 0);
        }
        else if (abs((int)input.aim_x) > abs((int)input.aim_y))
            dx = input.aim_x < 0 ? -1 : 1;
        else if (abs((int)input.aim_y) >= 8)
            dy = input.aim_y < 0 ? -1 : 1;
        else dy = input.wheel < 0 ? -1 : 1;

        // Spatial navigation across tabs, the wide STORY card, and two lower cards.
        static const uint8_t left[5]  = {0,0,2,3,3};
        static const uint8_t right[5] = {1,1,2,4,4};
        static const uint8_t up[5]    = {0,1,0,2,2};
        static const uint8_t down[5]  = {2,2,3,3,4};
        if (dx < 0) s_selectedItem = left[s_selectedItem];
        else if (dx > 0) s_selectedItem = right[s_selectedItem];
        else if (dy < 0) s_selectedItem = up[s_selectedItem];
        else if (dy > 0) s_selectedItem = down[s_selectedItem];
        s_launcherDirty = true;
        s_lastNapeNavMs = now;
        updateGlass2MenuDetail();
    }
    if (s_launcherDirty) launcher_draw();
    else if (now - s_lastBatteryDrawMs >= 5000)
    {
        napetune_lcd_acquire();
        core2_draw_battery_indicator();
        napetune_lcd_release();
        s_lastBatteryDrawMs = now;
    }
    if (!touchDown && !touchGestureHandled &&
        (input.pressed & (GAME_INPUT_ENTER | GAME_INPUT_FIRE | GAME_INPUT_USE)))
    {
        const LauncherSelection selected = selectedGamingMode();
        if (selected == LauncherSelection::NONE)
        {
            s_launcherDirty = true;
            updateGlass2MenuDetail();
        }
        return selected;
    }
    return LauncherSelection::NONE;
}
