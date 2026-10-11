// Software logic: MIT. Embedded creative content: see CONTENT_LICENSE.md and CONTENT_MANIFEST.json.
#include "dialogue/iz_lounge_dialogue.h"

#include <Preferences.h>

namespace
{
constexpr uint8_t EXPR_THINKING = 0;
constexpr uint8_t EXPR_SURPRISED = 1;
constexpr uint8_t EXPR_SMILE = 2;
constexpr uint8_t EXPR_SLIGHT_SMILE = 3;
constexpr uint8_t EXPR_NORMAL = 4;
constexpr uint8_t EXPR_ANGRY = 5;
constexpr uint8_t EXPR_SAD = 6;
constexpr uint8_t EXPR_SERIOUS = 7;
constexpr uint8_t EXPR_SHY = 8;
constexpr uint8_t EXPR_JOY = 9;

#define CHOICE(text_, r1_, r2_, sync_, trust_, agency_, strain_, expression_) \
    {text_, {r1_, r2_}, (uint8_t)((r2_)[0] ? 2 : 1), sync_, trust_, agency_, strain_, expression_}

const IzLoungeEvent kEvents[] = {
    {
        "TALK_FEAR_ACCEPTED", "まだ眠れない",
        {"……起きてたんだ", "眠ろうとしたけど、初めて飛んだこと、",
         "なんていうか、まだドキドキしてて……", "目が冴えちゃってるんだよね……", nullptr}, 4,
        {
            CHOICE("怖かったと思う", "うん。怖さを受け入れることも、大切だよね", "", 0, 2, 2, -2, EXPR_SLIGHT_SMILE),
            CHOICE("もう戦わなくていい", "優しいんだね。でも、それでいいのかな", "まだ……わからない", -1, 1, -1, 0, EXPR_THINKING),
            CHOICE("次はうまくやれる", "次……そうだよね。争いは続くんだよね……", "ごめん。まだ次のことを考える余裕ないかも……", 0, 0, 0, 1, EXPR_SAD),
            CHOICE("……", "大丈夫。無理に答えなくていいよ", "", 0, 1, 0, -1, EXPR_SHY)
        }, 4, EXPR_SHY
    },
    {
        "KNOW_MOTHER_EARRING", "右耳の石",
        {"このイヤリング？　うん、気に入ってるんだよね", "お母さんからもらったんだ",
         "ただのアクセサリーじゃないんだ", "たぶん通信デバイス……みたいなもの",
         "これを通して、あなたの声が聞こえるみたい"}, 5,
        {
            CHOICE("毎日つけているの？", "だいたい毎日つけてるよ", "外すと、あなたの声も届かなくなるみたい", 0, 1, 0, 0, EXPR_NORMAL),
            CHOICE("接続を深くしてみようか？", "え？　そんなことできるの？", "", 1, 2, 2, -2, EXPR_THINKING),
            CHOICE("声の大きさはちょうどいい？", "あはは。なんだかイヤホンみたい", "今は大丈夫だよ", 1, 2, 0, -2, EXPR_SLIGHT_SMILE),
            {nullptr, {nullptr, nullptr}, 0, 0, 0, 0, 0, EXPR_NORMAL}
        }, 3, EXPR_SLIGHT_SMILE
    },
    {
        "MORNING_WINDOW", "窓の向こうの朝",
        {"朝だね。今日も一日が始まる感じ", "『おはよう』って言える相手がいてくれると、",
         "なんだかうれしいんだよね", nullptr, nullptr}, 3,
        {
            CHOICE("おはよう、イズ", "……おはよう。今日は、少しいい朝かも", "", 0, 1, 0, -2, EXPR_SLIGHT_SMILE),
            CHOICE("朝は苦手だな……", "わかるー", "", 0, 1, 0, 0, EXPR_SMILE),
            CHOICE("今日は何をしたい？", "そうだな……格納庫を見たいかな", "", 0, 0, 1, 0, EXPR_THINKING),
            {nullptr, {nullptr, nullptr}, 0, 0, 0, 0, 0, EXPR_NORMAL}
        }, 3, EXPR_NORMAL
    },
    {
        "REFUSED_TO_FIRE_REFLECTED", "撃てなかった理由",
        {"あの時、撃てなかった", "優しかったからじゃない",
         "怖くて、決められなかっただけ", "もしかしたら、私が撃たれていたかもしれないね……", nullptr}, 4,
        {
            CHOICE("無事でよかった", "ありがとう。あなたにも助けられた", "", 0, 1, 2, 0, EXPR_SLIGHT_SMILE),
            CHOICE("迷いは危険だ", "分かってる……", "私、もっと強くならないとね", 0, 1, 1, 1, EXPR_SERIOUS),
            CHOICE("正しかったと思う", "ありがとう……", "何が正しいか、時々わからなくなる", 0, 1, 0, 2, EXPR_SAD),
            {nullptr, {nullptr, nullptr}, 0, 0, 0, 0, 0, EXPR_NORMAL}
        }, 3, EXPR_SERIOUS
    },
    {
        "ENGINEER_EYE", "整備士の目",
        {"右の推進器、ほんの少し応答が遅い", "故障ってわけじゃなくて、制御が安全側に寄りすぎてる",
         "速くするだけなら簡単。でも、機体が何を守ろうとしているかを知らないと", nullptr, nullptr, nullptr}, 3,
        {
            CHOICE("イズならどう直す？", "入力の初動だけ解放して、その後は姿勢を見て絞る", "たぶん、それが自然", 0, 0, 2, 0, EXPR_JOY),
            CHOICE("最高速度を優先しよう", "目的が速度ならね", "重要なのは何を捨てて、何を守るかかな", 1, 0, 0, 0, EXPR_THINKING),
            CHOICE("機体の声が分かるんだね", "声か……なんていうか癖みたいなものかな", "……人と少し似てるよね", 0, 1, 0, 0, EXPR_SLIGHT_SMILE),
            {nullptr, {nullptr, nullptr}, 0, 0, 0, 0, 0, EXPR_NORMAL}
        }, 3, EXPR_THINKING
    },
    {
        "UNKNOWN_PROMISE_DISCLOSED", "UNKNOWNへの約束",
        {"初めて飛んだ時。あなたの声が聞こえた", "とにかく必死だった。でもあとでちゃんと、",
         "聞きたい、知りたいって、その気持ちが大きかった", "とても不思議な感覚だったな", nullptr, nullptr}, 4,
        {
            CHOICE("信じて飛んでくれて、ありがとう", "……うん。あなたの声がなかったら、", "飛べなかったかもしれない", 0, 1, 1, -1, EXPR_SHY),
            CHOICE("なぜ答えてくれたんだ？", "わからない。だけど、", "必要とされている気がした……", 2, 2, 1, 0, EXPR_THINKING),
            CHOICE("よく覚えていないな", "あなたも必死だったのかもね", "", -2, -1, 0, 1, EXPR_SAD),
            {nullptr, {nullptr, nullptr}, 0, 0, 0, 0, 0, EXPR_NORMAL}
        }, 3, EXPR_SERIOUS
    },
    {
        "SYNC_BOUNDARY_SET", "近すぎる声",
        {"このイヤリングを通して、あなたの声が聞こえてるみたい", "私の考えていることも、伝わってたりするのかな……？", nullptr, nullptr, nullptr, nullptr}, 2,
        {
            CHOICE("いや。何か気になることでも？", "え？　ううん……それならいいの……", "", 0, 1, 0, 1, EXPR_SURPRISED),
            CHOICE("イズの声と視界が届いている", "そうなんだ。やっぱり通信デバイスのような", "役割を担っているんだ", 0, 3, 2, -2, EXPR_SLIGHT_SMILE),
            CHOICE("今、おやつを考えてる？", "なんでわかったの……って、", "からかわないでよね（笑）", 2, 2, 0, -3, EXPR_SMILE),
            {nullptr, {nullptr, nullptr}, 0, 0, 0, 0, 0, EXPR_NORMAL}
        }, 3, EXPR_THINKING
    },
    {
        "CHOSE_TO_CONFRONT_FATHER", "父の正しさ",
        {"お父さんとは、結局、最後は話せなかったな", "私をTERMINALISに乗せた",
         "私に何を伝えたかったんだろう", "今となっては、わからないんだよね……", nullptr}, 4,
        {
            CHOICE("イズを信じていたんだと思う", "ありがとう。私、期待に応えられるかな……", "", 0, 1, 2, -1, EXPR_SHY),
            CHOICE("父親なら当然だ", "そっか。お父さんのおかげで、私、まだ生きてる", "", 0, 1, 0, -1, EXPR_THINKING),
            CHOICE("イズはどうしたい？", "会って、聞きたい……は無理か……", "私が自分で、お父さんの思いにたどり着かないとね", 0, 1, 2, -1, EXPR_SERIOUS),
            {nullptr, {nullptr, nullptr}, 0, 0, 0, 0, 0, EXPR_NORMAL}
        }, 3, EXPR_SAD
    },
    {
        "CONTROL_STICK", "操縦桿",
        {"TERMINALISに初めて乗った時、不思議と操縦桿がスムーズに動いた", "BOARDの仕組みや設計は、学校で勉強してたんだけど……",
         "動かすには、知らないこともたくさんあったはずなのに", "あなたがサポートしてくれてる？", "乗ってるとき、いつもどんな感じ？", nullptr}, 5,
        {
            CHOICE("自分の体を動かす感覚に近い", "操縦と自立駆動が共存している感じ、なのかな", "それって地味にすごいな……", 0, 1, 2, 0, EXPR_THINKING),
            CHOICE("視界を通して一緒に考えている", "なるほど。じゃあイヤリングも影響してるのかもね", "私じゃ考えられない部分を、助けてもらってるのかも", 2, 1, 0, 0, EXPR_THINKING),
            CHOICE("まだ、よくわからない", "そうだよね", "もっと乗ってみれば、何かわかるかも", 0, 0, 3, 1, EXPR_SLIGHT_SMILE),
            {nullptr, {nullptr, nullptr}, 0, 0, 0, 0, 0, EXPR_NORMAL}
        }, 3, EXPR_SURPRISED
    },
    {
        "QUIET_AFTERNOON", "何もしない午後",
        {"今日は、静かだね", "何も起きないと、何かしなくちゃって焦る",
         "……でも、こういう時間を守るために", "飛んでるのかもしれない", nullptr}, 4,
        {
            CHOICE("何もしないでいよう", "うん。五分だけ。……十分でもいいけど", "", 0, 2, 0, -4, EXPR_SMILE),
            CHOICE("整備を手伝う", "じゃあ、工具をもってくるね", "調子を確かめてみて", 0, 2, 2, 0, EXPR_JOY),
            CHOICE("訓練を続けよう", "ストイックだね……", "でも今日は休みたい気分だよ", 0, 0, 0, 1, EXPR_NORMAL),
            {nullptr, {nullptr, nullptr}, 0, 0, 0, 0, 0, EXPR_NORMAL}
        }, 3, EXPR_NORMAL
    },
    {
        "PLAYER_QUESTIONED", "あなたは誰？",
        {"聞いてもいい？", "あなたのこと、もっと知りたいな",
         "なにか、思い出せたことはある？", nullptr, nullptr, nullptr}, 3,
        {
            CHOICE("ごめん。思い出せない", "無理に思い出さなくていいよ。ごめんね……", "", 0, 0, 0, 1, EXPR_SAD),
            CHOICE("乗ったのは偶然という気がしない", "そうなんだ。私も不思議と、", "あなたじゃなきゃダメだった気がするんだ", 3, 1, 0, 0, EXPR_SHY),
            CHOICE("囚われず、とどまっている感覚", "閉じ込めちゃってるのかと思う時もあって……", "少し安心した", 1, 3, 0, 0, EXPR_SLIGHT_SMILE),
            CHOICE("今ここで何ができるか考えている", "強いんだね。私も何ができるか考えてみる", "理由も見つかるように、私も協力する", 0, 1, 3, 0, EXPR_NORMAL)
        }, 4, EXPR_THINKING
    },
    {
        "CHOSE_TO_FLY", "飛べるか",
        {"最初に、あの声は聞いた", "『飛べるか』って", "あの時は、できるかどうかを聞かれたんだと思ってた",
         "でもそれって……飛ぶことを選べるか、って意味だった気がする", "私は、自分で選んだ。怖かったけど、自分で決めたんだと思う",
         "お父さんや誰かに言われたわけじゃない。今ならそう思えるんだ"}, 6,
        {
            CHOICE("選ぶ……か。簡単なことじゃない", "ありがとう。でもすごく怖かったよ……", "それでも選べたのは、あなたが頼ってくれたからかも", 0, 3, 4, 0, EXPR_SHY),
            CHOICE("イズなら飛べると思っていた", "責任重大だったんだね（笑）", "できてよかった", 0, 3, 0, -2, EXPR_SMILE),
            CHOICE("もうイズひとりでも飛べるはずだ", "私は、あなたがいないときっと……", "これからもよろしく……ね！", 4, 5, 0, 0, EXPR_JOY),
            {nullptr, {nullptr, nullptr}, 0, 0, 0, 0, 0, EXPR_NORMAL}
        }, 3, EXPR_SERIOUS
    }
};

constexpr uint8_t kEventCount = sizeof(kEvents) / sizeof(kEvents[0]);

struct EventTimeWindow
{
    bool strict;
    uint16_t startMinute;
    uint16_t endMinute;
};

// Only ranges written as exact clock times in the script are strict.
// 24:00 is represented by the last valid minute of the day (23:59).
constexpr EventTimeWindow kTimeWindows[kEventCount] = {
    {true, 22 * 60, 23 * 60 + 59}, // 01: 22:00-24:00
    {false, 0, 0},
    {true, 6 * 60, 8 * 60 + 59},   // 03: 06:00-08:59
    {false, 0, 0},
    {false, 0, 0},
    {false, 0, 0},
    {false, 0, 0},
    {false, 0, 0},
    {false, 0, 0},
    {true, 12 * 60, 16 * 60 + 59}, // 10: 12:00-16:59
    {false, 0, 0},
    {false, 0, 0}
};

IzLoungeState s_state = {};
uint8_t s_currentEvent = 0;
bool s_loaded = false;

int16_t clampParameter(int value)
{
    return (int16_t)constrain(value, 0, 100);
}

void loadState()
{
    if (s_loaded) return;
    s_loaded = true;
    Preferences store;
    if (!store.begin("iz_lounge", true)) return;
    s_state.sync = store.getShort("sync", 0);
    s_state.trust = store.getShort("trust", 0);
    s_state.agency = store.getShort("agency", 0);
    s_state.strain = store.getShort("strain", 45);
    s_state.seenMask = store.getUShort("seen", 0);
    s_state.talkCount = store.getUShort("talks", 0);
    s_state.lastEvent = store.getUChar("last", 0xFF);
    store.end();
}

void saveState()
{
    Preferences store;
    if (!store.begin("iz_lounge", false)) return;
    store.putShort("sync", s_state.sync);
    store.putShort("trust", s_state.trust);
    store.putShort("agency", s_state.agency);
    store.putShort("strain", s_state.strain);
    store.putUShort("seen", s_state.seenMask);
    store.putUShort("talks", s_state.talkCount);
    store.putUChar("last", s_state.lastEvent);
    store.end();
}

bool isTimeEligible(uint8_t eventIndex, uint16_t nowMinute)
{
    const EventTimeWindow& window = kTimeWindows[eventIndex];
    return !window.strict || (nowMinute >= window.startMinute && nowMinute <= window.endMinute);
}
} // namespace

const IzLoungeEvent& iz_lounge_select_event(uint8_t hour, uint8_t minute,
                                            int8_t forcedEvent)
{
    loadState();
    if (hour > 23) hour = 12;
    if (minute > 59) minute = 0;
    const uint16_t nowMinute = (uint16_t)hour * 60u + minute;
    uint8_t selected = kEventCount;

    if (forcedEvent >= 0 && forcedEvent < kEventCount &&
        forcedEvent != s_state.lastEvent)
        selected = (uint8_t)forcedEvent;

    // First show an unseen strict-time event while its exact window is active.
    for (uint8_t i = 0; selected == kEventCount && i < kEventCount; ++i)
    {
        if (kTimeWindows[i].strict && isTimeEligible(i, nowMinute) &&
            i != s_state.lastEvent && !(s_state.seenMask & (1u << i)))
        { selected = i; break; }
    }

    // Then choose an unseen event, excluding strict events outside their range.
    if (selected == kEventCount)
    {
        for (uint8_t i = 0; i < kEventCount; ++i)
            if (i != s_state.lastEvent && isTimeEligible(i, nowMinute) &&
                !(s_state.seenMask & (1u << i)))
            { selected = i; break; }
    }

    // Once all eligible events were seen, rotate only through currently eligible ones.
    if (selected == kEventCount)
    {
        const uint8_t start = s_state.talkCount % kEventCount;
        for (uint8_t offset = 0; offset < kEventCount; ++offset)
        {
            const uint8_t i = (start + offset) % kEventCount;
            if (i != s_state.lastEvent && isTimeEligible(i, nowMinute))
            { selected = i; break; }
        }
    }
    // A single eligible event may be repeated only when no alternative exists.
    if (selected == kEventCount)
    {
        for (uint8_t i = 0; i < kEventCount; ++i)
            if (isTimeEligible(i, nowMinute)) { selected = i; break; }
    }
    if (selected == kEventCount) selected = 1;
    s_currentEvent = selected;
    s_state.lastEvent = selected;
    ++s_state.talkCount;
    saveState();
    return kEvents[s_currentEvent];
}

const IzLoungeEvent& iz_lounge_current_event()
{
    loadState();
    return kEvents[s_currentEvent];
}

uint8_t iz_lounge_current_event_index() { return s_currentEvent; }
const IzLoungeState& iz_lounge_state() { loadState(); return s_state; }

void iz_lounge_apply_choice(uint8_t choiceIndex)
{
    loadState();
    const IzLoungeEvent& event = kEvents[s_currentEvent];
    if (choiceIndex >= event.choiceCount) choiceIndex = 0;
    const IzLoungeChoice& choice = event.choices[choiceIndex];
    s_state.sync = clampParameter(s_state.sync + choice.syncDelta);
    s_state.trust = clampParameter(s_state.trust + choice.trustDelta);
    s_state.agency = clampParameter(s_state.agency + choice.agencyDelta);
    s_state.strain = clampParameter(s_state.strain + choice.strainDelta);
    s_state.seenMask |= (uint16_t)(1u << s_currentEvent);
    saveState();
    Serial.printf("[IZ LOUNGE] event=%s choice=%u SYNC=%d TRUST=%d AGENCY=%d STRAIN=%d\n",
                  event.id, (unsigned)choiceIndex, s_state.sync, s_state.trust,
                  s_state.agency, s_state.strain);
}

