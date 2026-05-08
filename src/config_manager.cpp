#include "config_manager.h"

#include "app_state.h"
#include "brightness_manager.h"
#include "display_manager.h"
#include "network_manager.h"
#include "schedule_display.h"
#include "storage_manager.h"

#include <WiFi.h>

constexpr int WEEKDAY_COUNT = 7;
constexpr int MONTH_COUNT = 12;
constexpr int MAX_TRANSLATION_LENGTH = 24;
constexpr int MAX_SYSTEM_ID_COUNT = 16;
constexpr const char *SETUP_WIFI_PATH = "/wifi.txt";

String ssid;
String password;
String tzinfo;
String tformat;
String ntpserver;
String updateurl;
String WeekDays[WEEKDAY_COUNT] = {
  "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"
};
String MonthName[MONTH_COUNT] = {
  "January", "February", "March", "April", "May", "June",
  "July", "August", "September", "October", "November", "December"
};
String ScheduleEntries[MAX_SCHEDULE_ENTRIES];
String current_config_text;
String system_id;
String system_id_list[MAX_SYSTEM_ID_COUNT];
int system_id_count = 0;
int active_system_id_index = 0;
int system_id_clear_pixel_width = 0;
String cached_config_texts[MAX_SYSTEM_ID_COUNT];
bool cached_config_loaded[MAX_SYSTEM_ID_COUNT] = { false };
String config_source_state[MAX_SYSTEM_ID_COUNT];

String sanitizeTranslationToken(String token)
{
  token.replace("\r", "");
  token.replace("\n", "");
  token.trim();

  if ((token.startsWith("\"") && token.endsWith("\"")) ||
      (token.startsWith("'") && token.endsWith("'")))
  {
    token = token.substring(1, token.length() - 1);
    token.trim();
  }

  if (token.length() > MAX_TRANSLATION_LENGTH)
  {
    token = token.substring(0, MAX_TRANSLATION_LENGTH);
  }

  return token;
}

String sanitizeConfigKey(String key)
{
  key.replace("\r", "");
  key.replace("\n", "");
  key.trim();
  key.toLowerCase();
  return key;
}

bool configKeyEquals(const String &normalizedKey, const char *expectedKey)
{
  String normalizedExpected = expectedKey;
  normalizedExpected.toLowerCase();
  return normalizedKey == normalizedExpected;
}

bool parseColorConfigValue(String value, uint16_t &color)
{
  unsigned long rawColor;
  int firstSeparator;
  int secondSeparator;
  int red;
  int green;
  int blue;

  value.replace("\r", "");
  value.replace("\n", "");
  value.trim();

  firstSeparator = value.indexOf(',');
  if (firstSeparator != -1)
  {
    secondSeparator = value.indexOf(',', firstSeparator + 1);
    if (secondSeparator == -1)
    {
      return false;
    }

    red = value.substring(0, firstSeparator).toInt();
    green = value.substring(firstSeparator + 1, secondSeparator).toInt();
    blue = value.substring(secondSeparator + 1).toInt();
    if ((red < 0) || (red > 255) || (green < 0) || (green > 255) || (blue < 0) || (blue > 255))
    {
      return false;
    }

    color = createColor(red, green, blue);
    return true;
  }

  if (value.startsWith("#"))
  {
    value = value.substring(1);
  }
  else if (value.startsWith("0x") || value.startsWith("0X"))
  {
    value = value.substring(2);
  }

  if (value.length() != 6)
  {
    return false;
  }

  rawColor = strtoul(value.c_str(), nullptr, 16);
  red = (rawColor >> 16) & 0xFF;
  green = (rawColor >> 8) & 0xFF;
  blue = rawColor & 0xFF;
  color = createColor(red, green, blue);
  return true;
}

void normalizePhotoResistorRange()
{
  photoResistorBrightRaw = constrain(photoResistorBrightRaw, 0, 4095);
  photoResistorDarkRaw = constrain(photoResistorDarkRaw, 0, 4095);
  photoDimTargetStep = -1;
  photoDimTargetBrightness = -1;

  if (photoResistorBrightRaw == photoResistorDarkRaw)
  {
    photoResistorDarkRaw = min(4095, photoResistorBrightRaw + 1);
  }
}

bool parsePhotoResistorRange(String value)
{
  int separatorIndex;
  int firstValue;
  int secondValue;

  value.replace("\r", "");
  value.replace("\n", "");
  value.trim();

  separatorIndex = value.indexOf(',');
  if (separatorIndex == -1)
  {
    separatorIndex = value.indexOf(':');
  }
  if (separatorIndex == -1)
  {
    separatorIndex = value.indexOf('-');
  }
  if (separatorIndex == -1)
  {
    return false;
  }

  firstValue = value.substring(0, separatorIndex).toInt();
  secondValue = value.substring(separatorIndex + 1).toInt();

  photoResistorBrightRaw = min(firstValue, secondValue);
  photoResistorDarkRaw = max(firstValue, secondValue);
  normalizePhotoResistorRange();
  photoDimTargetStep = -1;
  photoDimTargetBrightness = -1;
  return true;
}

String sanitizeSystemId(String value)
{
  String sanitized = "";

  for (int index = 0; index < value.length(); ++index)
  {
    char current = value.charAt(index);

    if (isalnum(static_cast<unsigned char>(current)))
    {
      sanitized += current;
    }
  }

  return sanitized;
}

String buildSystemIdFileText(String rawValue)
{
  String content;
  int start = 0;

  rawValue.replace("\r", "");
  rawValue.replace("\n", ";");

  while (start <= rawValue.length())
  {
    int end = rawValue.indexOf(';', start);
    String token;
    if (end == -1)
    {
      token = rawValue.substring(start);
      start = rawValue.length() + 1;
    }
    else
    {
      token = rawValue.substring(start, end);
      start = end + 1;
    }

    token = sanitizeSystemId(token);
    if (token == "")
    {
      continue;
    }

    if (content != "")
    {
      content += "\n";
    }
    content += token;
  }

  if (content != "")
  {
    content += "\n";
  }
  return content;
}

void clearSystemIdList()
{
  for (int index = 0; index < MAX_SYSTEM_ID_COUNT; ++index)
  {
    system_id_list[index] = "";
  }
  system_id_count = 0;
  active_system_id_index = 0;
  system_id = "";
}

bool addSystemIdIfUnique(String candidate)
{
  if (candidate == "")
  {
    return false;
  }

  for (int index = 0; index < system_id_count; ++index)
  {
    if (system_id_list[index] == candidate)
    {
      return false;
    }
  }

  if (system_id_count >= MAX_SYSTEM_ID_COUNT)
  {
    return false;
  }

  system_id_list[system_id_count] = candidate;
  ++system_id_count;
  return true;
}

int computeSystemIdClearPixelWidth()
{
  int maxWidth = tft.textWidth("no-id", 1);

  for (int index = 0; index < system_id_count; ++index)
  {
    int width = tft.textWidth(system_id_list[index], 1);
    if (width > maxWidth)
    {
      maxWidth = width;
    }
  }

  return maxWidth;
}

void refreshSystemIdClearPixelWidth()
{
  tft.setTextFont(1);
  tft.setTextSize(1);
  system_id_clear_pixel_width = computeSystemIdClearPixelWidth();
}

void parseSystemIdList(String rawText)
{
  int start = 0;
  clearSystemIdList();
  for (int index = 0; index < MAX_SYSTEM_ID_COUNT; ++index)
  {
    cached_config_texts[index] = "";
    cached_config_loaded[index] = false;
    config_source_state[index] = "UNSET";
  }

  while (start <= rawText.length())
  {
    int end = rawText.indexOf('\n', start);
    String line;
    if (end == -1)
    {
      line = rawText.substring(start);
      start = rawText.length() + 1;
    }
    else
    {
      line = rawText.substring(start, end);
      start = end + 1;
    }

    line.replace("\r", "");
    line.trim();
    if ((line == "") || line.startsWith("#"))
    {
      continue;
    }

    addSystemIdIfUnique(sanitizeSystemId(line));
  }

  if (system_id_count > 0)
  {
    active_system_id_index = 0;
    system_id = system_id_list[active_system_id_index];
  }

  refreshSystemIdClearPixelWidth();
}

bool setActiveSystemIdByIndex(int index)
{
  if ((system_id_count <= 0) || (index < 0) || (index >= system_id_count))
  {
    return false;
  }

  active_system_id_index = index;
  system_id = system_id_list[active_system_id_index];
  return true;
}

bool advanceActiveSystemId()
{
  if (system_id_count <= 1)
  {
    return false;
  }

  int nextIndex = active_system_id_index + 1;
  if (nextIndex >= system_id_count)
  {
    nextIndex = 0;
  }
  return setActiveSystemIdByIndex(nextIndex);
}

String normalizeConfigForCompare(const String &value)
{
  String normalized = "";
  normalized.reserve(value.length());

  for (int index = 0; index < value.length(); ++index)
  {
    char current = value.charAt(index);
    if (current == '\r')
    {
      continue;
    }
    normalized += static_cast<char>(tolower(static_cast<unsigned char>(current)));
  }

  return normalized;
}

bool configContentEqualsNormalized(const String &left, const String &right)
{
  return normalizeConfigForCompare(left) == normalizeConfigForCompare(right);
}

String get_config_cache_path_for_id(const String &id)
{
  return String("/config.txt.") + id;
}

bool stringArrayEquals(const String *left, const String *right, int count)
{
  for (int index = 0; index < count; ++index)
  {
    if (left[index] != right[index])
    {
      return false;
    }
  }

  return true;
}

void parseTranslationList(String value, String destination[], int destinationSize)
{
  int index = 0;
  int start = 0;

  while ((start <= value.length()) && (index < destinationSize))
  {
    int end = value.indexOf(',', start);
    String token;

    if (end == -1)
    {
      token = value.substring(start);
      start = value.length() + 1;
    }
    else
    {
      token = value.substring(start, end);
      start = end + 1;
    }

    token = sanitizeTranslationToken(token);
    if (token != "")
    {
      destination[index] = token;
    }
    ++index;
  }
}

// parse config.txt Lines to Var
void parseConfigLine(String line) 
{
  int separatorIndex = line.indexOf('=');
  if (separatorIndex == -1) return;

  String key = line.substring(0, separatorIndex);
  key = sanitizeConfigKey(key);
  String value = line.substring(separatorIndex + 1);
  int scheduleIndex = parseScheduleIndex(key);
  value.replace("\r", "");
  value.replace("\n", "");
  // Serial.print(key + F("="));
  // Serial.println(value);
  if (scheduleIndex != -1) {
    ScheduleEntries[scheduleIndex] = value;
  } else if (configKeyEquals(key, "ssid")) {
    ssid = value;
  } else if (configKeyEquals(key, "password")) {
    password = value;
  } else if (configKeyEquals(key, "tzinfo")) {
    tzinfo = value;
  } else if (configKeyEquals(key, "ntpserver")) {
    ntpserver = value;
  } else if (configKeyEquals(key, "ntpsyncminutes") ||
             configKeyEquals(key, "ntpfrequencyminutes")) {
    ntp_sync_frequency_minutes = int(max(1L, value.toInt()));
  } else if (configKeyEquals(key, "ntpsyncrandomseconds") ||
             configKeyEquals(key, "ntprandomdelayseconds")) {
    ntp_sync_random_delay_seconds = int(min(long(MAX_NTP_RANDOM_DELAY_SECONDS), max(0L, value.toInt())));
  } else if (configKeyEquals(key, "ntpretryminutes")) {
    ntp_retry_frequency_minutes = int(max(1L, value.toInt()));
  } else if (configKeyEquals(key, "ntpretryrandomseconds")) {
    ntp_retry_random_delay_seconds = int(min(long(MAX_NTP_RANDOM_DELAY_SECONDS), max(0L, value.toInt())));
  } else if (configKeyEquals(key, "tformat")) {
    tformat = value;
  } else if (configKeyEquals(key, "brightness")) {
    brightness = value.toInt();
  } else if (configKeyEquals(key, "mindim")) {
    mindim = value.toInt();
  } else if (configKeyEquals(key, "maxdim")) {
    maxdim = value.toInt();
  } else if (configKeyEquals(key, "clockcolor")) {
    if (parseColorConfigValue(value, clockTextColor))
    {
      event_tm_sec = -1;
    }
  } else if (configKeyEquals(key, "datecolor")) {
    if (parseColorConfigValue(value, dateTextColor))
    {
      event_tm_hour = -1;
    }
  } else if (configKeyEquals(key, "datebgcolor")) {
    if (parseColorConfigValue(value, dateBgColor))
    {
      event_tm_hour = -1;
    }
  } else if (configKeyEquals(key, "statuscolor")) {
    if (parseColorConfigValue(value, statusTextColor))
    {
      event_tm_hour = -1;
    }
  } else if (configKeyEquals(key, "statusbgcolor")) {
    if (parseColorConfigValue(value, statusBgColor))
    {
      event_tm_hour = -1;
    }
  } else if (configKeyEquals(key, "schedulecolor")) {
    if (parseColorConfigValue(value, scheduleTextColor))
    {
      event_tm_min = -1;
    }
  } else if (configKeyEquals(key, "bootcolor")) {
    parseColorConfigValue(value, bootTextColor);
  } else if (configKeyEquals(key, "errorcolor")) {
    parseColorConfigValue(value, errorTextColor);
  } else if (configKeyEquals(key, "photoresistorrange")) {
    if (!parsePhotoResistorRange(value))
    {
      Serial.print("Invalid photoresistorrange ignored: ");
      Serial.println(value);
    }
  } else if (configKeyEquals(key, "photoresistorbright") ||
             configKeyEquals(key, "photoresistorlow") ||
             configKeyEquals(key, "maxresistor")) {
    photoResistorBrightRaw = value.toInt();
    normalizePhotoResistorRange();
  } else if (configKeyEquals(key, "photoresistordark") ||
             configKeyEquals(key, "photoresistorhigh") ||
             configKeyEquals(key, "minresistor")) {
    photoResistorDarkRaw = value.toInt();
    normalizePhotoResistorRange();
  } else if (configKeyEquals(key, "photodimsteps")) {
    photoDimSteps = int(min(255L, max(2L, value.toInt())));
    normalizePhotoDimSettings();
  } else if (configKeyEquals(key, "photodimdeadzone")) {
    photoDimDeadzone = value.toInt();
    normalizePhotoDimSettings();
  } else if (configKeyEquals(key, "hourspan")) {
    hourspan = int(max(1L, value.toInt()));
  } else if (configKeyEquals(key, "sunrise")) {
    if (parseClockToMinutes(value) != -1)
    {
      sunrise_time = value;
    }
    else
    {
      Serial.print("Invalid sunrise ignored: ");
      Serial.println(value);
    }
  } else if (configKeyEquals(key, "sunset")) {
    if (parseClockToMinutes(value) != -1)
    {
      sunset_time = value;
    }
    else
    {
      Serial.print("Invalid sunset ignored: ");
      Serial.println(value);
    }
  } else if (configKeyEquals(key, "autodimholdms")) {
    autodim_hold_ms = int(max(0L, value.toInt()));
  } else if (configKeyEquals(key, "autodimstepms")) {
    autodim_step_ms = int(max(10L, value.toInt()));
  } else if (configKeyEquals(key, "autodimpercent")) {
    autodim_percent = int(min(100L, max(1L, value.toInt())));
  } else if (configKeyEquals(key, "autodimdebug")) {
    autodim_debug = (value.toInt() != 0);
  } else if (configKeyEquals(key, "WeekDays")) {
    parseTranslationList(value, WeekDays, WEEKDAY_COUNT);
  } else if (configKeyEquals(key, "MonthName")) {
    parseTranslationList(value, MonthName, MONTH_COUNT);
  } else if (configKeyEquals(key, "updateurl")) {
    updateurl = value;
  }
}

bool read_system_id_from_sd()
{
  String rawSystemId = "";

  if (!read_text_file_from_sd("/systemid.txt", rawSystemId))
  {
    system_id = "";
    return false;
  }

  parseSystemIdList(rawSystemId);
  return true;
}

bool read_system_id_from_littlefs()
{
  String rawSystemId = "";
  if (!read_file_text_from_littlefs("/systemid.txt", rawSystemId))
  {
    return false;
  }

  parseSystemIdList(rawSystemId);
  return true;
}

bool read_system_id_from_wifi_profile_littlefs()
{
  String wifiText;
  String rawSystemId;
  int start = 0;

  if (!readLittleFsTextMounted("/wifi.txt", wifiText))
  {
    return false;
  }

  while (start < wifiText.length())
  {
    int end = wifiText.indexOf('\n', start);
    String line;
    if (end == -1)
    {
      line = wifiText.substring(start);
      start = wifiText.length();
    }
    else
    {
      line = wifiText.substring(start, end);
      start = end + 1;
    }

    line.replace("\r", "");
    line.trim();
    int separator = line.indexOf('=');
    if (separator == -1)
    {
      continue;
    }

    String key = line.substring(0, separator);
    key = sanitizeConfigKey(key);
    if (key != "systemid")
    {
      continue;
    }

    String value = line.substring(separator + 1);
    value.replace(";", "\n");
    rawSystemId += value;
    rawSystemId += "\n";
  }

  if (rawSystemId == "")
  {
    return false;
  }

  parseSystemIdList(rawSystemId);
  return system_id_count > 0;
}

void apply_current_config_with_runtime_state()
{
  String old_tzinfo = tzinfo;
  String old_ntpserver = ntpserver;
  String old_tformat = tformat;
  int old_ntp_sync_frequency_minutes = ntp_sync_frequency_minutes;
  int old_ntp_sync_random_delay_seconds = ntp_sync_random_delay_seconds;
  int old_ntp_retry_frequency_minutes = ntp_retry_frequency_minutes;
  int old_ntp_retry_random_delay_seconds = ntp_retry_random_delay_seconds;
  String oldWeekDays[WEEKDAY_COUNT];
  String oldMonthName[MONTH_COUNT];
  int runtimeBrightnessBeforeApply = brightness;

  for (int index = 0; index < WEEKDAY_COUNT; ++index)
  {
    oldWeekDays[index] = WeekDays[index];
  }
  for (int index = 0; index < MONTH_COUNT; ++index)
  {
    oldMonthName[index] = MonthName[index];
  }

  apply_config_from_string(current_config_text);
  brightness = runtimeBrightnessBeforeApply;

  if ((tzinfo != old_tzinfo) || (ntpserver != old_ntpserver))
  {
    apply_runtime_NTP_config();
  }

  if ((ntp_sync_frequency_minutes != old_ntp_sync_frequency_minutes) ||
      (ntp_sync_random_delay_seconds != old_ntp_sync_random_delay_seconds) ||
      (ntp_retry_frequency_minutes != old_ntp_retry_frequency_minutes) ||
      (ntp_retry_random_delay_seconds != old_ntp_retry_random_delay_seconds))
  {
    scheduleNextNtpSync(true);
  }

  if ((tzinfo != old_tzinfo) || (ntpserver != old_ntpserver) ||
      (tformat != old_tformat) ||
      !stringArrayEquals(WeekDays, oldWeekDays, WEEKDAY_COUNT) ||
      !stringArrayEquals(MonthName, oldMonthName, MONTH_COUNT))
  {
    event_tm_hour = -1;
    event_tm_min = -1;
    event_tm_sec = -1;
  }
}

void load_cached_config_for_index_from_storage(int index, bool allowLegacyFallback)
{
  String content;
  String cachePath;

  if ((index < 0) || (index >= system_id_count))
  {
    return;
  }

  cachePath = get_config_cache_path_for_id(system_id_list[index]);
  if (!ram_only_mode && read_text_file_from_sd(cachePath, content))
  {
    cached_config_texts[index] = content;
    cached_config_loaded[index] = true;
    config_source_state[index] = "CACHED_SD";
    return;
  }

  if (allowLegacyFallback)
  {
    if (!ram_only_mode && read_config_text_from_sd(content))
    {
      cached_config_texts[index] = content;
      cached_config_loaded[index] = true;
      config_source_state[index] = "CACHED_SD_LEGACY";
      return;
    }

    if (read_config_text_from_littlefs(content))
    {
      cached_config_texts[index] = content;
      cached_config_loaded[index] = true;
      config_source_state[index] = "CACHED_LFS";
      return;
    }
  }

  cached_config_texts[index] = "";
  cached_config_loaded[index] = false;
  config_source_state[index] = "MISSING";
}

bool sync_config_to_sd_and_memory(String newContent, bool &changed)
{
  String verifiedContent;

  changed = false;

  if (current_config_text == newContent)
  {
    Serial.println("sync_config_to_sd_and_memory: no config changes");
    return true;
  }

  Serial.println("sync_config_to_sd_and_memory: config changed, writing payload");
  if (!write_config_to_sd(newContent))
  {
    Serial.println("sync_config_to_sd_and_memory: write failed");
    return false;
  }

  if (!read_config_text_from_sd(verifiedContent))
  {
    Serial.println("sync_config_to_sd_and_memory: verify read failed");
    return false;
  }

  if (verifiedContent != newContent)
  {
    Serial.println("sync_config_to_sd_and_memory: verify mismatch");
    return false;
  }

  current_config_text = verifiedContent;
  changed = true;
  Serial.println("sync_config_to_sd_and_memory: verified");
  return true;
}

// read config.txt from SD Card
void read_sd()
{
  Serial.println("read_sd: Begin");
  if (ram_only_mode)
  {
    if (read_config_text_from_littlefs(current_config_text))
    {
      Serial.println("RAM_ONLY: Using LittleFS /config.txt");
      apply_config_from_string(current_config_text);
    }
    else
    {
      current_config_text = "";
      Serial.println("RAM_ONLY: LittleFS /config.txt missing -- Using Defaults.");
    }
  }
  else if (read_config_text_from_sd(current_config_text))
  {
    Serial.println("Config source: SD /config.txt");
    apply_config_from_string(current_config_text);
  }
  else if (read_config_text_from_littlefs(current_config_text))
  {
    Serial.println("Config source: LittleFS /config.txt");
    apply_config_from_string(current_config_text);
  }
  else
  {
    current_config_text = "";
    Serial.println("Config source: defaults (/config.txt missing on SD and LittleFS)");
  }
  Serial.println("read_sd: End");
}

void read_system_id()
{
  auto logSystemIdState = [](const char *prefix) {
    if (system_id_count > 0)
    {
      Serial.print(prefix);
      Serial.print(system_id);
      Serial.print(" (");
      Serial.print(active_system_id_index + 1);
      Serial.print("/");
      Serial.print(system_id_count);
      Serial.println(")");
    }
    else
    {
      Serial.println("System ID file empty after sanitization");
    }
  };

  Serial.println("read_system_id: Begin");
  if (read_system_id_from_littlefs())
  {
    logSystemIdState("System ID source: LittleFS /systemid.txt -> ");
  }
  else if (read_system_id_from_sd())
  {
    logSystemIdState("System ID source: SD /systemid.txt -> ");
  }
  else if (read_system_id_from_wifi_profile_littlefs())
  {
    logSystemIdState("System ID source: LittleFS /wifi.txt -> ");
  }
  else
  {
    clearSystemIdList();
    Serial.println("System ID source: none (/systemid.txt missing on SD and LittleFS)");
  }
  Serial.println("read_system_id: End");
}

void apply_config_from_string(String content)
{
  int start = 0;
  int end = 0;
  String line;

  clearScheduleEntries();

  while (start < content.length())
  {
    end = content.indexOf('\n', start);
    if (end == -1)
    {
      line = content.substring(start);
      start = content.length();
    }
    else
    {
      line = content.substring(start, end);
      start = end + 1;
    }

    line.replace("\r", "");
    line.trim();

    if (line == "")
    {
    }
    else if (line.startsWith("#"))
    {
    }
    else
    {
      parseConfigLine(line);
    }
  }
}

