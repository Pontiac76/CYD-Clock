#pragma once

#include "Arduino.h"
#include <TFT_eSPI.h>

constexpr int MAX_SCHEDULE_ENTRIES = 32;
constexpr int MAX_VISIBLE_SCHEDULE_ENTRIES = 3;

extern String tformat;
extern String ScheduleEntries[MAX_SCHEDULE_ENTRIES];
extern TFT_eSPI tft;
uint16_t createColor(uint8_t r, uint8_t g, uint8_t b);
