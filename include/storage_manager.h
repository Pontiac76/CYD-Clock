#pragma once

#include "Arduino.h"

bool detect_sd_available_at_boot();
bool begin_sd_session();
void end_sd_session();
bool ensureLittleFsMounted();

bool read_text_file_from_sd(const String &path, String &content);
bool write_text_file_to_sd(const String &path, const String &content);
bool read_config_text_from_sd(String &content);
bool write_config_to_sd(String content);

bool read_file_text_from_littlefs(const char *path, String &content);
bool readLittleFsTextMounted(const char *path, String &content);
bool writeLittleFsTextMounted(const char *path, const String &content);
bool read_config_text_from_littlefs(String &content);

void list_sd_files_to_serial();
void list_littlefs_files_to_serial();
