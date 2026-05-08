#pragma once

#include "Arduino.h"
#include <TFT_eSPI.h>

constexpr int MAX_SCHEDULE_ENTRIES = 32;
constexpr int MAX_VISIBLE_SCHEDULE_ENTRIES = 3;

extern String tformat;
extern String ScheduleEntries[MAX_SCHEDULE_ENTRIES];
extern TFT_eSPI tft;
extern uint16_t scheduleTextColor;
extern uint16_t statusTextColor;
extern uint16_t statusBgColor;
extern uint16_t bootTextColor;
extern String system_id;
extern int system_id_clear_pixel_width;
extern String build_version_code;
extern bool ram_only_mode;
