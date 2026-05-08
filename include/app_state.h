#pragma once

#include "Arduino.h"
#include <TFT_eSPI.h>

constexpr int MAX_SCHEDULE_ENTRIES = 32;
constexpr int MAX_VISIBLE_SCHEDULE_ENTRIES = 3;

extern String ssid;
extern String password;
extern String tzinfo;
extern String tformat;
extern String ntpserver;
extern String WeekDays[];
extern String MonthName[];
extern String ScheduleEntries[MAX_SCHEDULE_ENTRIES];
extern TFT_eSPI tft;
extern uint16_t clockTextColor;
extern uint16_t dateTextColor;
extern uint16_t dateBgColor;
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
extern uint16_t errorTextColor;
extern String updateurl;
extern String current_config_text;
extern String system_id;
extern String system_id_list[];
extern int system_id_count;
extern int active_system_id_index;
extern int system_id_clear_pixel_width;
extern String cached_config_texts[];
extern bool cached_config_loaded[];
extern String config_source_state[];
extern int next_update_modular;
extern int ntp_sync_frequency_minutes;
extern int ntp_sync_random_delay_seconds;
extern int ntp_retry_frequency_minutes;
extern int ntp_retry_random_delay_seconds;
extern unsigned long next_ntp_sync_ms;
extern bool ntp_sync_scheduled;
extern int event_tm_hour;
extern int event_tm_min;
extern int event_tm_sec;
extern String build_version_code;
extern bool ram_only_mode;
