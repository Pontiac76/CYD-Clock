#pragma once

#include "Arduino.h"

constexpr unsigned long MINUTE_MS = 60UL * 1000UL;
constexpr int MAX_NTP_RANDOM_DELAY_SECONDS = 24 * 60 * 60;

bool wifi_start_STA();
bool timesync(bool drawStatus = true);
unsigned long computeNtpDelayMs(int baseMinutes, int randomDelaySeconds);
void scheduleNextNtpSync(bool lastSyncSucceeded);
void processScheduledNtpSync();
String build_update_request_url();
String build_update_request_url_for_system_id(const String &id);
void ensure_default_update_url();
void preload_all_cached_configs_from_server();
bool bootstrap_config_from_server();
void apply_runtime_NTP_config();
bool poll_update_server();
