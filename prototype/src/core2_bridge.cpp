// Software logic: MIT. Embedded creative content: see CONTENT_LICENSE.md and CONTENT_MANIFEST.json.
#include <Arduino.h>
#include <M5UnitGLASS2.h>
#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>
#include <esp_heap_caps.h>
#include <Preferences.h>

#include "core2_bridge.h"
#include "game_input.h"
#include "nape_game_source.h"
#include "spi_guard.h"
#include "napetune_pins.h"
#include "mecha/mecha_game.h"
#include "dialogue/iz_lounge_dialogue.h"

static uint16_t* s_rgbFrame = nullptr;
static bool s_sdReady = false;
static volatile bool s_configActive = false;
static volatile bool s_configDirty = false;
static volatile uint8_t s_targetLayer = 7;
static volatile uint8_t s_configPage = 0;
static volatile uint8_t s_configSelection = 0;
static volatile uint8_t s_configNapeButtons = 0;
static Preferences s_configStore;
static bool s_configStoreReady = false;
static volatile bool s_configSavePending = false;
static volatile uint32_t s_configChangedMs = 0;
static volatile bool s_reticleEnabled = true;

static M5UnitGLASS2* s_glass2 = nullptr;
static M5Canvas* s_glass2Canvas = nullptr;
static bool s_glass2Ready = false;
static uint8_t s_glass2Address = 0;
static uint32_t s_glass2LastUpdateMs = 0;
static uint32_t s_glass2UpdateCount = 0;
static uint32_t s_glass2MaxUpdateUs = 0;
static uint32_t s_glass2NapeReadySinceMs = 0;
static uint32_t s_glass2NextInitMs = 0;
static uint8_t s_glass2InitAttempts = 0;
static TaskHandle_t s_glass2TaskHandle = nullptr;
struct Glass2Message { char english[32]; char japanese[128]; uint8_t portrait; };
static QueueHandle_t s_glass2MessageQueue = nullptr;
struct Glass2DisplayState {
    Glass2Message message;
    bool revealCurrentLine;
    uint8_t previousPixels[1024];
    bool previousValid;
    uint32_t skippedTransfers;
    uint32_t lastTransferPixels;
    char lastSpeaker[12];
    uint32_t speakerChangedMs;
};
static Glass2DisplayState* s_glass2State = nullptr;
static uint32_t s_glass2MessageUntilMs = 0;
static uint32_t s_glass2MessageStartMs = 0;
static volatile bool s_glass2DialogueActive = false;
static volatile bool s_glass2DialogueRequested = false;
static volatile bool s_glass2ContextActive = false;
static char s_glass2ContextArea[12] = "GAMING";
static volatile bool s_glass2IntroSequence = false;
static volatile bool s_glass2DemoUnlockSequence = false;
static volatile bool s_glass2IntroComplete = false;
static volatile bool s_glass2IntroRestartRequested = false;
static volatile bool s_glass2DemoPlaying = false;
static volatile bool s_glass2DemoConsolePage = false;
static volatile uint32_t s_glass2GuideStartMs = 0;
static volatile bool s_glass2DemoImpulseUnlocked = false;
static volatile bool s_glass2AdvanceRequested = false;
static volatile bool s_glass2AutoPauseEnabled = true;
static volatile uint8_t s_glass2DialogueButtons = 0;
// Portrait order: thinking, surprised, smile, slight smile, normal,
// angry, sad, serious, shy, joy.
static volatile uint8_t s_glass2LoungeExpression = 4;
// Bits 0-1: previous M1/M2, 2-3: choice kind, 4-5: selected option.
static uint8_t glass2ChoiceKind() { return (s_glass2DialogueButtons >> 2) & 3; }
static uint8_t glass2ChoiceIndex() { return (s_glass2DialogueButtons >> 4) & 3; }
static void glass2SetChoiceKind(uint8_t kind)
{
    s_glass2DialogueButtons = (s_glass2DialogueButtons & (uint8_t)~0x0C) |
        ((kind & 3) << 2);
}
static void glass2SetChoiceIndex(uint8_t index)
{
    s_glass2DialogueButtons = (s_glass2DialogueButtons & (uint8_t)~0x30) |
        ((index & 3) << 4);
}

static uint8_t glass2MessageChoiceKind(const Glass2Message& msg)
{
    if (!strcmp(msg.english, "CHOICE_SUPPORT")) return 1;
    if (!strcmp(msg.english, "CHOICE_TACTIC")) return 2;
    if (!strcmp(msg.english, "CHOICE_IZU") ||
        !strcmp(msg.english, "CHOICE_LOUNGE")) return 3;
    return 0;
}

static bool glass2IntroSlowMessage(const Glass2Message& msg)
{
    return !strcmp(msg.english, "UNKNOWN") ||
           !strcmp(msg.english, "UNKNOWN FINAL") ||
           !strcmp(msg.english, "CONTACT") ||
           !strcmp(msg.english, "SOURCE") ||
           !strcmp(msg.english, "SIGNAL STATUS") ||
           !strcmp(msg.english, "IZU: SILENCE");
}

static bool glass2IntroBlinkMessage(const Glass2Message& msg)
{
    return !strcmp(msg.english, "SIGNAL DETECTED") ||
           !strcmp(msg.english, "CONNECTING") ||
           !strcmp(msg.english, "MISSILE ALERT") ||
           !strcmp(msg.english, "MULTIPLE TRACKS");
}

static size_t utf8PrefixBytes(const char* text, size_t characters);
static size_t utf8CharacterCount(const char* text);

static void glass2QueueRawMessage(const char* english, const char* japanese,
                                  bool portrait)
{
    if (!s_glass2MessageQueue) return;
    Glass2Message msg = {};
    if (english) strlcpy(msg.english, english, sizeof(msg.english));
    if (japanese) strlcpy(msg.japanese, japanese, sizeof(msg.japanese));
    msg.portrait = portrait ? 1 : 0;
    if (xQueueSend(s_glass2MessageQueue, &msg, 0) != pdTRUE)
    {
        Glass2Message dropped;
        xQueueReceive(s_glass2MessageQueue, &dropped, 0);
        xQueueSend(s_glass2MessageQueue, &msg, 0);
    }
}

static bool glass2PreferredPageEnd(const char* character)
{
    return !strncmp(character, "、", 3) || !strncmp(character, "。", 3) ||
           !strncmp(character, "！", 3) || !strncmp(character, "？", 3) ||
           !strncmp(character, "…", 3);
}

// The 128x64 Glass2 has room for two 12px Japanese lines. Lounge dialogue is
// paged before it reaches the renderer so text never falls past line two.
static void glass2QueuePagedText(const char* english, const char* japanese,
                                 bool portrait,
                                 size_t maxCharactersPerPage)
{
    if (!japanese || !japanese[0])
    {
        glass2QueueRawMessage(english, "", portrait);
        return;
    }
    const size_t preferredMinimum = max((size_t)5,
                                        maxCharactersPerPage / 2);
    const char* remaining = japanese;
    while (*remaining)
    {
        const size_t total = utf8CharacterCount(remaining);
        size_t take = min(total, maxCharactersPerPage);
        if (total > maxCharactersPerPage)
        {
            for (size_t candidate = take; candidate >= preferredMinimum; --candidate)
            {
                const size_t characterOffset = utf8PrefixBytes(remaining, candidate - 1);
                if (glass2PreferredPageEnd(remaining + characterOffset))
                {
                    take = candidate;
                    break;
                }
                if (candidate == preferredMinimum) break;
            }
        }
        // Avoid leaving a one-character orphan on the next page. Moving one
        // more character forward makes the final page at least two chars.
        if (total > take && total - take == 1 && take > 1) --take;
        const size_t pageBytes = utf8PrefixBytes(remaining, take);
        char page[128] = {};
        memcpy(page, remaining, min(pageBytes, sizeof(page) - 1));
        glass2QueueRawMessage(english, page, portrait);
        remaining += pageBytes;
    }
}

static void glass2QueueLoungeText(const char* english, const char* japanese,
                                  bool portrait)
{
    glass2QueuePagedText(english, japanese, portrait, 18);
}

static void glass2QueueMessage(const char* english, const char* japanese, bool portrait)
{
    glass2QueueRawMessage(english, japanese, portrait);
}

extern "C" void core2_glass2_message(const char* english, const char* japanese)
{
    s_glass2ContextActive = false;
    if (s_glass2DemoPlaying && english &&
        (!strcmp(english, "TARGET LOCKED") ||
         !strcmp(english, "TARGET DESTROYED"))) return;
    glass2QueueMessage(english, japanese, false);
}

extern "C" void core2_glass2_paged_message(const char* english,
                                             const char* japanese)
{
    s_glass2ContextActive = false;
    if (s_glass2MessageQueue) xQueueReset(s_glass2MessageQueue);
    s_glass2MessageUntilMs = 0;
    // System guidance often mixes wide Japanese glyphs and narrow ASCII.
    // Fourteen characters per page safely fit the two-line 128x64 layout.
    glass2QueuePagedText(english, japanese, false, 14);
    if (s_glass2TaskHandle) xTaskNotifyGive(s_glass2TaskHandle);
}

extern "C" void core2_glass2_context(
    const char* area,
    const char* english,
    const char* japanese
)
{
    if (s_glass2MessageQueue) xQueueReset(s_glass2MessageQueue);
    s_glass2DialogueActive = false;
    s_glass2DialogueRequested = false;
    s_glass2ContextActive = true;
    strlcpy(s_glass2ContextArea, area ? area : "SYSTEM",
            sizeof(s_glass2ContextArea));
    // Make the current timed message replaceable on the next GLASS2 frame.
    s_glass2MessageUntilMs = 0;
    glass2QueueMessage(english, japanese, false);
    if (s_glass2TaskHandle) xTaskNotifyGive(s_glass2TaskHandle);
}

extern "C" void core2_glass2_pilot_launch_message(void)
{
    s_glass2ContextActive = false;
    if (s_glass2MessageQueue) xQueueReset(s_glass2MessageQueue);
    s_glass2DemoPlaying = false;
    s_glass2DemoConsolePage = false;
    s_glass2DialogueRequested = true;
    s_glass2DialogueActive = s_glass2Ready;
    s_glass2IntroSequence = true;
    s_glass2IntroComplete = false;
    s_glass2IntroRestartRequested = true;
    s_glass2AdvanceRequested = false;
    s_glass2DialogueButtons = 0;
    glass2QueueMessage("SIGNAL DETECTED", "UNKNOWN SIGNAL DETECTED", false);
    glass2QueueMessage("UNKNOWN", "・・・べ・・るか・・・", false);
    glass2QueueMessage("CHOICE_IZU", "イズ：返答を選択", false);
    glass2QueueMessage("CONTACT", "CONTACT: UNKNOWN", false);
    glass2QueueMessage("SOURCE", "SOURCE: ---", false);
    glass2QueueMessage("SIGNAL STATUS", "SIGNAL: UNSTABLE", false);
    glass2QueueMessage("IZU: SILENCE", "・・・・", false);
    glass2QueueMessage("CONNECTING", "CONNECTING・・・・", false);
    glass2QueueMessage("UNKNOWN FINAL", "飛べるか？", false);
}

extern "C" void core2_glass2_demo_unlock_message(void)
{
    s_glass2ContextActive = false;
    if (s_glass2MessageQueue) xQueueReset(s_glass2MessageQueue);
    s_glass2DemoPlaying = false;
    s_glass2DemoConsolePage = false;
    s_glass2DialogueRequested = true;
    s_glass2DialogueActive = s_glass2Ready;
    s_glass2IntroSequence = false;
    s_glass2DemoUnlockSequence = true;
    s_glass2IntroRestartRequested = true;
    s_glass2AdvanceRequested = false;
    s_glass2DialogueButtons = 0;
    glass2QueueMessage("MISSILE ALERT", "MISSILE ALERT！！", false);
    glass2QueueMessage("MULTIPLE TRACKS", "MULTIPLE TRACKS DETECTED", false);
    glass2QueueMessage("IZU:", "やば……！", false);
    glass2QueueMessage("TERMINALIS", "IMPULSE FORM : AVAILABLE", false);
    glass2QueueMessage("TERMINALIS", "FRAME SHIFT : AVAILABLE", false);
    glass2QueueMessage("TERMINALIS", "PLASMA VECTOR SYNC : STANDBY", false);
    glass2QueueMessage("IZU:", "インパルスフォーム……？", false);
    glass2QueueMessage("IZU:", "……TERMINALISは", false);
    glass2QueueMessage("IZU:", "もっと……", false);
    glass2QueueMessage("IZU:", "飛べる！！", false);
}

extern "C" void core2_glass2_demo_success_message(void)
{
    s_glass2ContextActive = false;
    if (s_glass2MessageQueue) xQueueReset(s_glass2MessageQueue);
    s_glass2DemoPlaying = false;
    s_glass2DemoConsolePage = false;
    s_glass2DialogueRequested = true;
    s_glass2DialogueActive = s_glass2Ready;
    s_glass2IntroSequence = false;
    s_glass2DemoUnlockSequence = false;
    s_glass2IntroRestartRequested = true;
    s_glass2AdvanceRequested = false;
    s_glass2DialogueButtons = 0;
    glass2QueueMessage("MISSILE TRACK", "MISSILE TRACK : LOST", false);
    glass2QueueMessage("HOSTILE LOCK", "HOSTILE LOCK : LOST", false);
    glass2QueueMessage("SIGNAL", "SIGNAL : UNSTABLE", false);
    glass2QueueMessage("IZU:", "……あとで、ちゃんと聞くから", false);
    glass2QueueMessage("UNKNOWN", "・・・・", false);
    glass2QueueMessage("NARRATION", "返事はなかった。", false);
    glass2QueueMessage("NARRATION", "TERMINALISは、", false);
    glass2QueueMessage("NARRATION", "さらに空へ上がっていく。", false);
}

extern "C" void core2_glass2_lounge_dialogue_event(int eventIndex)
{
    s_glass2ContextActive = false;
    if (s_glass2MessageQueue) xQueueReset(s_glass2MessageQueue);
    s_glass2DemoPlaying = false;
    s_glass2DemoConsolePage = false;
    s_glass2DialogueRequested = true;
    s_glass2DialogueActive = s_glass2Ready;
    s_glass2IntroSequence = false;
    s_glass2IntroRestartRequested = true;
    s_glass2AdvanceRequested = false;
    s_glass2DialogueButtons = 0;
    const m5::rtc_time_t clock = M5.Rtc.getTime();
    const uint8_t hour = clock.hours <= 23 ? clock.hours : 12;
    const IzLoungeEvent& event = iz_lounge_select_event(hour, clock.minutes,
        (int8_t)eventIndex);
    s_glass2LoungeExpression = event.openingExpression;
    for (uint8_t i = 0; i < event.openingCount; ++i)
        glass2QueueLoungeText("IZU: LOUNGE", event.opening[i], true);
    glass2QueueMessage("CHOICE_LOUNGE", event.title, false);
    Serial.printf("[GLASS2 LOUNGE] DIALOGUE START event=%s choices=%u\n",
                  event.id, (unsigned)event.choiceCount);
}

extern "C" void core2_glass2_lounge_dialogue(void)
{
    core2_glass2_lounge_dialogue_event(-1);
}

extern "C" void core2_glass2_lounge_sleep_message(void)
{
    s_glass2ContextActive = false;
    if (s_glass2MessageQueue) xQueueReset(s_glass2MessageQueue);
    glass2QueueLoungeText("IZU: SLEEPING",
        "よく眠っているようだ。そっとしておこう", false);
}

extern "C" int core2_glass2_lounge_can_wake_event01(void)
{
    return iz_lounge_state().lastEvent != 0;
}

extern "C" int core2_glass2_ready(void)
{
    return s_glass2Ready ? 1 : 0;
}

extern "C" int core2_glass2_intro_complete(void)
{
    return s_glass2IntroComplete ? 1 : 0;
}

extern "C" void core2_glass2_set_demo_playing(int playing)
{
    s_glass2DemoPlaying = playing != 0;
    s_glass2GuideStartMs = millis();
    if (s_glass2DemoPlaying) s_glass2ContextActive = false;
    s_glass2DemoConsolePage = false;
}

extern "C" int core2_glass2_demo_playing(void)
{
    return s_glass2DemoPlaying ? 1 : 0;
}

extern "C" void core2_glass2_set_demo_impulse_unlocked(int unlocked)
{
    s_glass2DemoImpulseUnlocked = unlocked != 0;
    if (s_glass2TaskHandle) xTaskNotifyGive(s_glass2TaskHandle);
}

extern "C" void core2_glass2_toggle_demo_page(void)
{
    if (!s_glass2DemoPlaying) return;
    s_glass2DemoConsolePage = !s_glass2DemoConsolePage;
    if (!s_glass2DemoConsolePage) s_glass2GuideStartMs = millis();
    Serial.printf("[GLASS2 DEMO] page=%s\n",
                  s_glass2DemoConsolePage ? "CONSOLE" : "KEY GUIDE");
}

extern "C" int core2_glass2_dialogue_active(void)
{
    return s_glass2DialogueActive ? 1 : 0;
}

extern "C" int core2_glass2_current_speaker_is_izu(void)
{
    if (!s_glass2State) return 0;
    const Glass2Message& msg = s_glass2State->message;
    return (msg.portrait || !strncmp(msg.english, "IZU:", 4) ||
            !strcmp(msg.english, "CHOICE_IZU")) ? 1 : 0;
}

extern "C" uint8_t core2_glass2_lounge_expression(void)
{
    return s_glass2LoungeExpression;
}

extern "C" void core2_glass2_nape_buttons(uint8_t buttons)
{
    const uint8_t pressed = buttons & (uint8_t)~s_glass2DialogueButtons;
    s_glass2DialogueButtons = (s_glass2DialogueButtons & 0xFC) | (buttons & 0x03);
    if (s_glass2DialogueActive && (pressed & 0x01))
    {
        s_glass2AdvanceRequested = true;
        Serial.println("[GLASS2 DIALOGUE] M1 ADVANCE");
    }
    if (s_glass2DialogueActive && glass2ChoiceKind() && (pressed & 0x02))
    {
        const uint8_t count = glass2ChoiceKind() == 1 ? 2 :
            (glass2ChoiceKind() == 3 && s_glass2State &&
             !strcmp(s_glass2State->message.english, "CHOICE_LOUNGE")
                ? iz_lounge_current_event().choiceCount : 3);
        glass2SetChoiceIndex((glass2ChoiceIndex() + 1) % count);
        Serial.printf("[GLASS2 CHOICE] select=%u/%u\n",
                      (unsigned)(glass2ChoiceIndex() + 1), (unsigned)count);
    }
}

static size_t utf8PrefixBytes(const char* text, size_t characters)
{
    size_t bytes = 0;
    while (text[bytes] && characters--)
    {
        const uint8_t c = (uint8_t)text[bytes];
        bytes += c < 0x80 ? 1 : (c < 0xE0 ? 2 : (c < 0xF0 ? 3 : 4));
    }
    return bytes;
}

static size_t utf8CharacterCount(const char* text)
{
    size_t count = 0;
    for (size_t i = 0; text[i]; ++i)
        if (((uint8_t)text[i] & 0xC0) != 0x80) ++count;
    return count;
}

static void glass2PrintPrefix(M5Canvas& g, const char* text, size_t characters)
{
    char visible[129] = {};
    const size_t bytes = min(utf8PrefixBytes(text, characters), sizeof(visible) - 1);
    memcpy(visible, text, bytes);
    g.print(visible);
}

static void glass2PrintCenteredPrefix(M5Canvas& g, const char* text,
                                      size_t characters, int y, int left)
{
    char visible[129] = {};
    const size_t bytes = min(utf8PrefixBytes(text, characters), sizeof(visible) - 1);
    memcpy(visible, text, bytes);
    const int available = 128 - left;
    const int width = g.textWidth(visible);
    g.setCursor(left + max(0, (available - width) / 2), y);
    g.print(visible);
}

static void glass2PrintAnchoredPrefix(M5Canvas& g, const char* fullLine,
                                      size_t characters, int y, int left)
{
    char visible[129] = {};
    const size_t bytes = min(utf8PrefixBytes(fullLine, characters), sizeof(visible) - 1);
    memcpy(visible, fullLine, bytes);
    g.setCursor(left, y);
    g.print(visible);
}

static void glass2PrintFixedTwoLines(M5Canvas& g, const char* first,
                                     const char* second, size_t revealed)
{
    const size_t firstCount = utf8CharacterCount(first);
    glass2PrintAnchoredPrefix(g, first, min(revealed, firstCount), 16, 3);
    if (revealed > firstCount)
        glass2PrintAnchoredPrefix(g, second, revealed - firstCount, 34, 3);
}

static void glass2PrintWrappedJapanese(M5Canvas& g, const char* text,
                                       size_t revealedCharacters,
                                       int y1, int y2, int left)
{
    const size_t total = utf8CharacterCount(text);
    const int available = 128 - left - 2;
    size_t split = 0;
    char probe[65] = {};
    for (size_t count = 1; count <= total; ++count)
    {
        const size_t bytes = min(utf8PrefixBytes(text, count), sizeof(probe) - 1);
        memset(probe, 0, sizeof(probe));
        memcpy(probe, text, bytes);
        if (g.textWidth(probe) > available) break;
        split = count;
    }
    if (split == 0) split = 1;

    // For mixed Japanese/English messages, prefer the last ASCII space that
    // fits on line one. This keeps words such as AVAILABLE and DETECTED whole.
    size_t secondStart = split;
    if (split < total)
    {
        size_t wordSplit = 0;
        size_t wordSecondStart = 0;
        for (size_t count = 1; count <= split; ++count)
        {
            const size_t offset = utf8PrefixBytes(text, count - 1);
            if (text[offset] == ' ')
            {
                wordSplit = count - 1;
                wordSecondStart = count;
            }
        }
        if (wordSplit > 0)
        {
            split = wordSplit;
            secondStart = wordSecondStart;
        }
    }
    // Avoid a second display line containing only one character.
    if (total > split && total - split == 1 && split > 1) --split;

    char firstLine[65] = {};
    const size_t firstBytes = min(utf8PrefixBytes(text, split), sizeof(firstLine) - 1);
    memcpy(firstLine, text, firstBytes);
    glass2PrintAnchoredPrefix(g, firstLine, min(revealedCharacters, split), y1, left);
    if (revealedCharacters > secondStart && secondStart < total)
    {
        const size_t offset = utf8PrefixBytes(text, secondStart);
        glass2PrintAnchoredPrefix(g, text + offset,
                                 min(revealedCharacters - secondStart,
                                     total - secondStart),
                                 y2, left);
    }
}

static void glass2DrawPilotPortrait(M5Canvas& g)
{
    // Compact monochrome cockpit portrait: hair, face, eyes, headset and collar.
    g.fillTriangle(4, 17, 21, 8, 39, 18, TFT_WHITE);
    g.fillTriangle(4, 17, 8, 53, 16, 30, TFT_WHITE);
    g.fillTriangle(39, 18, 35, 53, 28, 30, TFT_WHITE);
    g.fillRoundRect(11, 16, 24, 31, 8, TFT_WHITE);
    g.fillTriangle(14, 16, 22, 10, 30, 17, TFT_BLACK);
    g.fillTriangle(11, 19, 20, 14, 15, 29, TFT_BLACK);
    g.fillTriangle(35, 19, 25, 14, 32, 28, TFT_BLACK);
    g.fillRect(15, 29, 5, 3, TFT_BLACK);
    g.fillRect(27, 29, 5, 3, TFT_BLACK);
    g.drawPixel(17, 29, TFT_WHITE);
    g.drawPixel(29, 29, TFT_WHITE);
    g.drawFastHLine(21, 39, 6, TFT_BLACK);
    g.drawCircle(8, 31, 4, TFT_WHITE);
    g.drawCircle(38, 31, 4, TFT_WHITE);
    g.drawLine(38, 34, 33, 40, TFT_WHITE);
    g.fillTriangle(10, 55, 22, 44, 22, 61, TFT_WHITE);
    g.fillTriangle(37, 55, 25, 44, 25, 61, TFT_WHITE);
}

static const char* glass2Speaker(const Glass2Message& msg)
{
    if (msg.portrait || !strncmp(msg.english, "IZU:", 4) ||
        !strcmp(msg.english, "CHOICE_IZU")) return "イズ";
    if (!strcmp(msg.english, "CHOICE_LOUNGE") ||
        !strncmp(msg.english, "YOU:", 4)) return "YOU";
    if (!strncmp(msg.english, "UNKNOWN", 7)) return "UNKNOWN";
    if (!strncmp(msg.english, "OPS:", 4)) return "OPS";
    return "SYSTEM";
}

static void glass2DrawSpeakerIcon(M5Canvas& g, int x, int y, const char* speaker)
{
    g.drawRect(x, y, 9, 9, TFT_WHITE);
    if (!strcmp(speaker, "イズ"))
    {
        g.drawPixel(x + 2, y + 3, TFT_WHITE);
        g.drawPixel(x + 6, y + 3, TFT_WHITE);
        g.drawFastHLine(x + 3, y + 6, 3, TFT_WHITE);
    }
    else if (!strcmp(speaker, "OPS"))
    {
        g.drawCircle(x + 4, y + 4, 2, TFT_WHITE);
        g.drawPixel(x + 8, y + 8, TFT_WHITE);
    }
    else
    {
        g.drawFastHLine(x + 2, y + 2, 5, TFT_WHITE);
        g.drawFastHLine(x + 2, y + 4, 5, TFT_WHITE);
        g.drawFastHLine(x + 2, y + 6, 5, TFT_WHITE);
    }
}

static bool glass2Begin()
{
    bool found[120] = {};
    if (!M5.Ex_I2C.begin(I2C_NUM_0, napetune_board::pins.glass_sda, napetune_board::pins.glass_scl))
    {
        Serial.printf("[GLASS2] I2C BEGIN FAILED SDA=%d SCL=%d\n", napetune_board::pins.glass_sda, napetune_board::pins.glass_scl);
        return false;
    }

    M5.Ex_I2C.scanID(found, 100000);
    if (found[0x3C]) s_glass2Address = 0x3C;
    else if (found[0x3D]) s_glass2Address = 0x3D;
    M5.Ex_I2C.release();

    if (!s_glass2Address)
    {
        Serial.printf("[GLASS2] NOT FOUND addr=0x3C/0x3D SDA=%d SCL=%d\n", napetune_board::pins.glass_sda, napetune_board::pins.glass_scl);
        return false;
    }

    s_glass2 = new M5UnitGLASS2(napetune_board::pins.glass_sda, napetune_board::pins.glass_scl, 400000, I2C_NUM_0, s_glass2Address);
    if (!s_glass2 || !s_glass2->init())
    {
        Serial.printf("[GLASS2] INIT FAILED addr=0x%02X\n", s_glass2Address);
        delete s_glass2;
        s_glass2 = nullptr;
        return false;
    }

    s_glass2->setBrightness(128);
    s_glass2Canvas = new M5Canvas(s_glass2);
    if (!s_glass2Canvas)
    {
        Serial.println("[GLASS2] CANVAS ALLOC FAILED");
        return false;
    }
    s_glass2Canvas->setColorDepth(1);
    if (!s_glass2Canvas->createSprite(128, 64))
    {
        Serial.println("[GLASS2] CANVAS FAILED");
        delete s_glass2Canvas;
        s_glass2Canvas = nullptr;
        return false;
    }

    s_glass2Ready = true;
    if (s_glass2DialogueRequested) s_glass2DialogueActive = true;
    Serial.printf("[GLASS2] ONLINE addr=0x%02X size=128x64 refresh=1000ms\n",
                  s_glass2Address);
    if (s_glass2MessageQueue && uxQueueMessagesWaiting(s_glass2MessageQueue) == 0)
        core2_glass2_message("COMMUNICATION LINK", "通信接続");
    return true;
}

static void glass2ResolveChoice(uint8_t kind, uint8_t index)
{
    Serial.printf("[GLASS2 CHOICE] kind=%u answer=%u\n",
                  (unsigned)kind, (unsigned)index);
    if (kind == 1)
    {
        if (index == 0)
            glass2QueueMessage("IZU: SUPPORT YES", "イズ：支援をお願いします", false);
        else
            glass2QueueMessage("IZU: SUPPORT NO", "イズ：単独で突破します", false);
        glass2QueueMessage("OPS: TACTIC", "管制：次の行動は？", false);
        glass2QueueMessage("CHOICE_TACTIC", "次の行動は？", false);
    }
    else if (kind == 2)
    {
        static const char* const answers[] = {
            "イズ：先行します", "イズ：援護に回ります", "イズ：待機します"
        };
        glass2QueueMessage("IZU: TACTIC", answers[index < 3 ? index : 0], false);
        glass2QueueMessage("OPS: ROGER", "管制：了解、健闘を", false);
    }
    else if (kind == 3 && s_glass2MessageQueue)
    {
        if (s_glass2State &&
            !strcmp(s_glass2State->message.english, "CHOICE_LOUNGE"))
        {
            const IzLoungeEvent& event = iz_lounge_current_event();
            const uint8_t selected = index < event.choiceCount ? index : 0;
            const IzLoungeChoice& choice = event.choices[selected];
            s_glass2LoungeExpression = choice.expression;
            glass2QueueLoungeText("YOU: LOUNGE", choice.text, false);
            for (uint8_t i = 0; i < choice.replyCount; ++i)
                glass2QueueLoungeText("IZU: LOUNGE REPLY", choice.replies[i], true);
            iz_lounge_apply_choice(selected);
            return;
        }
        static const char* const replies[] = {
            "…だれなの？", "怖い…", "何が起きているの…"
        };
        Glass2Message reply = {};
        strlcpy(reply.english, "IZU: REPLY", sizeof(reply.english));
        strlcpy(reply.japanese, replies[index < 3 ? index : 0],
                sizeof(reply.japanese));
        xQueueSendToFront(s_glass2MessageQueue, &reply, 0);
    }
}

static void glass2PushChangedFrame(M5Canvas& canvas)
{
    // This canvas is 128x64, 1 bpp, with 16 packed bytes per row.
    // Keep a shadow of the displayed pixels so completed text does not
    // repeatedly trigger a ~34 ms full-screen I2C transfer.
    if (!s_glass2State) { canvas.pushSprite(0, 0); return; }
    uint8_t* previous = s_glass2State->previousPixels;
    bool& valid = s_glass2State->previousValid;
    const uint8_t* pixels = static_cast<const uint8_t*>(canvas.getBuffer());
    if (!pixels || canvas.width() != 128 || canvas.height() != 64 ||
        canvas.bufferLength() != 1024)
    {
        canvas.pushSprite(0, 0);
        valid = false;
        s_glass2State->lastTransferPixels = 128 * 64;
        return;
    }

    int left = 16, right = -1, top = 64, bottom = -1;
    if (valid)
    {
        for (int y = 0; y < 64; ++y)
        {
            for (int byteX = 0; byteX < 16; ++byteX)
            {
                const int offset = y * 16 + byteX;
                if (pixels[offset] == previous[offset]) continue;
                left = min(left, byteX);
                right = max(right, byteX);
                top = min(top, y);
                bottom = max(bottom, y);
            }
        }
    }
    else
    {
        left = 0; right = 15; top = 0; bottom = 63;
    }
    if (right < left)
    {
        ++s_glass2State->skippedTransfers;
        s_glass2State->lastTransferPixels = 0;
        return;
    }

    // Clip on the destination: pushSprite still handles its own palette
    // and source stride. The OLED driver rounds to its hardware page size.
    const int width = (right - left + 1) * 8;
    const int height = bottom - top + 1;
    s_glass2->setClipRect(left * 8, top, width, height);
    canvas.pushSprite(0, 0);
    s_glass2->clearClipRect();
    memcpy(previous, pixels, 1024);
    valid = true;
    s_glass2State->lastTransferPixels = width * height;
}

static void glass2RenderFrame()
{
    const uint32_t now = millis();
    if (!s_glass2Ready || !s_glass2Canvas) return;
    s_glass2LastUpdateMs = now;

    const uint32_t startedUs = micros();
    M5Canvas& g = *s_glass2Canvas;
    if (s_glass2IntroRestartRequested)
    {
        s_glass2IntroRestartRequested = false;
        s_glass2MessageStartMs = 0;
        s_glass2MessageUntilMs = 0;
        s_glass2AdvanceRequested = false;
        if (s_glass2State) s_glass2State->revealCurrentLine = false;
    }
    if (s_glass2DialogueActive && s_glass2AdvanceRequested &&
        s_glass2State && s_glass2MessageStartMs)
    {
        const Glass2Message& current = s_glass2State->message;
        const size_t totalChars = utf8CharacterCount(current.japanese);
        const uint32_t elapsed = now - s_glass2MessageStartMs;
        const bool finalVoice = s_glass2IntroSequence &&
            !strcmp(current.english, "UNKNOWN FINAL");
        const bool blinking = (s_glass2IntroSequence || s_glass2DemoUnlockSequence) &&
            glass2IntroBlinkMessage(current);
        const uint32_t characterMs = s_glass2IntroSequence &&
            glass2IntroSlowMessage(current) ? 185 : 110;
        const size_t visibleChars = finalVoice
            ? (elapsed < 1800 ? 0 : (elapsed - 1800) / 350)
            : (blinking ? totalChars : elapsed / characterMs);
        if (!glass2ChoiceKind() && !s_glass2State->revealCurrentLine &&
            visibleChars < totalChars)
        {
            s_glass2State->revealCurrentLine = true;
            s_glass2AdvanceRequested = false;
            Serial.println("[GLASS2 DIALOGUE] M1 REVEAL LINE");
        }
        else
        {
            if (glass2ChoiceKind())
            {
                glass2ResolveChoice(glass2ChoiceKind(), glass2ChoiceIndex());
                glass2SetChoiceKind(0);
            }
            if (!s_glass2MessageQueue || uxQueueMessagesWaiting(s_glass2MessageQueue) == 0)
            {
                s_glass2DialogueActive = false;
                s_glass2DialogueRequested = false;
                s_glass2AdvanceRequested = false;
                s_glass2MessageUntilMs = 0;
                if (s_glass2IntroSequence)
                {
                    s_glass2IntroSequence = false;
                    s_glass2IntroComplete = true;
                    Serial.println("[GLASS2 INTRO] COMPLETE; SHOW CONTROLS");
                }
                else Serial.println("[GLASS2 DIALOGUE] COMPLETE; GAME RESUME");
                s_glass2DemoUnlockSequence = false;
            }
        }
    }
    if (!s_glass2DialogueActive && glass2ChoiceKind() && s_glass2MessageUntilMs &&
        (int32_t)(now - s_glass2MessageUntilMs) >= 0)
    {
        glass2ResolveChoice(glass2ChoiceKind(), 0);
        glass2SetChoiceKind(0);
    }
    const bool previousMessageDone = !s_glass2MessageUntilMs ||
        (!s_glass2DialogueActive && (int32_t)(now - s_glass2MessageUntilMs) >= 0) ||
        (s_glass2DialogueActive && s_glass2AdvanceRequested);
    Glass2Message incoming;
    if (previousMessageDone && s_glass2MessageQueue &&
        xQueueReceive(s_glass2MessageQueue, &incoming, 0) == pdTRUE)
    {
        if (s_glass2State)
        {
            const char* speaker = glass2Speaker(incoming);
            if (strcmp(s_glass2State->lastSpeaker, speaker))
            {
                strlcpy(s_glass2State->lastSpeaker, speaker,
                        sizeof(s_glass2State->lastSpeaker));
                s_glass2State->speakerChangedMs = now;
            }
            s_glass2State->message = incoming;
            s_glass2State->revealCurrentLine = false;
        }
        glass2SetChoiceKind(glass2MessageChoiceKind(incoming));
        glass2SetChoiceIndex(0);
        s_glass2MessageStartMs = now;
        s_glass2AdvanceRequested = false;
        const uint32_t typingMs = utf8CharacterCount(incoming.japanese) * 110 + 900;
        s_glass2MessageUntilMs = now + (incoming.portrait
            ? 6500 : max((uint32_t)4000, min(typingMs, (uint32_t)6500)));
        Serial.printf("[GLASS2 COMMS] EN=%s JA=%s\n",
                      incoming.english, incoming.japanese);
    }
    const bool showMessage = s_glass2ContextActive || s_glass2DialogueActive || (s_glass2MessageUntilMs &&
        (int32_t)(s_glass2MessageUntilMs - now) > 0);
    const uint32_t rxAge = nape_game_source_motion_age_ms();
    const uint32_t rxCount = nape_game_source_motion_rx_count();
    g.fillScreen(TFT_BLACK);
    g.setTextColor(TFT_WHITE, TFT_BLACK);
    g.setTextSize(1);
    g.setTextWrap(false);
    if (showMessage && s_glass2State)
    {
        const bool speakerAttention = s_glass2State->speakerChangedMs &&
            now - s_glass2State->speakerChangedMs < 1200 &&
            ((now - s_glass2State->speakerChangedMs) / 250) % 2 == 0;
        if (speakerAttention)
        {
            g.fillRect(0, 0, 128, 12, TFT_WHITE);
            g.setTextColor(TFT_BLACK, TFT_WHITE);
        }
        const uint16_t headerRule = speakerAttention ? TFT_BLACK : TFT_WHITE;
        const uint32_t characterMs = s_glass2IntroSequence &&
            glass2IntroSlowMessage(s_glass2State->message) ? 185 : 110;
        const size_t typed = s_glass2ContextActive
            ? utf8CharacterCount(s_glass2State->message.japanese)
            : (s_glass2State->revealCurrentLine
            ? utf8CharacterCount(s_glass2State->message.japanese)
            : (now - s_glass2MessageStartMs) / characterMs);
        if (glass2ChoiceKind())
        {
            if (glass2ChoiceKind() == 3)
            {
                const bool loungeChoice = !strcmp(
                    s_glass2State->message.english, "CHOICE_LOUNGE");
                g.setFont(&fonts::lgfxJapanGothic_12);
                g.setCursor(2, 1);
                g.print(loungeChoice ? "あなた" : "イズ");
                g.setFont(&fonts::Font0);
                g.setCursor(g.getCursorX() + 4, 1);
                g.print("/ CHOICE");
                g.drawFastHLine(0, 11, 128, headerRule);
                g.setTextColor(TFT_WHITE, TFT_BLACK);
                static const char* const answers[] = {
                    "…だれなの？", "怖い…", "何が起きているの…"
                };
                const IzLoungeEvent& loungeEvent = iz_lounge_current_event();
                g.setFont(&fonts::lgfxJapanGothic_12);
                const int answerCount = loungeChoice ? loungeEvent.choiceCount : 3;
                const uint8_t selected = min(glass2ChoiceIndex(),
                    (uint8_t)(answerCount - 1));
                const char* selectedText = loungeChoice
                    ? loungeEvent.choices[selected].text : answers[selected];
                g.drawRoundRect(1, 14, 126, 36, 3, TFT_WHITE);
                glass2PrintWrappedJapanese(g, selectedText,
                    utf8CharacterCount(selectedText), 18, 34, 5);
                g.setFont(&fonts::Font0);
                char counter[8] = {};
                snprintf(counter, sizeof(counter), "%u/%u",
                         (unsigned)(selected + 1), (unsigned)answerCount);
                g.setCursor(127 - g.textWidth(counter), 2);
                g.print(counter);
                g.setTextColor(TFT_WHITE, TFT_BLACK);
                g.setFont(&fonts::Font0);
                g.setCursor(2, 56);
                g.print("M2 NEXT / M1 OK");
            }
            else
            {
            g.setFont(&fonts::Font0);
            g.setCursor(2, 1);
            g.print("COMMS / CHOICE");
            g.drawFastHLine(0, 11, 128, headerRule);
            g.setTextColor(TFT_WHITE, TFT_BLACK);
            g.setFont(&fonts::lgfxJapanGothic_12);
            glass2PrintCenteredPrefix(g, s_glass2State->message.japanese,
                                      typed, 16, 2);
            g.drawFastHLine(0, 32, 128, TFT_WHITE);
            const char* const support[] = {"YES", "NO"};
            const char* const tactic[] = {"先行", "援護", "待機"};
            const bool twoOptions = glass2ChoiceKind() == 1;
            const int count = twoOptions ? 2 : 3;
            for (int i = 0; i < count; ++i)
            {
                const int x = twoOptions ? 2 + i * 64 : 2 + i * 42;
                const int width = twoOptions ? 60 : 40;
                const bool selected = i == glass2ChoiceIndex();
                if (selected) g.fillRect(x, 36, width, 18, TFT_WHITE);
                else g.drawRect(x, 36, width, 18, TFT_WHITE);
                g.setTextColor(selected ? TFT_BLACK : TFT_WHITE,
                               selected ? TFT_WHITE : TFT_BLACK);
                const char* label = twoOptions ? support[i] : tactic[i];
                g.setCursor(x + max(0, (width - g.textWidth(label)) / 2), 39);
                g.print(label);
            }
            g.setTextColor(TFT_WHITE, TFT_BLACK);
            g.setFont(&fonts::Font0);
            g.setCursor(2, 56);
            g.print(s_glass2DialogueActive ? "M2 SELECT / M1 OK" : "AUTO RESPONSE");
            }
        }
        else
        {
            const char* speaker = glass2Speaker(s_glass2State->message);
            if (s_glass2ContextActive)
            {
                g.setFont(&fonts::Font0);
                g.setCursor(2, 1);
                g.print("SYSTEM/COMMS");
            }
            else
            {
                if (!strcmp(speaker, "イズ"))
                    g.setFont(&fonts::lgfxJapanGothic_12);
                else
                    g.setFont(&fonts::Font0);
                g.setCursor(2, 1);
                g.print(speaker);
                g.setFont(&fonts::Font0);
                g.print("/COMMS");
            }
            g.drawFastHLine(0, 11, 128, headerRule);
            g.setTextColor(TFT_WHITE, TFT_BLACK);
            const Glass2Message& msg = s_glass2State->message;
            const bool blinking = (s_glass2IntroSequence || s_glass2DemoUnlockSequence) &&
                glass2IntroBlinkMessage(msg);
            const uint32_t blinkPeriodMs = s_glass2DemoUnlockSequence ? 85 : 400;
            if (!blinking || ((now - s_glass2MessageStartMs) / blinkPeriodMs) % 2 == 0)
            {
                if (s_glass2IntroSequence &&
                    !strcmp(msg.english, "SIGNAL DETECTED"))
                {
                    g.setFont(&fonts::lgfxJapanGothic_12);
                    glass2PrintFixedTwoLines(g, "UNKNOWN SIGNAL", "DETECTED", 99);
                }
                else if (s_glass2IntroSequence &&
                         !strcmp(msg.english, "UNKNOWN"))
                {
                    g.setFont(&fonts::lgfxJapanGothic_12);
                    glass2PrintFixedTwoLines(g, "・・・べ・・", "るか・・・", typed);
                }
                else if (s_glass2IntroSequence &&
                         !strcmp(msg.english, "UNKNOWN FINAL"))
                {
                    const uint32_t elapsed = now - s_glass2MessageStartMs;
                    g.setFont(&fonts::lgfxJapanGothic_16);
                    if (s_glass2State->revealCurrentLine)
                        glass2PrintAnchoredPrefix(g, msg.japanese,
                            utf8CharacterCount(msg.japanese), 26, 3);
                    else if (elapsed >= 1800)
                        glass2PrintAnchoredPrefix(g, msg.japanese,
                            (elapsed - 1800) / 350, 26, 3);
                    else if ((elapsed / 300) % 2 == 0)
                        g.drawFastHLine(3, 33, 13, TFT_WHITE);
                }
                else if (s_glass2DemoUnlockSequence &&
                         !strcmp(msg.japanese, "IMPULSE FORM : AVAILABLE"))
                {
                    g.setFont(&fonts::Font0);
                    glass2PrintFixedTwoLines(g, "IMPULSE FORM :", "AVAILABLE",
                                             typed);
                }
                else if (s_glass2DemoUnlockSequence &&
                         !strcmp(msg.japanese, "FRAME SHIFT : AVAILABLE"))
                {
                    g.setFont(&fonts::Font0);
                    glass2PrintFixedTwoLines(g, "FRAME SHIFT :", "AVAILABLE",
                                             typed);
                }
                else if (s_glass2DemoUnlockSequence &&
                         !strcmp(msg.japanese,
                                 "PLASMA VECTOR SYNC : STANDBY"))
                {
                    g.setFont(&fonts::Font0);
                    glass2PrintFixedTwoLines(g, "PLASMA VECTOR SYNC :",
                                             "STANDBY", typed);
                }
                else if (s_glass2ContextActive &&
                         !strcmp(msg.english, "REACH EXIT POINT"))
                {
                    g.setFont(&fonts::lgfxJapanGothic_12);
                    const char* separator = strstr(msg.japanese, "　");
                    if (separator)
                    {
                        char first[64] = {};
                        const size_t bytes = min((size_t)(separator - msg.japanese),
                                                 sizeof(first) - 1);
                        memcpy(first, msg.japanese, bytes);
                        glass2PrintFixedTwoLines(g, first, separator + 3, 99);
                    }
                    else
                    {
                        glass2PrintWrappedJapanese(g, msg.japanese, typed,
                                                   16, 37, 3);
                    }
                }
                else
                {
                    g.setFont(&fonts::lgfxJapanGothic_16);
                    if (g.textWidth(msg.japanese) > 122)
                        g.setFont(&fonts::lgfxJapanGothic_12);
                    if (g.textWidth(msg.japanese) <= 122)
                        glass2PrintAnchoredPrefix(g, msg.japanese, typed, 26, 3);
                    else
                        glass2PrintWrappedJapanese(g, msg.japanese,
                                                   typed, 16, 37, 3);
                }
            }
            if (s_glass2IntroSequence &&
                glass2IntroSlowMessage(msg))
            {
                const uint32_t noisePhase = (now - s_glass2MessageStartMs) / 250;
                if ((noisePhase & 1u) == 0)
                {
                    g.drawFastHLine(3 + (noisePhase * 7u) % 22u, 49, 2, TFT_WHITE);
                    g.drawFastHLine(102 + (noisePhase * 11u) % 20u, 49, 2, TFT_WHITE);
                }
            }
            const bool demoMissileAlert = s_glass2DemoUnlockSequence &&
                (!strcmp(msg.english, "MISSILE ALERT") ||
                 !strcmp(msg.english, "MULTIPLE TRACKS"));
            if (demoMissileAlert)
            {
                const bool hotFrame = ((now - s_glass2MessageStartMs) / 85) & 1;
                g.drawRect(0, 12, 128, 42, TFT_WHITE);
                if (hotFrame) g.drawRect(2, 14, 124, 38, TFT_WHITE);
            }
            g.drawFastHLine(0, 54, 128, TFT_WHITE);
            g.setFont(&fonts::Font0);
            g.setCursor(2, 56);
            const bool finalVoiceWaiting = !s_glass2State->revealCurrentLine &&
                s_glass2IntroSequence &&
                !strcmp(msg.english, "UNKNOWN FINAL") &&
                now - s_glass2MessageStartMs <
                    1800 + utf8CharacterCount(msg.japanese) * 350 + 700;
            g.print(s_glass2ContextActive ? "CURSOR INFORMATION" :
                    (finalVoiceWaiting ? "SIGNAL..." :
                    (s_glass2DialogueActive ? "M1 NEXT" : "LINK ACTIVE")));
        }
    }
    else if (s_glass2DemoPlaying && !s_glass2DemoConsolePage)
    {
        // Unsigned elapsed time remains valid across millis() rollover.
        const uint8_t page = ((uint32_t)(now - s_glass2GuideStartMs) / 5000U) % 2;
        g.setFont(&fonts::Font0);
        g.setCursor(2, 1);
        g.print("TAP:CONSOLE");
        g.setCursor(108, 1);
        g.printf("%u/2", page + 1);
        g.drawFastHLine(0, 10, 128, TFT_WHITE);
        g.setFont(&fonts::lgfxJapanGothic_12);
        static const char* const lockedGuide[] = {
            "M1 攻撃  M2 ロール",
            "M1長押し 溜め攻撃",
            "BALL 照準・高度",
            "WHEEL+/- 武器切替"
        };
        static const char* const unlockedGuide[] = {
            "M1 攻撃  M2 ロール",
            "01 高速飛行に変形",
            "BALL 照準・高度",
            "WHEEL+/- 武器切替"
        };
        static const char* const flightGuide[] = {
            "02 フレア射出",
            "03 スロットルUP",
            "04 スロットルDown"
        };
        const char* const* guide = page == 1 ? flightGuide :
            (s_glass2DemoImpulseUnlocked ? unlockedGuide : lockedGuide);
        const int rows = page == 1 ? 3 : 4;
        for (int i = 0; i < rows; ++i)
        {
            g.setCursor(2, 13 + i * 12);
            g.print(guide[i]);
        }
    }
    else
    {
        g.setCursor(2, 1);
        g.print(s_glass2DemoPlaying ? "CONSOLE  TAP:KEYS" :
                "NAPETUNE SUB CONSOLE");
        g.drawFastHLine(0, 10, 128, TFT_WHITE);

        // Lightweight radar/status proof. Animation advances only once per second.
        const int phase = (s_glass2UpdateCount % 8);
        const int radarX = 31;
        const int radarY = 38;
        g.drawCircle(radarX, radarY, 21, TFT_WHITE);
        g.drawCircle(radarX, radarY, 11, TFT_WHITE);
        g.drawFastHLine(9, radarY, 44, TFT_WHITE);
        g.drawFastVLine(radarX, 16, 44, TFT_WHITE);
        static const int8_t sweepX[8] = { 18, 13, 0, -13, -18, -13, 0, 13 };
        static const int8_t sweepY[8] = { 0, 13, 18, 13, 0, -13, -18, -13 };
        g.drawLine(radarX, radarY, radarX + sweepX[phase], radarY + sweepY[phase], TFT_WHITE);
        g.fillCircle(39, 29, 2, TFT_WHITE);

        g.setCursor(60, 17); g.print("NAPE");
        g.setCursor(96, 17); g.print(nape_game_source_ready() ? "LINK" : "WAIT");
        g.setCursor(60, 30); g.print("USB CS19");
        const int sync = (!nape_game_source_ready() || rxAge == UINT32_MAX)
            ? 0 : (rxAge >= 1000 ? 0 : 100 - (int)(rxAge / 10));
        g.setCursor(60, 43); g.printf("RX %lu", (unsigned long)rxCount);
        g.drawRect(60, 54, 64, 7, TFT_WHITE);
        g.fillRect(62, 56, (60 * sync) / 100, 3, TFT_WHITE);
    }
    glass2PushChangedFrame(g);

    const uint32_t elapsedUs = micros() - startedUs;
    if (elapsedUs > s_glass2MaxUpdateUs) s_glass2MaxUpdateUs = elapsedUs;
    ++s_glass2UpdateCount;
    if ((s_glass2UpdateCount % 10) == 0)
    {
        Serial.printf("[GLASS2 PERF] updates=%u last=%uus max=%uus nape=%d rx=%u age=%ums addr=0x%02X pixels=%u skipped=%u\n",
                      (unsigned)s_glass2UpdateCount, (unsigned)elapsedUs,
                      (unsigned)s_glass2MaxUpdateUs,
                      nape_game_source_ready() ? 1 : 0, (unsigned)rxCount,
                      rxAge == UINT32_MAX ? 0u : (unsigned)rxAge, s_glass2Address,
                      (unsigned)(s_glass2State ? s_glass2State->lastTransferPixels : 8192),
                      (unsigned)(s_glass2State ? s_glass2State->skippedTransfers : 0));
        s_glass2MaxUpdateUs = 0;
    }
}

static void glass2Task(void*)
{
    Serial.println("[GLASS2] render task core=0 priority=1");
    for (;;)
    {
        glass2RenderFrame();
        const bool typing = s_glass2ContextActive || s_glass2DialogueActive || (s_glass2MessageUntilMs &&
            (int32_t)(s_glass2MessageUntilMs - millis()) > 0);
        const bool pending = s_glass2MessageQueue &&
            uxQueueMessagesWaiting(s_glass2MessageQueue) > 0;
        // One transfer occupies ~34 ms on this bus. During conversation,
        // updating every 110 ms competes with USB polling and the main LCD;
        // the typewriter can safely advance several glyphs per refresh.
        ulTaskNotifyTake(pdTRUE,
            pdMS_TO_TICKS((typing || pending) ? 250 : 1000));
    }
}

extern "C" void core2_glass2_service(void)
{
    if (s_glass2Ready) return;

    const uint32_t now = millis();
    // USB enumeration is timing-sensitive on this hardware. Let Nape mount
    // all interfaces and remain stable before starting the external I2C bus.
    if (!nape_game_source_ready())
    {
        s_glass2NapeReadySinceMs = 0;
        return;
    }
    if (!s_glass2NapeReadySinceMs)
    {
        s_glass2NapeReadySinceMs = now;
        s_glass2NextInitMs = now + 1500;
        Serial.println("[GLASS2] DEFERRED; waiting 1500ms after Nape online");
        return;
    }
    if ((int32_t)(now - s_glass2NextInitMs) < 0) return;

    ++s_glass2InitAttempts;
    Serial.printf("[GLASS2] PROBE attempt=%u SDA=%d SCL=%d\n",
                  (unsigned)s_glass2InitAttempts, napetune_board::pins.glass_sda, napetune_board::pins.glass_scl);
    if (!glass2Begin())
    {
        s_glass2Address = 0;
        s_glass2NextInitMs = now + 2000;
        return;
    }

    const BaseType_t rc = xTaskCreatePinnedToCore(
        glass2Task, "Glass2", 4096, nullptr, 1,
        &s_glass2TaskHandle, 0);
    if (rc != pdPASS)
    {
        Serial.println("[GLASS2] render task create FAILED");
        s_glass2Ready = false;
        s_glass2NextInitMs = now + 2000;
    }
}

static void markConfigChanged()
{
    s_configDirty = true;
    s_configSavePending = true;
    s_configChangedMs = millis();
}

// Only actions consumed by the current NapeTune modes are offered.
static constexpr uint8_t s_configActions[] = {
    NAPE_ACTION_OFF, NAPE_ACTION_FIRE, NAPE_ACTION_USE,
    NAPE_ACTION_RUN_TOGGLE, NAPE_ACTION_WEAPON_NEXT,
    NAPE_ACTION_GUIDE_TOGGLE, NAPE_ACTION_HUD_TOGGLE,
    NAPE_ACTION_THROTTLE_UP, NAPE_ACTION_THROTTLE_DOWN, NAPE_ACTION_FLARE
};
static bool isNapetuneAction(uint8_t action)
{
    for (uint8_t candidate : s_configActions)
        if (candidate == action) return true;
    return false;
}
static const char* configItemName(uint8_t item)
{
    static const char* names[] = {
        "M1", "M2", "F9", "F10", "F11", "F12",
        "X SENSITIVITY", "Y SENSITIVITY", "BOOST POWER",
        "MECHA RENDER", "DEMO AUTOPILOT", "COMMS AUTO PAUSE",
        "RESET DEFAULT"
    };
    return item < 13 ? names[item] : "?";
}

static void resetConfigDefaults()
{
    static const uint8_t keyDefaults[4] = {
        NAPE_ACTION_GUIDE_TOGGLE, NAPE_ACTION_RUN_TOGGLE,
        NAPE_ACTION_THROTTLE_UP, NAPE_ACTION_THROTTLE_DOWN
    };
    nape_game_source_set_mouse_action(0, NAPE_ACTION_FIRE);
    nape_game_source_set_mouse_action(1, NAPE_ACTION_USE);
    for (uint8_t i = 0; i < 4; ++i)
        nape_game_source_set_key_action(i, keyDefaults[i]);
    static const int tuningDefaults[9] = {100, 65, 110, 64, 16, 32, 75, 100, 100};
    for (uint8_t i = 6; i < 9; ++i)
        game_input_set_tuning(i, tuningDefaults[i]);
    mecha_game_set_hybrid_render(true);
    mecha_game_set_autopilot(false);
    s_glass2AutoPauseEnabled = true;
    markConfigChanged();
    Serial.println("[NAPE CONFIG] defaults restored");
}

static void loadConfig()
{
    if (!s_configStore.begin("nape_cfg", false)) return;
    s_configStoreReady = true;
    if (s_configStore.getUChar("version", 0) != 1)
    {
        resetConfigDefaults();
        s_configChangedMs = 0;
        return;
    }
    for (uint8_t i = 0; i < 2; ++i)
    {
        char key[4]; snprintf(key, sizeof(key), "m%u", i);
        const uint8_t fallback = i == 0 ? NAPE_ACTION_FIRE : NAPE_ACTION_USE;
        const uint8_t saved = s_configStore.getUChar(key, fallback);
        nape_game_source_set_mouse_action(i, isNapetuneAction(saved) ? saved : fallback);
    }
    static const uint8_t keyDefaults[4] = {
        NAPE_ACTION_GUIDE_TOGGLE, NAPE_ACTION_RUN_TOGGLE,
        NAPE_ACTION_THROTTLE_UP, NAPE_ACTION_THROTTLE_DOWN
    };
    uint8_t loadedKeys[4];
    for (uint8_t i = 0; i < 4; ++i)
    {
        char key[4]; snprintf(key, sizeof(key), "k%u", i);
        loadedKeys[i] = s_configStore.getUChar(key, keyDefaults[i]);
    }
    // Upgrade the former F11/F12 defaults. Other obsolete actions below
    // fall back to the corresponding NapeTune default; valid mappings stay.
    if (loadedKeys[2] == NAPE_ACTION_STRAFE_LEFT &&
        loadedKeys[3] == NAPE_ACTION_STRAFE_RIGHT)
    {
        loadedKeys[2] = NAPE_ACTION_THROTTLE_UP;
        loadedKeys[3] = NAPE_ACTION_THROTTLE_DOWN;
        s_configStore.putUChar("k2", loadedKeys[2]);
        s_configStore.putUChar("k3", loadedKeys[3]);
        Serial.println("[NAPE CONFIG] F11/F12 migrated to throttle up/down");
    }
    for (uint8_t i = 0; i < 4; ++i)
        nape_game_source_set_key_action(i, isNapetuneAction(loadedKeys[i]) ? loadedKeys[i] : keyDefaults[i]);
    static const int tuningDefaults[9] = {100, 65, 110, 64, 16, 32, 75, 100, 100};
    for (uint8_t i = 0; i < 9; ++i)
    {
        char key[4]; snprintf(key, sizeof(key), "t%u", i);
        game_input_set_tuning(i, s_configStore.getInt(key, tuningDefaults[i]));
    }
    s_reticleEnabled = s_configStore.getBool("reticle", true);
    mecha_game_set_hybrid_render(s_configStore.getBool("hybrid", true));
    mecha_game_set_autopilot(s_configStore.getBool("autopilot", false));
    s_glass2AutoPauseEnabled = s_configStore.getBool("commPause", true);
    Serial.println("[NAPE CONFIG] saved settings loaded");
}

static const char* actionName(uint8_t action)
{
    switch (action)
    {
        case NAPE_ACTION_MAP:          return "MAP";
        case NAPE_ACTION_RUN_TOGGLE:   return "BOOST TOGGLE";
        case NAPE_ACTION_STRAFE_LEFT:  return "STRAFE LEFT";
        case NAPE_ACTION_STRAFE_RIGHT: return "STRAFE RIGHT";
        case NAPE_ACTION_FIRE:         return "FIRE";
        case NAPE_ACTION_USE:          return "USE";
        case NAPE_ACTION_WEAPON_NEXT:  return "WEAPON NEXT";
        case NAPE_ACTION_FORWARD:      return "FORWARD";
        case NAPE_ACTION_BACKWARD:     return "BACKWARD";
        case NAPE_ACTION_TURN_LEFT:    return "TURN LEFT";
        case NAPE_ACTION_TURN_RIGHT:   return "TURN RIGHT";
        case NAPE_ACTION_GUIDE_TOGGLE:return "GUIDE TOGGLE";
        case NAPE_ACTION_HUD_TOGGLE:  return "HUD TOGGLE";
        case NAPE_ACTION_THROTTLE_UP: return "THROTTLE UP";
        case NAPE_ACTION_THROTTLE_DOWN:return "THROTTLE DOWN";
        case NAPE_ACTION_FLARE:        return "FLARE";
        default:                       return "OFF";
    }
}

static void adjustConfigItem(int direction)
{
    const uint8_t item = s_configSelection;
    if (item < 6)
    {
        const uint8_t current = item < 2
            ? nape_game_source_get_mouse_action(item)
            : nape_game_source_get_key_action(item - 2);
        const int count = sizeof(s_configActions) / sizeof(s_configActions[0]);
        int index = 0;
        for (int i = 0; i < count; ++i)
            if (s_configActions[i] == current) index = i;
        const uint8_t action = s_configActions[(index + (direction > 0 ? 1 : count - 1)) % count];
        if (item < 2) nape_game_source_set_mouse_action(item, action);
        else nape_game_source_set_key_action(item - 2, action);
    }
    else if (item < 9)
        game_input_adjust_tuning(item, direction > 0 ? 5 : -5);
    else if (item == 9)
        mecha_game_set_hybrid_render(direction > 0);
    else if (item == 10)
        mecha_game_set_autopilot(direction > 0);
    else if (item == 11)
        s_glass2AutoPauseEnabled = direction > 0;
    else
    {
        if (direction > 0) resetConfigDefaults();
        return;
    }
    markConfigChanged();
}

static void moveConfigSelection(int direction)
{
    int next = (int)s_configSelection + (direction >= 0 ? 1 : -1);
    if (next < 0) next = 12;
    if (next > 12) next = 0;
    s_configSelection = (uint8_t)next;
    s_configPage = next >= 9 ? 2 : (next >= 6 ? 1 : 0);
    s_configDirty = true;
}

extern "C" void core2_draw_battery_indicator(void)
{
    static uint32_t lastReadMs = 0;
    static int level = -1;
    static bool charging = false;
    const uint32_t now = millis();
    if (level < 0 || now - lastReadMs >= 5000)
    {
        lastReadMs = now;
        level = constrain((int)M5.Power.getBatteryLevel(), 0, 100);
        charging = M5.Power.isCharging() == m5::Power_Class::is_charging;
    }

    uint16_t color = GREEN;
    if (charging) color = CYAN;
    else if (level < 20) color = RED;
    else if (level < 40) color = YELLOW;

    constexpr int x = 301;
    constexpr int y = 7;
    constexpr int w = 14;
    constexpr int h = 9;
    M5.Display.fillRect(x - 1, y - 1, w + 4, h + 2, BLACK);
    M5.Display.drawRect(x, y, w, h, color);
    M5.Display.fillRect(x + w, y + 3, 2, 3, color);
    const int fillW = (w - 4) * level / 100;
    if (fillW > 0) M5.Display.fillRect(x + 2, y + 2, fillW, h - 4, color);
}

static void drawConfigScreen()
{
    M5.Display.fillScreen(BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(CYAN, BLACK);
    M5.Display.setCursor(12, 10);
    M5.Display.print("NAPETUNE CONFIG");

    M5.Display.setTextSize(1);
    M5.Display.setTextColor(WHITE, BLACK);
    M5.Display.fillRect(8, 32, 96, 22, s_configPage == 0 ? DARKCYAN : DARKGREY);
    M5.Display.fillRect(112, 32, 96, 22, s_configPage == 1 ? DARKCYAN : DARKGREY);
    M5.Display.fillRect(216, 32, 96, 22, s_configPage == 2 ? DARKCYAN : DARKGREY);
    M5.Display.setTextColor(WHITE);
    M5.Display.setCursor(39, 39); M5.Display.print("KEYS");
    M5.Display.setCursor(140, 39); M5.Display.print("CONTROL");
    M5.Display.setCursor(237, 39); M5.Display.print("OPTIONS");

    const uint8_t item = s_configSelection;
    const uint8_t first = s_configPage == 0 ? 0 : (s_configPage == 1 ? 6 : 9);
    const uint8_t last = s_configPage == 0 ? 5 : (s_configPage == 1 ? 8 : 12);
    const uint8_t previous = item == first ? last : item - 1;
    const uint8_t next = item == last ? first : item + 1;

    M5.Display.setTextColor(DARKGREY, BLACK);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(20, 68);
    M5.Display.printf("^ %s", configItemName(previous));

    M5.Display.fillRect(8, 91, 304, 82, NAVY);
    M5.Display.drawRect(8, 91, 304, 82, CYAN);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(YELLOW, NAVY);
    M5.Display.setCursor(20, 103);
    M5.Display.print(configItemName(item));
    M5.Display.setTextColor(WHITE, NAVY);
    M5.Display.setCursor(20, 139);
    M5.Display.print("< ");
    if (item < 6)
    {
        const uint8_t action = item < 2
            ? nape_game_source_get_mouse_action(item)
            : nape_game_source_get_key_action(item - 2);
        M5.Display.print(actionName(action));
    }
    else if (item < 9)
        M5.Display.printf("%d%%", game_input_get_tuning(item));
    else if (item == 9)
        M5.Display.print(mecha_game_get_hybrid_render() ? "HYBRID" : "LINE");
    else if (item == 10)
        M5.Display.print(mecha_game_get_autopilot() ? "ON" : "OFF");
    else if (item == 11)
        M5.Display.print(s_glass2AutoPauseEnabled ? "ON" : "OFF");
    else
        M5.Display.print("M2: RESTORE");
    M5.Display.print(" >");

    M5.Display.setTextSize(1);
    M5.Display.setTextColor(DARKGREY, BLACK);
    M5.Display.setCursor(20, 184);
    M5.Display.printf("v %s", configItemName(next));

    M5.Display.setTextColor(DARKGREY, BLACK);
    M5.Display.setCursor(12, 224);
    M5.Display.print("A:back B:next  M1:- M2:+  Hold:close");
    core2_draw_battery_indicator();
}

extern "C" int core2_config_is_active(void)
{
    return s_configActive ? 1 : 0;
}

extern "C" void core2_config_toggle(void)
{
    s_configActive = !s_configActive;
    s_configDirty = true;
    if (s_configActive)
    {
        s_configNapeButtons = 0;
        nape_game_source_request_orientation();
    }
}

extern "C" void core2_config_touch(int x, int y)
{
    if (!s_configActive) return;
    if (y >= 28 && y < 58)
    {
        s_configPage = x < 107 ? 0 : (x < 214 ? 1 : 2);
        s_configSelection = s_configPage == 0 ? 0 : (s_configPage == 1 ? 6 : 9);
        s_configDirty = true;
        return;
    }
    // For the NapeTune sensitivity/boost rows, the whole content area is
    // a large decrement/increment control. A/B handle item navigation.
    if (s_configSelection >= 6 && s_configSelection < 9 && y >= 58 && y < 200)
    {
        adjustConfigItem(x < 160 ? -1 : 1);
        return;
    }
    if (y < 100)
    {
        moveConfigSelection(-1);
        return;
    }
    if (y >= 173)
    {
        moveConfigSelection(1);
        return;
    }
    adjustConfigItem(x < 160 ? -1 : 1);
}

extern "C" void core2_config_nape_wheel(int steps)
{
    if (!s_configActive || steps == 0) return;
    moveConfigSelection(steps > 0 ? 1 : -1);
}

extern "C" void core2_config_move(int direction)
{
    if (!s_configActive || direction == 0) return;
    moveConfigSelection(direction);
}

extern "C" void core2_config_nape_buttons(uint8_t buttons)
{
    if (!s_configActive) return;
    const uint8_t pressed = buttons & (uint8_t)~s_configNapeButtons;
    s_configNapeButtons = buttons;
    if (pressed & 0x01) adjustConfigItem(-1);
    if (pressed & 0x02) adjustConfigItem(1);
}

extern "C" void core2_config_service(void)
{
    if (!s_configStoreReady || !s_configSavePending) return;
    if (s_configChangedMs != 0 && millis() - s_configChangedMs < 1000) return;
    for (uint8_t i = 0; i < 2; ++i)
    {
        char key[4]; snprintf(key, sizeof(key), "m%u", i);
        s_configStore.putUChar(key, nape_game_source_get_mouse_action(i));
    }
    for (uint8_t i = 0; i < 4; ++i)
    {
        char key[4]; snprintf(key, sizeof(key), "k%u", i);
        s_configStore.putUChar(key, nape_game_source_get_key_action(i));
    }
    for (uint8_t i = 0; i < 9; ++i)
    {
        char key[4]; snprintf(key, sizeof(key), "t%u", i);
        s_configStore.putInt(key, game_input_get_tuning(i));
    }
    s_configStore.putUChar("version", 1);
    s_configStore.putBool("reticle", s_reticleEnabled);
    s_configStore.putBool("hybrid", mecha_game_get_hybrid_render());
    s_configStore.putBool("autopilot", mecha_game_get_autopilot());
    s_configStore.putBool("commPause", s_glass2AutoPauseEnabled);
    s_configSavePending = false;
    Serial.println("[NAPE CONFIG] settings saved");
}

extern "C" void core2_config_render(void)
{
    static uint32_t lastBatteryDrawMs = 0;
    if (!s_configActive) return;
    const uint32_t now = millis();
    if (!s_configDirty && now - lastBatteryDrawMs < 5000) return;
    napetune_lcd_acquire();
    if (s_configDirty) drawConfigScreen();
    else core2_draw_battery_indicator();
    napetune_lcd_release();
    lastBatteryDrawMs = now;
    s_configDirty = false;
}

extern "C" void core2_reticle_toggle(void)
{
    s_reticleEnabled = !s_reticleEnabled;
    markConfigChanged();
    Serial.printf("[CORE2 RETICLE] %s\n", s_reticleEnabled ? "ON" : "OFF");
}

static void framePixel(
    uint16_t* frame, int width, int height,
    int x, int y, uint16_t color
)
{
    if (x >= 0 && x < width && y >= 0 && y < height)
        frame[y * width + x] = color;
}

static uint16_t blendGreen(uint16_t source)
{
    // Approximately 50% source + 50% bright green in RGB565.
    const uint16_t r = ((source >> 11) & 0x1F) >> 1;
    const uint16_t g = ((((source >> 5) & 0x3F) + 0x3F) >> 1) & 0x3F;
    const uint16_t b = (source & 0x1F) >> 1;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static void reticlePixel(uint16_t* frame, int width, int height, int x, int y)
{
    if (x >= 0 && x < width && y >= 0 && y < height)
        frame[y * width + x] = blendGreen(frame[y * width + x]);
}

static void drawReticle(uint16_t* frame, int width, int height)
{
    if (!s_reticleEnabled || !frame) return;
    const int cx = width / 2;
    const int cy = height / 2;
    // Thin segmented crosshair with a clear center for target visibility.
    for (int d = 6; d <= 18; ++d)
    {
        reticlePixel(frame, width, height, cx - d, cy);
        reticlePixel(frame, width, height, cx + d, cy);
        reticlePixel(frame, width, height, cx, cy - d);
        reticlePixel(frame, width, height, cx, cy + d);
    }
    reticlePixel(frame, width, height, cx - 1, cy - 1);
    reticlePixel(frame, width, height, cx + 1, cy - 1);
    reticlePixel(frame, width, height, cx - 1, cy + 1);
    reticlePixel(frame, width, height, cx + 1, cy + 1);
}

static uint16_t tinyGlyph(char c)
{
    // 3x5, row-major, low 15 bits.
    static const uint16_t digits[10] = {
        0x7B6F, 0x2492, 0x73E7, 0x73CF, 0x5BC9,
        0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF
    };
    if (c >= '0' && c <= '9') return digits[c - '0'];
    if (c == '%') return 0x5294;
    if (c == '-') return 0x01C0;
    if (c == 'R') return 0x7AEC;
    if (c == 'U') return 0x5B6F;
    if (c == 'N') return 0x5F6D;
    return 0;
}

static void drawTinyChar(
    uint16_t* frame, int width, int height,
    int x, int y, char c, uint16_t color
)
{
    const uint16_t bits = tinyGlyph(c);
    for (int row = 0; row < 5; ++row)
    {
        for (int col = 0; col < 3; ++col)
        {
            if (bits & (1u << (14 - (row * 3 + col))))
            {
                for (int yy = 0; yy < 2; ++yy)
                    for (int xx = 0; xx < 2; ++xx)
                        framePixel(frame, width, height,
                                   x + col * 2 + xx, y + row * 2 + yy, color);
            }
        }
    }
}

static void drawPowerOverlay(uint16_t* frame, int width, int height)
{
    static uint32_t lastBatteryReadMs = 0;
    static int batteryLevel = -1;
    const uint32_t now = millis();
    if (batteryLevel < 0 || now - lastBatteryReadMs >= 5000)
    {
        lastBatteryReadMs = now;
        batteryLevel = (int)M5.Power.getBatteryLevel();
        if (batteryLevel > 100) batteryLevel = 100;
    }

    const int x0 = width - 88;
    const int y0 = 1;
    if (x0 < 0 || height < 14) return;

    for (int y = y0; y < y0 + 14; ++y)
        for (int x = x0; x < width; ++x)
            framePixel(frame, width, height, x, y, 0x0000);

    uint16_t color = 0x07E0;
    if (batteryLevel >= 0 && batteryLevel <= 15) color = 0xF800;
    else if (batteryLevel >= 0 && batteryLevel <= 35) color = 0xFFE0;

    if (nape_game_source_run_enabled())
    {
        const char* run = "RUN";
        for (int i = 0; i < 3; ++i)
            drawTinyChar(frame, width, height, x0 + 4 + i * 7, y0 + 2,
                         run[i], 0x07E0);
    }

    const int iconX = width - 52;
    const int iconY = y0 + 2;
    for (int x = 0; x < 22; ++x)
    {
        framePixel(frame, width, height, iconX + x, iconY, color);
        framePixel(frame, width, height, iconX + x, iconY + 9, color);
    }
    for (int y = 0; y < 10; ++y)
    {
        framePixel(frame, width, height, iconX, iconY + y, color);
        framePixel(frame, width, height, iconX + 21, iconY + y, color);
    }
    for (int y = 3; y < 7; ++y)
        for (int x = 22; x < 24; ++x)
            framePixel(frame, width, height, iconX + x, iconY + y, color);

    if (batteryLevel >= 0)
    {
        const int fillW = (18 * batteryLevel) / 100;
        for (int y = 2; y < 8; ++y)
            for (int x = 0; x < fillW; ++x)
                framePixel(frame, width, height, iconX + 2 + x, iconY + y, color);
    }

    char text[5];
    if (batteryLevel >= 0) snprintf(text, sizeof(text), "%d%%", batteryLevel);
    else snprintf(text, sizeof(text), "--");
    int len = 0;
    while (text[len]) ++len;
    int textX = width - len * 7;
    for (int i = 0; i < len; ++i)
        drawTinyChar(frame, width, height, textX + i * 7, y0 + 2, text[i], color);
}

extern "C" void core2_platform_init(void)
{
    // Deselect USB before M5 initializes the shared LCD/SPI bus.
    digitalWrite(NAPETUNE_USB_CS_PIN, HIGH);
    pinMode(NAPETUNE_USB_CS_PIN, OUTPUT);
    auto cfg = M5.config();
#if defined(NAPETUNE_BOARD_CORES3_LITE)
    cfg.external_display_value = 0;
    cfg.internal_spk = false;
    cfg.internal_mic = false;
    cfg.internal_imu = false;
#endif
    M5.begin(cfg);
#if defined(NAPETUNE_BOARD_CORES3_LITE)
    M5.setTouchButtonHeight(20);
#endif

    s_glass2State = new Glass2DisplayState();
    s_glass2MessageQueue = xQueueCreate(16, sizeof(Glass2Message));

    M5.Display.setRotation(1);
    M5.Display.setBrightness(220);

    // Doom palette is converted to host-order RGB565 uint16_t values.
    // M5GFX pushImage() needs byte swapping enabled for this format.
    M5.Display.setSwapBytes(true);

    M5.Display.fillScreen(BLACK);

    Serial.begin(115200);
    delay(100);
    loadConfig();

    Serial.println();
    Serial.println("NAPETUNE GAME MODE v0.5");
    Serial.println("PROTOTYPE / PURE ARDUINO");

    s_rgbFrame = (uint16_t*)heap_caps_malloc(
        320 * 240 * sizeof(uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );

    Serial.printf(
        "[CORE2] framebuffer=%s free_psram=%u\n",
        s_rgbFrame ? "OK" : "FAILED",
        (unsigned)ESP.getFreePsram()
    );

    M5.Display.setTextSize(1);
    M5.Display.setTextColor(DARKGREY, BLACK);
    M5.Display.setCursor(10, 10);
    M5.Display.print("NAPETUNE // GAME SUBSYSTEM");

    M5.Display.setTextSize(2);
    M5.Display.setTextColor(CYAN, BLACK);
    M5.Display.setCursor(20, 42);
    M5.Display.print("NAPETUNE");

    M5.Display.setTextSize(1);
    M5.Display.setTextColor(GREEN, BLACK);
    M5.Display.setCursor(20, 76);
    M5.Display.print("INITIALIZING CONTROLS...");

    // Disable MAX3421E on the shared SPI bus before SD init.
    pinMode(NAPETUNE_USB_CS_PIN, OUTPUT);
    digitalWrite(NAPETUNE_USB_CS_PIN, HIGH);
    Serial.printf("[USB PIN] CS=%d INT=%d GLASS2=DEFERRED\n",
                  NAPETUNE_USB_CS_PIN, napetune_board::pins.usb_int);
    delay(10);

#if defined(NAPETUNE_BOARD_CORES3_LITE)
    // G35 is LCD DC and shared SPI MISO. USB must initialize before SD.
    M5.Display.waitDMA();
    pinMode(35, INPUT);
#else
    napetune_spi_acquire();

    // Explicit Core2 routing before SD.begin(); do not rely on defaults.
    SPI.begin(18, 38, 23, -1);

    s_sdReady = SD.begin(
        4,
        SPI,
        20000000
    );

    napetune_spi_release();

    M5.Display.setCursor(20, 100);
    M5.Display.setTextColor(
        s_sdReady ? GREEN : RED,
        BLACK
    );
    M5.Display.print(
        s_sdReady ? "SD CARD // ONLINE"
                  : "SD CARD // ERROR"
    );

    Serial.printf(
        "[CORE2] SD=%s\n",
        s_sdReady ? "ONLINE" : "ERROR"
    );

#endif

}

extern "C" void core2_sd_begin(void)
{
#if defined(NAPETUNE_BOARD_CORES3_LITE)
    napetune_spi_acquire();
    pinMode(napetune_board::pins.miso, INPUT);
    SPI.begin(napetune_board::pins.sck, napetune_board::pins.miso,
              napetune_board::pins.mosi, -1);
    s_sdReady = SD.begin(napetune_board::pins.sd_cs, SPI, 10000000);
    napetune_spi_release();
    Serial.printf("[CORES3] SD=%s\n", s_sdReady ? "ONLINE" : "ERROR");
#endif
}

extern "C" int core2_sd_ready(void)
{
    return s_sdReady ? 1 : 0;
}

extern "C" void core2_video_wait_finish(void)
{
}

extern "C" void core2_video_send(
    const uint8_t* indexed,
    const uint16_t* palette,
    int width,
    int height
)
{
    if (!indexed || !palette || !s_rgbFrame)
    {
        return;
    }

    if (s_configActive)
    {
        static int lastOrientation = -2;
        const int orientation = nape_game_source_get_orientation();
        if (s_configDirty || orientation != lastOrientation)
        {
            napetune_lcd_acquire();
            drawConfigScreen();
            napetune_lcd_release();
            s_configDirty = false;
            lastOrientation = orientation;
        }
        return;
    }

    if (width > 320) width = 320;
    if (height > 240) height = 240;

    const int count = width * height;

    for (int i = 0; i < count; ++i)
    {
        s_rgbFrame[i] = palette[indexed[i]];
    }

    // Composite the HUD into the same RGB565 frame. LCD receives one image,
    // so the game frame and battery indicator can never alternate visibly.
    drawPowerOverlay(s_rgbFrame, width, height);
    drawReticle(s_rgbFrame, width, height);

    const int y = (height < 240)
        ? (240 - height) / 2
        : 0;

    static int lastLetterboxY = -1;
    static int lastLetterboxHeight = -1;

    napetune_lcd_acquire();

    if (
        height < 240 &&
        (y != lastLetterboxY || height != lastLetterboxHeight)
    )
    {
        M5.Display.fillRect(0, 0, 320, y, BLACK);
        M5.Display.fillRect(
            0,
            y + height,
            320,
            240 - (y + height),
            BLACK
        );
        lastLetterboxY = y;
        lastLetterboxHeight = height;
    }
    else if (height >= 240)
    {
        lastLetterboxY = y;
        lastLetterboxHeight = height;
    }

    M5.Display.pushImage(
        0,
        y,
        width,
        height,
        s_rgbFrame
    );

    napetune_lcd_release();
}
