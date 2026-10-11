// Software logic: MIT. Embedded creative content: see CONTENT_LICENSE.md and CONTENT_MANIFEST.json.
#include <Arduino.h>
#include <M5Unified.h>
#include <SD.h>
#include <new>

#include "game_input.h"
#include "core2_bridge.h"
#include "spi_guard.h"
#include "modes/mode_rooms.h"
#include "progression/game_progress.h"

extern const uint8_t iz_sleep_bed_start[] asm("_binary_src_assets_iz_sleep_bed_132x88_png_start");
extern const uint8_t iz_sleep_bed_end[] asm("_binary_src_assets_iz_sleep_bed_132x88_png_end");
extern const uint8_t iz_sleep_portrait_start[] asm("_binary_src_assets_iz_sleep_portrait_98x128_png_start");
extern const uint8_t iz_sleep_portrait_end[] asm("_binary_src_assets_iz_sleep_portrait_98x128_png_end");
extern const uint8_t hangar_mecha_front_v7_start[] asm("_binary_src_assets_hangar_mecha_front_v7_184x132_png_start");
extern const uint8_t hangar_mecha_front_v7_end[] asm("_binary_src_assets_hangar_mecha_front_v7_184x132_png_end");
extern const uint8_t hangar_mecha_rear_v7_start[] asm("_binary_src_assets_hangar_mecha_rear_v7_184x132_png_start");
extern const uint8_t hangar_mecha_rear_v7_end[] asm("_binary_src_assets_hangar_mecha_rear_v7_184x132_png_end");

namespace
{
struct RoomState
{
    uint8_t flags;
    GameLoadout loadout;
    bool hangar;
    bool touchWasDown;
    bool saveFailed;
    bool previewRear;
    uint8_t category;
    uint8_t weaponSlot;
    uint8_t hangarFocus;
    bool hangarNavLatched;
    uint32_t hangarNeutralSinceMs;
    uint8_t walkFrame;
    uint32_t lastWalkMs;
    uint32_t lastNavMs;
    int16_t izX, izY;
    int16_t izTargetX, izTargetY;
    int8_t izDirection;
    uint32_t nextWalkDecisionMs;
    int16_t lastClockMinute;
    int16_t cursorX, cursorY;
    bool cursorTouchingIz;
    uint8_t reactionFrame;
    uint32_t nextReactionMs;
    uint32_t reactionPulseStartMs;
    bool lastDialogueActive;
    uint8_t lastDialogueExpression;
    bool wakeForEvent01;
    uint32_t sleepMessageUntilMs;
};

RoomState* s_room = nullptr;
M5Canvas* s_loungeCanvas = nullptr;
M5Canvas* s_izSdFrames[8] = {};
bool s_izSdFramesReady = false;
M5Canvas* s_izPortraitFrames[10] = {};
bool s_izPortraitFramesReady = false;
void drawButton(int x, const char* label, bool accented);

void ensureRtcClock()
{
    const m5::rtc_date_t current = M5.Rtc.getDate();
    if (current.year >= 2024 && current.month >= 1 && current.month <= 12 &&
        current.date >= 1 && current.date <= 31) return;

    static const char* const months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char monthName[4] = {};
    int day = 1, year = 2024, hour = 0, minute = 0, second = 0;
    sscanf(__DATE__, "%3s %d %d", monthName, &day, &year);
    sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second);
    int month = 1;
    const char* found = strstr(months, monthName);
    if (found) month = (int)((found - months) / 3) + 1;
    M5.Rtc.setDate(m5::rtc_date_t(year, month, day, -1));
    M5.Rtc.setTime(m5::rtc_time_t(hour, minute, second));
    Serial.printf("[IZ LOUNGE] RTC initialized from build time %04d-%02d-%02d %02d:%02d:%02d\n",
                  year, month, day, hour, minute, second);
}

const char* weaponLabel(uint8_t id)
{
    switch (id)
    {
        case 0: return "BARRAGE";
        case 1: return "MISSILE";
        case 2: return "BEAM";
        case 3: return "MULTI";
        case 4: return "P-BLADE";
        case 5: return "FLARE";
        default: return "?";
    }
}

void drawMechaPreview()
{
    const bool rear = s_room->previewRear;
    M5.Display.fillRect(6, 64, 196, 138, BLACK);
    M5.Display.drawRect(6, 64, 196, 138, DARKCYAN);
    M5.Display.setFont(&fonts::Font0);
    M5.Display.fillRect(11, 69, 36, 12, NAVY);
    M5.Display.setTextColor(CYAN, NAVY);
    M5.Display.drawString(rear ? "REAR" : "FRONT", 13, 71);
    const uint8_t* imageStart = rear ? hangar_mecha_rear_v7_start
                                     : hangar_mecha_front_v7_start;
    const uint8_t* imageEnd = rear ? hangar_mecha_rear_v7_end
                                   : hangar_mecha_front_v7_end;
    // The PNG canvas already contains an aspect-ratio-preserving, centered
    // fit of the reference artwork. Draw it 1:1 so neither view is stretched.
    M5.Display.drawPng(imageStart,
        static_cast<uint32_t>(imageEnd - imageStart), 12, 67, 184, 132);
    M5.Display.setFont(&fonts::lgfxJapanGothic_16);
}

bool pointIn(int x, int y, int left, int top, int width, int height)
{
    return x >= left && x < left + width && y >= top && y < top + height;
}

void drawHudTile(int x, int y, int w, int h, const char* label,
                 bool selected, bool hovered)
{
    const uint16_t fill = selected ? NAVY : (hovered ? 0x18C3 : 0x0841);
    const uint16_t edge = hovered ? YELLOW : (selected ? CYAN : DARKCYAN);
    M5.Display.fillRect(x, y, w, h, fill);
    M5.Display.drawRect(x, y, w, h, edge);
    if (hovered)
    {
        M5.Display.drawRect(x + 1, y + 1, w - 2, h - 2, edge);
        M5.Display.fillTriangle(x + 4, y + h / 2,
                                x + 9, y + h / 2 - 4,
                                x + 9, y + h / 2 + 4, edge);
    }
    else if (selected) M5.Display.fillRect(x, y, 3, h, edge);
    M5.Display.setTextColor(WHITE, fill);
    M5.Display.drawString(label, x + 9, y + (h - 16) / 2);
}

void drawHangar()
{
    static const char* tabs[] = {"腕", "脚", "推進", "武装"};
    for (uint8_t i = 0; i < 4; ++i)
    {
        const int x = 6 + 78 * i;
        drawHudTile(x, 34, 74, 26, tabs[i], s_room->category == i,
                    s_room->hangarFocus == i);
    }
    drawMechaPreview();
    M5.Display.fillRect(206, 64, 108, 138, 0x0841);
    M5.Display.drawRect(206, 64, 108, 138, DARKCYAN);
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextColor(CYAN, 0x0841);
    M5.Display.drawString(s_room->category == 3 ? "WEAPON" : "FRAME", 213, 70);
    if (s_room->category == 3)
    {
        for (uint8_t slot = 0; slot < 2; ++slot)
        {
            const int y = 91 + slot * 49;
            const bool hover = s_room->hangarFocus == 5 + slot;
            drawHudTile(211, y, 98, 41, weaponLabel(slot == 0 ?
                s_room->loadout.weapon1 : s_room->loadout.weapon2),
                s_room->weaponSlot == slot, hover);
            M5.Display.setTextColor(CYAN, hover ? 0x18C3 :
                (s_room->weaponSlot == slot ? NAVY : 0x0841));
            M5.Display.drawString(slot == 0 ? "W1" : "W2", 286, y + 3);
        }
    }
    else
    {
        const uint8_t selected = s_room->category == 0 ? s_room->loadout.arms :
            (s_room->category == 1 ? s_room->loadout.legs : s_room->loadout.thruster);
        drawHudTile(211, 91, 98, 41, "標準型", selected == 0,
                    s_room->hangarFocus == 5);
        const char* variant = s_room->category == 0 ? "強襲型" :
            (s_room->category == 1 ? "高機動型" : "シアン型");
        drawHudTile(211, 140, 98, 41, variant, selected == 1,
                    s_room->hangarFocus == 6);
        if (s_room->category == 2 &&
            !(s_room->flags & PROGRESS_CYAN_THRUSTER_OWNED))
        {
            M5.Display.setTextColor(YELLOW, selected == 1 ? NAVY : 0x0841);
            M5.Display.drawString("LOCK", 276, 166);
        }
    }
    M5.Display.setFont(&fonts::lgfxJapanGothic_16);
    drawHudTile(6, 207, 88, 27, "戻る", false,
                s_room->hangarFocus == 7);
    drawHudTile(99, 207, 103, 27, s_room->previewRear ? "前面表示" : "背面表示",
                false, s_room->hangarFocus == 8);
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextColor(DARKGREY, BLACK);
    M5.Display.drawString("BALL MOVE / M1 SELECT", 211, 213);
}

void loadIzSdFrames()
{
    if (s_izSdFramesReady || s_izSdFrames[0]) return;
    static const char* const paths[8] = {
        "/napetune/lounge/iz/down_0.png", "/napetune/lounge/iz/down_1.png",
        "/napetune/lounge/iz/left_0.png", "/napetune/lounge/iz/left_1.png",
        "/napetune/lounge/iz/right_0.png", "/napetune/lounge/iz/right_1.png",
        "/napetune/lounge/iz/up_0.png", "/napetune/lounge/iz/up_1.png"
    };
    napetune_spi_acquire();
    for (const char* path : paths)
    {
        if (!SD.exists(path))
        {
            napetune_spi_release();
            return;
        }
    }
    bool loaded = true;
    for (int i = 0; i < 8; ++i)
    {
        s_izSdFrames[i] = new (std::nothrow) M5Canvas(&M5.Display);
        if (!s_izSdFrames[i]) { loaded = false; break; }
        s_izSdFrames[i]->setColorDepth(16);
        s_izSdFrames[i]->setPsram(true);
        if (!s_izSdFrames[i]->createSprite(36, 66)) { loaded = false; break; }
        s_izSdFrames[i]->fillSprite(MAGENTA);
        File image = SD.open(paths[i], FILE_READ);
        const bool decoded = image &&
            s_izSdFrames[i]->drawPng(&image, 0, 0, 36, 66);
        image.close();
        if (!decoded)
        {
            loaded = false;
            break;
        }
    }
    napetune_spi_release();
    if (!loaded)
    {
        for (M5Canvas*& frame : s_izSdFrames)
        {
            if (frame)
            {
                if (frame->getBuffer()) frame->deleteSprite();
                delete frame;
                frame = nullptr;
            }
        }
        Serial.println("[IZ LOUNGE] SD sprite load failed; using built-in pixels");
        return;
    }
    s_izSdFramesReady = true;
    Serial.println("[IZ LOUNGE] SD sprite set loaded 36x66 x8");
}

bool loadIzCanvasSet(M5Canvas** frames, size_t count,
                     const char* const* paths, int width, int height,
                     const char* label)
{
    if (frames[0]) return true;
    napetune_spi_acquire();
    bool loaded = true;
    for (size_t i = 0; i < count; ++i)
    {
        if (!SD.exists(paths[i])) { loaded = false; break; }
        frames[i] = new (std::nothrow) M5Canvas(&M5.Display);
        if (!frames[i]) { loaded = false; break; }
        frames[i]->setColorDepth(16);
        frames[i]->setPsram(true);
        if (!frames[i]->createSprite(width, height)) { loaded = false; break; }
        frames[i]->fillSprite(MAGENTA);
        File image = SD.open(paths[i], FILE_READ);
        const bool decoded = image &&
            frames[i]->drawPng(&image, 0, 0, width, height);
        image.close();
        if (!decoded) { loaded = false; break; }
    }
    napetune_spi_release();
    if (!loaded)
    {
        for (size_t i = 0; i < count; ++i)
        {
            if (!frames[i]) continue;
            if (frames[i]->getBuffer()) frames[i]->deleteSprite();
            delete frames[i];
            frames[i] = nullptr;
        }
        Serial.printf("[IZ LOUNGE] %s load failed; fallback enabled\n", label);
        return false;
    }
    Serial.printf("[IZ LOUNGE] %s loaded %dx%d x%u\n",
                  label, width, height, (unsigned)count);
    return true;
}

void loadIzInteractionAssets()
{
    static const char* const portraitPaths[10] = {
        "/napetune/lounge/iz/portraits/thinking.png",
        "/napetune/lounge/iz/portraits/surprised.png",
        "/napetune/lounge/iz/portraits/smile.png",
        "/napetune/lounge/iz/portraits/slight_smile.png",
        "/napetune/lounge/iz/portraits/normal.png",
        "/napetune/lounge/iz/portraits/angry.png",
        "/napetune/lounge/iz/portraits/sad.png",
        "/napetune/lounge/iz/portraits/serious.png",
        "/napetune/lounge/iz/portraits/shy.png",
        "/napetune/lounge/iz/portraits/joy.png"
    };
    s_izPortraitFramesReady = loadIzCanvasSet(
        s_izPortraitFrames, 10, portraitPaths, 96, 144, "portrait set");
}

void drawIzSprite(M5Canvas& target, int x, int y, uint8_t frame, int8_t direction)
{
    if (s_izSdFramesReady)
    {
        const int facing = constrain((int)direction, 0, 3);
        s_izSdFrames[facing * 2 + (frame & 1)]->pushSprite(
            &target, x, y, MAGENTA);
        return;
    }
    constexpr int p = 3;
    auto block = [&](int bx, int by, int bw, int bh, uint16_t color) {
        target.fillRect(x + bx * p, y + by * p, bw * p, bh * p, color);
    };
    // Provisional two-head-tall, limited-palette Iz. This function is the SD
    // asset seam: later 4-direction PNG/BMP frames can replace these blocks
    // without changing lounge movement or interaction logic.
    const bool back = direction == 3;
    const bool side = direction == 1 || direction == 2;
    block(2, 0, 7, 2, WHITE);
    block(1, 2, 9, 4, 0xD6BA);
    block(2, 6, 7, 4, back ? 0xB596 : 0xF6D5);
    if (!back)
    {
        if (side)
            block(direction == 1 ? 3 : 7, 8, 1, 1, CYAN);
        else
        {
            block(3, 8, 1, 1, CYAN);
            block(7, 8, 1, 1, CYAN);
        }
    }
    block(4, 11, 3, 1, WHITE);
    block(2, 12, 7, 5, 0x49AA);
    block(0, 13, 2, 4, WHITE);
    block(9, 13, 2, 4, WHITE);
    block(4, 14, 3, 2, CYAN);
    block(3, 17, 2, 3, WHITE);
    block(6, 17, 2, 3, WHITE);
    block(frame ? 2 : 3, 20, 3, 1, DARKGREY);
    block(frame ? 7 : 6, 20, 3, 1, DARKGREY);
}

void drawIzReactionBubble(M5Canvas& g, int x, int y, uint8_t reaction)
{
    const int bx = constrain(x + 25, 16, 298);
    const int by = max(48, y - 9);
    switch (reaction)
    {
        case 9: // sleeping
            g.setFont(&fonts::Font0);
            g.setTextColor(CYAN);
            g.drawString("Z", bx - 2, by + 7);
            g.drawString("Z", bx + 5, by + 1);
            g.drawString("Z", bx + 13, by - 6);
            break;
        case 0: // notice
            g.fillRect(bx + 3, by, 3, 7, ORANGE);
            g.fillRect(bx + 3, by + 9, 3, 3, YELLOW);
            break;
        case 1: // talk
            g.fillRoundRect(bx - 5, by - 2, 19, 12, 2, WHITE);
            g.fillTriangle(bx + 2, by + 9, bx + 6, by + 9,
                           bx + 4, by + 13, WHITE);
            g.fillCircle(bx, by + 4, 1, DARKGREY);
            g.fillCircle(bx + 4, by + 4, 1, DARKGREY);
            g.fillCircle(bx + 8, by + 4, 1, DARKGREY);
            break;
        case 3: // think
            g.drawCircle(bx + 4, by + 3, 4, CYAN);
            g.drawCircle(bx + 9, by + 1, 3, CYAN);
            g.fillCircle(bx, by + 9, 1, CYAN);
            break;
        case 5: // happy
            g.drawFastVLine(bx + 5, by, 8, CYAN);
            g.drawFastHLine(bx + 5, by, 5, CYAN);
            g.fillCircle(bx + 2, by + 9, 3, CYAN);
            break;
        default: // surprise
            g.fillRect(bx, by, 2, 7, ORANGE);
            g.fillRect(bx + 6, by, 2, 7, ORANGE);
            g.drawPixel(bx, by + 9, YELLOW);
            g.drawPixel(bx + 6, by + 9, YELLOW);
            break;
    }
}

bool isLoungeSleepTime(int hour)
{
    return hour >= 0 && hour < 8;
}

bool isIzSleeping(int hour)
{
    return isLoungeSleepTime(hour) && s_room && !s_room->wakeForEvent01;
}

void drawLoungeScene()
{
    if (!s_room) return;
    if (!s_loungeCanvas)
    {
        s_loungeCanvas = new (std::nothrow) M5Canvas(&M5.Display);
        if (s_loungeCanvas)
        {
            s_loungeCanvas->setColorDepth(16);
            s_loungeCanvas->setPsram(true);
            if (!s_loungeCanvas->createSprite(320, 180))
            {
                delete s_loungeCanvas;
                s_loungeCanvas = nullptr;
            }
        }
    }
    const m5::rtc_time_t clock = M5.Rtc.getTime();
    int hour = clock.hours;
    int minute = clock.minutes;
    if (hour < 0 || hour > 23) hour = 12;
    if (minute < 0 || minute > 59) minute = 0;

    // Compose the complete static room and character off-screen, then perform
    // one contiguous LCD transfer. Furniture/background primitives are never
    // exposed half-drawn, which removes the visible clear-and-repaint flicker.
    if (!s_loungeCanvas) return;
    M5Canvas& g = *s_loungeCanvas;
    g.fillSprite(BLACK);
    g.setFont(&fonts::lgfxJapanGothic_16);
    g.setTextSize(1);
    g.setTextColor(CYAN, BLACK);
    g.drawString("イズのラウンジ", 13, 13);
    g.drawFastHLine(12, 43, 296, DARKCYAN);

    if (core2_glass2_dialogue_active())
    {
        g.fillRect(12, 50, 296, 128, 0x0862);
        g.drawRect(12, 50, 296, 128, CYAN);
        g.setFont(&fonts::Font0);
        g.setTextColor(CYAN, 0x0862);
        g.drawString("DIALOGUE LINK / IZ", 23, 62);
        g.setTextColor(WHITE, 0x0862);
        g.drawString("GLASS2で返答を選択", 23, 88);
        g.drawString("M2  SELECT", 23, 116);
        g.drawString("M1  NEXT / OK", 23, 132);
        g.setTextColor(DARKGREY, 0x0862);
        g.drawString("会話中...", 23, 157);
        const uint8_t expression = min((uint8_t)9,
            core2_glass2_lounge_expression());
        if (s_izPortraitFramesReady)
            s_izPortraitFrames[expression]->pushSprite(&g, 210, 45);
        else
            drawIzSprite(g, 237, 91, 0, 0);
        g.pushSprite(0, 0);
        return;
    }

    if ((int32_t)(s_room->sleepMessageUntilMs - millis()) > 0)
    {
        g.fillRect(12, 50, 296, 128, 0x0862);
        g.drawRect(12, 50, 296, 128, CYAN);
        g.setFont(&fonts::Font0);
        g.setTextColor(CYAN, 0x0862);
        g.drawString("IZ / SLEEPING", 23, 62);
        g.setFont(&fonts::lgfxJapanGothic_16);
        g.setTextColor(WHITE, 0x0862);
        g.drawString("よく眠っているようだ", 23, 91);
        g.drawString("そっとしておこう", 23, 118);
        g.drawPng(iz_sleep_portrait_start,
            (uint32_t)(iz_sleep_portrait_end - iz_sleep_portrait_start), 210, 50);
        g.pushSprite(0, 0);
        return;
    }
    g.fillRect(12, 52, 296, 127, 0x18C5);
    // Rear wall and floor establish a small room with enough open space for
    // random walking. Furniture is deliberately low-detail in this prototype.
    g.fillRect(12, 52, 296, 79, 0x10A4);
    g.fillRect(12, 131, 296, 48, 0x2945);
    for (int y = 136; y < 179; y += 11)
        g.drawFastHLine(12, y, 296, 0x39C7);
    g.fillRect(25, 78, 66, 42, 0x2108); // sofa
    g.drawRect(25, 78, 66, 42, DARKCYAN);
    g.fillRect(31, 87, 25, 24, 0x31AA);
    g.fillRect(59, 87, 25, 24, 0x31AA);
    g.fillRect(105, 112, 48, 9, 0x632C); // low table
    g.fillRect(111, 121, 4, 18, 0x4208);
    g.fillRect(143, 121, 4, 18, 0x4208);

    // Window colors follow the Core2 RTC: dawn, day, evening and night.
    uint16_t skyTop = 0x0010, skyBottom = 0x20A5;
    const char* phase = "NIGHT";
    if (hour >= 5 && hour < 8)
    {
        skyTop = 0xA145; skyBottom = 0xFD20; phase = "MORNING";
    }
    else if (hour >= 8 && hour < 17)
    {
        skyTop = 0x147D; skyBottom = 0x5D7F; phase = "DAY";
    }
    else if (hour >= 17 && hour < 20)
    {
        skyTop = 0x6812; skyBottom = 0xFBE0; phase = "EVENING";
    }
    g.fillRect(190, 61, 106, 65, BLACK);
    g.fillRect(194, 65, 98, 28, skyTop);
    g.fillRect(194, 93, 98, 29, skyBottom);
    if (hour >= 8 && hour < 17)
        g.fillCircle(271, 77, 7, YELLOW);
    else if (hour >= 5 && hour < 20)
        g.fillCircle(271, 85, 6, ORANGE);
    else
    {
        g.fillCircle(270, 78, 6, WHITE);
        g.fillCircle(273, 76, 6, skyTop);
        g.drawPixel(208, 74, WHITE);
        g.drawPixel(232, 83, WHITE);
        g.drawPixel(253, 70, CYAN);
    }
    g.drawFastVLine(242, 65, 57, 0xBDF7);
    g.drawFastHLine(194, 94, 98, 0xBDF7);

    const bool sleeping = isIzSleeping(hour);
    if (sleeping)
    {
        g.drawPng(iz_sleep_bed_start,
            (uint32_t)(iz_sleep_bed_end - iz_sleep_bed_start), 31, 84);
        if (s_room->cursorTouchingIz)
            drawIzReactionBubble(g, 72, 104, 9);
    }

    int bounceY = 0;
    if (!sleeping && s_room->cursorTouchingIz)
    {
        const uint32_t pulse = millis() - s_room->reactionPulseStartMs;
        if (pulse < 110) bounceY = -(int)(pulse * 5 / 110);
        else if (pulse < 230) bounceY = -5 + (int)((pulse - 110) * 5 / 120);
        else if (pulse >= 270 && pulse < 350)
            bounceY = -2 + (int)((pulse - 270) * 2 / 80);
    }
    const int characterY = s_room->izY + bounceY;
    if (!sleeping)
    {
        drawIzSprite(g, s_room->izX, characterY, s_room->walkFrame,
                     s_room->cursorTouchingIz ? 0 : s_room->izDirection);
        if (s_room->cursorTouchingIz)
            drawIzReactionBubble(g, s_room->izX, characterY,
                                 s_room->reactionFrame);
    }

    // Nape Ball cursor.  Its small open shape keeps the character readable.
    const int cx = s_room->cursorX;
    const int cy = s_room->cursorY;
    const uint16_t cursorColor = s_room->cursorTouchingIz ? YELLOW : CYAN;
    g.drawCircle(cx, cy, s_room->cursorTouchingIz ? 6 : 4, cursorColor);
    g.drawFastHLine(cx - 8, cy, 4, cursorColor);
    g.drawFastHLine(cx + 5, cy, 4, cursorColor);
    g.drawFastVLine(cx, cy - 8, 4, cursorColor);
    g.drawFastVLine(cx, cy + 5, 4, cursorColor);
    g.setFont(&fonts::Font0);
    g.setTextColor(CYAN, BLACK);
    g.fillRect(197, 66, 42, 10, BLACK);
    g.setCursor(200, 68);
    g.printf("%02d:%02d", hour, minute);
    g.setCursor(249, 113);
    g.print(phase);
    g.pushSprite(0, 0);
}

void drawButton(int x, const char* label, bool accented)
{
    M5.Display.fillRect(x, 190, 136, 36, accented ? NAVY : DARKGREY);
    M5.Display.drawRect(x, 190, 136, 36, accented ? CYAN : WHITE);
    M5.Display.setTextColor(WHITE, accented ? NAVY : DARKGREY);
    M5.Display.drawString(label, x + 17, 199);
}

void drawRoom()
{
    if (!s_room) return;
    napetune_lcd_acquire();
    M5.Display.fillScreen(BLACK);
    M5.Display.setFont(&fonts::lgfxJapanGothic_16);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(CYAN, BLACK);
    M5.Display.drawString(s_room->hangar ? "格納庫 / HANGAR" : "イズのラウンジ", 13, 13);
    if (s_room->hangar)
    {
        drawHangar();
    }
    else
    {
        M5.Display.drawFastHLine(12, 43, 296, DARKCYAN);
        drawLoungeScene();
        M5.Display.setFont(&fonts::Font0);
        M5.Display.fillRect(14, 55, 106, 13, BLACK);
        M5.Display.setTextColor(WHITE, BLACK);
        M5.Display.setCursor(18, 58);
        M5.Display.print("IZ / LOUNGE");
        M5.Display.setFont(&fonts::lgfxJapanGothic_16);
    }
    M5.Display.setTextColor(s_room->saveFailed ? RED : DARKGREY, BLACK);
    if (s_room->saveFailed)
        M5.Display.drawString("保存できませんでした", 13, 213);
    if (!s_room->hangar)
    {
        M5.Display.drawString("M1 / 画面タップ：話す", 13, 232);
        drawButton(14, "戻る", false);
        drawButton(170, "話す", true);
    }
    M5.Display.setFont(&fonts::Font0);
    napetune_lcd_release();
}

void talkToIz()
{
    if (!s_room) return;
    const m5::rtc_time_t clock = M5.Rtc.getTime();
    const int hour = clock.hours <= 23 ? clock.hours : 12;
    if (isIzSleeping(hour))
    {
        s_room->reactionFrame = 9;
        s_room->reactionPulseStartMs = millis();
        s_room->sleepMessageUntilMs = millis() + 7000;
        core2_glass2_lounge_sleep_message();
        drawRoom();
        return;
    }
    if (!(s_room->flags & PROGRESS_CYAN_THRUSTER_OWNED))
    {
        const uint8_t next = s_room->flags | PROGRESS_CYAN_THRUSTER_OWNED;
        if (game_progress_save(next))
        {
            s_room->flags = next;
            Serial.println("[IZ LOUNGE] CYAN THRUSTER received");
        }
        else s_room->saveFailed = true;
    }
    s_room->izDirection = 0;
    if (isLoungeSleepTime(hour) && s_room->wakeForEvent01)
        core2_glass2_lounge_dialogue_event(0);
    else
        core2_glass2_lounge_dialogue();
    drawRoom();
}

void saveLoadout(const GameLoadout& next)
{
    if (!s_room) return;
    if (game_loadout_save(next))
    {
        s_room->loadout = next;
        s_room->saveFailed = false;
    }
    else s_room->saveFailed = true;
    drawRoom();
}

void selectPart(uint8_t variant)
{
    if (!s_room || s_room->category >= 3) return;
    if (s_room->category == 2 && variant &&
        !(s_room->flags & PROGRESS_CYAN_THRUSTER_OWNED)) return;
    GameLoadout next = s_room->loadout;
    uint8_t* part = s_room->category == 0 ? &next.arms :
        (s_room->category == 1 ? &next.legs : &next.thruster);
    if (*part == variant) return;
    *part = variant;
    saveLoadout(next);
}

void cycleWeapon(uint8_t slot)
{
    if (!s_room || slot > 1) return;
    GameLoadout next = s_room->loadout;
    uint8_t* chosen = slot == 0 ? &next.weapon1 : &next.weapon2;
    const uint8_t other = slot == 0 ? next.weapon2 : next.weapon1;
    do { *chosen = (uint8_t)((*chosen + 1) % GAME_WEAPON_COUNT); }
    while (*chosen == other);
    s_room->weaponSlot = slot;
    saveLoadout(next);
}

bool activateHangarAt(int x, int y)
{
    if (!s_room || !s_room->hangar) return false;
    if (pointIn(x, y, 6, 207, 88, 27)) return true;
    if (pointIn(x, y, 6, 34, 308, 26))
    {
        s_room->category = (uint8_t)constrain((x - 6) / 78, 0, 3);
        drawRoom();
    }
    else if (pointIn(x, y, 6, 64, 196, 138) ||
             pointIn(x, y, 99, 207, 103, 27))
    {
        s_room->previewRear = !s_room->previewRear;
        drawRoom();
    }
    else if (pointIn(x, y, 211, 91, 98, 90))
    {
        const uint8_t row = y < 136 ? 0 : 1;
        if (s_room->category == 3) cycleWeapon(row);
        else selectPart(row);
    }
    return false;
}

bool activateHangarFocus()
{
    static const int16_t focusPoints[9][2] = {
        {43, 47}, {121, 47}, {199, 47}, {277, 47},
        {104, 128}, {260, 111}, {260, 160}, {50, 220}, {150, 220}
    };
    const uint8_t focus = s_room && s_room->hangarFocus <= 8
        ? s_room->hangarFocus : 0;
    return activateHangarAt(focusPoints[focus][0], focusPoints[focus][1]);
}

void moveHangarFocus(int horizontal, int vertical)
{
    uint8_t f = s_room->hangarFocus;
    if (horizontal)
    {
        if (f <= 3) f = (uint8_t)((f + 4 + horizontal) % 4);
        else if (f == 4 && horizontal > 0) f = 5;
        else if ((f == 5 || f == 6) && horizontal < 0) f = 4;
        else if (f == 7 && horizontal > 0) f = 8;
        else if (f == 8 && horizontal < 0) f = 7;
    }
    else if (vertical)
    {
        if (f <= 3) f = (f <= 1) ? 4 : 5;
        else if (f == 4) f = vertical < 0 ? 1 : 7;
        else if (f == 5) f = vertical < 0 ? 3 : 6;
        else if (f == 6) f = vertical < 0 ? 5 : 8;
        else if (f == 7) f = vertical < 0 ? 4 : 0;
        else if (f == 8) f = vertical < 0 ? 6 : 0;
    }
    if (f != s_room->hangarFocus)
    {
        s_room->hangarFocus = f;
        drawRoom();
    }
}
} // namespace

void mode_room_begin(bool hangar)
{
    if (!s_room) s_room = new (std::nothrow) RoomState();
    if (!s_room)
    {
        Serial.println("[MODE] room allocation failed");
        return;
    }
    game_input_clear_nape();
    s_room->flags = game_progress_load();
    s_room->loadout = game_loadout_load();
    s_room->hangar = hangar;
    s_room->previewRear = false;
    s_room->category = 0;
    s_room->weaponSlot = 0;
    s_room->hangarFocus = 0;
    s_room->hangarNavLatched = false;
    s_room->hangarNeutralSinceMs = 0;
    s_room->lastNavMs = millis();
    s_room->touchWasDown = M5.Touch.getCount() > 0;
    s_room->saveFailed = false;
    s_room->walkFrame = 0;
    s_room->lastWalkMs = millis();
    s_room->izX = 128;
    s_room->izY = 104;
    s_room->izTargetX = 128;
    s_room->izTargetY = 104;
    s_room->izDirection = 0;
    s_room->nextWalkDecisionMs = millis() + 700;
    s_room->lastClockMinute = -1;
    s_room->cursorX = hangar ? 43 : 280;
    s_room->cursorY = hangar ? 47 : 70;
    s_room->cursorTouchingIz = false;
    s_room->reactionFrame = 0;
    s_room->nextReactionMs = 0;
    s_room->reactionPulseStartMs = 0;
    s_room->lastDialogueActive = false;
    s_room->lastDialogueExpression = 4;
    s_room->wakeForEvent01 = false;
    s_room->sleepMessageUntilMs = 0;
    if (!hangar)
    {
        ensureRtcClock();
        const m5::rtc_time_t clock = M5.Rtc.getTime();
        const int hour = clock.hours <= 23 ? clock.hours : 12;
        // One in five nighttime visits may wake Iz for event 01. Never allow
        // event 01 twice in succession.
        s_room->wakeForEvent01 = isLoungeSleepTime(hour) &&
            core2_glass2_lounge_can_wake_event01() && random(100) < 20;
        loadIzSdFrames();
        loadIzInteractionAssets();
    }
    else
    {
        core2_glass2_context("HANGAR", "LOADOUT CONTROL",
                             "ボール：移動　M1：決定");
    }
    drawRoom();
    Serial.printf("[MODE] %s flags=0x%02X\n", hangar ? "HANGAR" : "IZ LOUNGE", s_room->flags);
}

bool mode_room_update()
{
    if (!s_room) return true;
    GameInputState input = {};
    game_input_poll(&input);
    const bool touchDown = M5.Touch.getCount() > 0;
    bool touchActionHandled = false;
    if (touchDown && !s_room->touchWasDown)
    {
        const auto touch = M5.Touch.getDetail(0);
        if (touch.isPressed())
        {
            if (touch.y >= 190 && touch.x < 150) return true;
            if (s_room->hangar)
            {
                if (activateHangarAt(touch.x, touch.y)) return true;
                touchActionHandled = true;
            }
            else if (!s_room->hangar)
            {
                if (core2_glass2_dialogue_active())
                {
                    // A lounge screen tap mirrors one fresh M1 edge. Resetting
                    // immediately preserves the bridge's normal edge detector.
                    core2_glass2_nape_buttons(0x01);
                    core2_glass2_nape_buttons(0x00);
                }
                else talkToIz();
                touchActionHandled = true;
            }
        }
    }
    s_room->touchWasDown = touchDown;
    // The shared Core2 touch mapper emits USE for x>=230, y<120. That area
    // overlaps the hangar's weapon rows, so only a non-touch USE (Nape M2)
    // may leave the room. The touch action above owns screen taps.
    if (!touchDown && !core2_glass2_dialogue_active() &&
        (input.pressed & GAME_INPUT_USE)) return true;
    if (!touchActionHandled && (input.pressed & GAME_INPUT_FIRE))
    {
        if (s_room->hangar)
        {
            if (activateHangarFocus()) return true;
        }
        else if (!core2_glass2_dialogue_active()) talkToIz();
    }
    const bool dialogueActive = !s_room->hangar &&
        core2_glass2_dialogue_active();
    if (s_room->hangar)
    {
        const int ax = abs((int)input.aim_x);
        const int ay = abs((int)input.aim_y);
        // One physical ball gesture advances exactly one tile. It must return
        // close to neutral before another navigation edge can be generated.
        if (ax <= 4 && ay <= 4)
        {
            if (!s_room->hangarNeutralSinceMs)
                s_room->hangarNeutralSinceMs = millis();
            else if (millis() - s_room->hangarNeutralSinceMs >= 140)
                s_room->hangarNavLatched = false;
        }
        else if (!s_room->hangarNavLatched && (ax >= 16 || ay >= 16))
        {
            s_room->hangarNeutralSinceMs = 0;
            s_room->hangarNavLatched = true;
            s_room->lastNavMs = millis();
            if (ax > ay) moveHangarFocus(input.aim_x < 0 ? -1 : 1, 0);
            else moveHangarFocus(0, input.aim_y < 0 ? -1 : 1);
        }
        else if (ax > 4 || ay > 4)
            s_room->hangarNeutralSinceMs = 0;
    }
    if (!s_room->hangar)
    {
        const uint8_t expression = core2_glass2_lounge_expression();
        if (dialogueActive != s_room->lastDialogueActive ||
            (dialogueActive && expression != s_room->lastDialogueExpression))
        {
            s_room->lastDialogueActive = dialogueActive;
            s_room->lastDialogueExpression = expression;
            napetune_lcd_acquire();
            drawLoungeScene();
            napetune_lcd_release();
        }
    }
    if (!s_room->hangar && !dialogueActive)
    {
        const uint32_t now = millis();
        const m5::rtc_time_t clock = M5.Rtc.getTime();
        const int clockMinute = clock.minutes;
        bool redrawScene = clockMinute != s_room->lastClockMinute;
        if (redrawScene) s_room->lastClockMinute = clockMinute;
        if (s_room->sleepMessageUntilMs &&
            (int32_t)(now - s_room->sleepMessageUntilMs) >= 0)
        {
            s_room->sleepMessageUntilMs = 0;
            redrawScene = true;
        }

        if (input.aim_x || input.aim_y)
        {
            const int dx = constrain((int)input.aim_x / 5, -18, 18);
            const int dy = constrain((int)input.aim_y / 5, -14, 14);
            s_room->cursorX = constrain(s_room->cursorX + dx, 13, 307);
            s_room->cursorY = constrain(s_room->cursorY + dy, 54, 176);
            redrawScene = true;
        }
        const int clockHour = clock.hours <= 23 ? clock.hours : 12;
        const bool sleeping = isIzSleeping(clockHour);
        const bool touching = sleeping
            ? (s_room->cursorX >= 31 && s_room->cursorX <= 163 &&
               s_room->cursorY >= 84 && s_room->cursorY <= 172)
            : (s_room->cursorX >= s_room->izX - 5 &&
               s_room->cursorX <= s_room->izX + 41 &&
               s_room->cursorY >= s_room->izY - 5 &&
               s_room->cursorY <= s_room->izY + 66);
        if (touching != s_room->cursorTouchingIz)
        {
            s_room->cursorTouchingIz = touching;
            redrawScene = true;
            if (touching)
            {
                s_room->reactionFrame = sleeping ? 9 : 0; // SLEEP / NOTICE
                s_room->nextReactionMs = now + 650;
                s_room->reactionPulseStartMs = now;
                s_room->izTargetX = s_room->izX;
                s_room->izTargetY = s_room->izY;
                s_room->izDirection = 0;
                s_room->walkFrame = 0;
                Serial.println("[IZ LOUNGE] BALL CONTACT");
            }
        }
        if (!sleeping && touching && (int32_t)(now - s_room->nextReactionMs) >= 0)
        {
            static const uint8_t reactions[] = {5, 7, 3, 1};
            s_room->reactionFrame = reactions[random(0, 4)];
            s_room->nextReactionMs = now + random(900, 1600);
            s_room->reactionPulseStartMs = now;
            s_room->izDirection = 0;
            s_room->walkFrame = 0;
            redrawScene = true;
        }
        if (!sleeping && touching && now - s_room->reactionPulseStartMs < 370 &&
            now - s_room->lastWalkMs >= 45)
        {
            s_room->lastWalkMs = now;
            redrawScene = true;
        }

        if (!sleeping && !touching &&
            s_room->izX == s_room->izTargetX && s_room->izY == s_room->izTargetY &&
            (int32_t)(now - s_room->nextWalkDecisionMs) >= 0)
        {
            s_room->izTargetX = random(28, 270);
            s_room->izTargetY = random(88, 113);
        }
        if (!sleeping && !touching && now - s_room->lastWalkMs >= 170 &&
            (s_room->izX != s_room->izTargetX || s_room->izY != s_room->izTargetY))
        {
            s_room->lastWalkMs = now;
            const int dx = s_room->izTargetX - s_room->izX;
            const int dy = s_room->izTargetY - s_room->izY;
            if (abs(dx) >= abs(dy))
            {
                const int step = constrain(dx, -3, 3);
                s_room->izX += step;
                s_room->izDirection = step < 0 ? 1 : 2;
            }
            else
            {
                const int step = constrain(dy, -2, 2);
                s_room->izY += step;
                s_room->izDirection = step < 0 ? 3 : 0;
            }
            s_room->walkFrame ^= 1;
            redrawScene = true;
            if (abs(s_room->izTargetX - s_room->izX) <= 3)
                s_room->izX = s_room->izTargetX;
            if (abs(s_room->izTargetY - s_room->izY) <= 2)
                s_room->izY = s_room->izTargetY;
            if (s_room->izX == s_room->izTargetX && s_room->izY == s_room->izTargetY)
            {
                s_room->izDirection = 0;
                s_room->nextWalkDecisionMs = now + random(700, 2300);
            }
        }
        if (redrawScene)
        {
            napetune_lcd_acquire();
            drawLoungeScene();
            napetune_lcd_release();
        }
    }
    return false;
}
