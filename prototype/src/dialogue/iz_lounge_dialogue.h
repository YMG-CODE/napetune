#pragma once

#include <Arduino.h>

constexpr uint8_t IZ_LOUNGE_MAX_OPENING_LINES = 6;
constexpr uint8_t IZ_LOUNGE_MAX_CHOICES = 4;
constexpr uint8_t IZ_LOUNGE_MAX_REPLY_LINES = 2;

struct IzLoungeChoice
{
    const char* text;
    const char* replies[IZ_LOUNGE_MAX_REPLY_LINES];
    uint8_t replyCount;
    int8_t syncDelta;
    int8_t trustDelta;
    int8_t agencyDelta;
    int8_t strainDelta;
    uint8_t expression;
};

struct IzLoungeEvent
{
    const char* id;
    const char* title;
    const char* opening[IZ_LOUNGE_MAX_OPENING_LINES];
    uint8_t openingCount;
    IzLoungeChoice choices[IZ_LOUNGE_MAX_CHOICES];
    uint8_t choiceCount;
    uint8_t openingExpression;
};

struct IzLoungeState
{
    int16_t sync;
    int16_t trust;
    int16_t agency;
    int16_t strain;
    uint16_t seenMask;
    uint16_t talkCount;
    uint8_t lastEvent;
};

const IzLoungeEvent& iz_lounge_select_event(uint8_t hour, uint8_t minute,
                                            int8_t forcedEvent = -1);
const IzLoungeEvent& iz_lounge_current_event();
uint8_t iz_lounge_current_event_index();
const IzLoungeState& iz_lounge_state();
void iz_lounge_apply_choice(uint8_t choiceIndex);

