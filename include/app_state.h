#pragma once

#include "Arduino.h"
#include <TFT_eSPI.h>

constexpr int MAX_SCHEDULE_ENTRIES = 32;
constexpr int MAX_VISIBLE_SCHEDULE_ENTRIES = 3;

extern String tformat;
extern String ScheduleEntries[MAX_SCHEDULE_ENTRIES];
extern TFT_eSPI tft;
extern uint16_t scheduleTextColor;
extern int photoResistorBrightRaw;
extern int photoResistorDarkRaw;
extern int photoDimSteps;
extern int photoDimDeadzone;
extern int photoDimTargetStep;
extern int photoDimTargetBrightness;
extern int brightness;
extern int mindim;
extern int maxdim;
extern int hourspan;
extern String sunrise_time;
extern String sunset_time;
extern unsigned long next_auto_dim_ms;
extern unsigned long auto_dim_resume_ms;
extern unsigned long next_photoresistor_log_ms;
extern int autodim_hold_ms;
extern int autodim_step_ms;
extern int autodim_percent;
extern bool autodim_debug;
extern unsigned long next_autodim_debug_ms;
extern uint16_t statusTextColor;
extern uint16_t statusBgColor;
extern uint16_t bootTextColor;
extern String system_id;
extern int system_id_clear_pixel_width;
extern String build_version_code;
extern bool ram_only_mode;
