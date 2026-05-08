#pragma once

#include "Arduino.h"

String sanitizeConfigKey(String key);
bool configKeyEquals(const String &normalizedKey, const char *expectedKey);
String sanitizeSystemId(String value);
String buildSystemIdFileText(String rawValue);
void parseSystemIdList(String rawText);
bool advanceActiveSystemId();
bool configContentEqualsNormalized(const String &left, const String &right);
String get_config_cache_path_for_id(const String &id);
void parseConfigLine(String line);
void apply_config_from_string(String content);
void apply_current_config_with_runtime_state();
void load_cached_config_for_index_from_storage(int index, bool allowLegacyFallback);
bool sync_config_to_sd_and_memory(String newContent, bool &changed);
void read_sd();
void read_system_id();
