#include "Arduino.h"
#include <SD.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <time.h>
//#include <sunset.h>
#include "seven_regular11pt7b.h"
#include "seven_regular31pt7b.h"
#include "DSEG14_Classic_Regular_60.h"
#include "app_state.h"
#include "brightness_manager.h"
#include "display_manager.h"
#include "network_manager.h"
#include "schedule_display.h"
#include "setup_portal.h"
#include "storage_manager.h"
#include "touch_manager.h"
#include <esp_system.h>

//SD Card Pin
#define SD_CS 5

//Backlight and photoresistor pins are defined in brightness_manager.h
int photoResistorBrightRaw = 100;
int photoResistorDarkRaw = 1024;
int photoDimSteps = 10;
int photoDimDeadzone = 2;
int photoDimTargetStep = -1;
int photoDimTargetBrightness = -1;
int brightness = 128; // Brightness (0-255)
int mindim = 32;
int maxdim = 128;
int hourspan = 1;
String sunrise_time = "06:00";
String sunset_time = "18:00";
unsigned long next_auto_dim_ms = 0;
unsigned long auto_dim_resume_ms = 0;
unsigned long next_photoresistor_log_ms = 0;
int autodim_hold_ms = 2000;
int autodim_step_ms = 1000;
int autodim_percent = 10;
bool autodim_debug = false;
unsigned long next_autodim_debug_ms = 0;

// RGB conversion
#define RGB565(r, g, b) (((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F))

uint16_t clockTextColor = TFT_RED;
uint16_t dateTextColor = TFT_WHITE;
uint16_t dateBgColor = RGB565(0, 0, 90 >> 3);
uint16_t statusTextColor = TFT_WHITE;
uint16_t statusBgColor = RGB565(0, 0, 90 >> 3);
uint16_t scheduleTextColor = RGB565(255 >> 3, 220 >> 2, 160 >> 3);
uint16_t bootTextColor = RGB565(128 >> 3, 255 >> 2, 128 >> 3);
uint16_t errorTextColor = RGB565(255 >> 3, 128 >> 2, 128 >> 3);

// Default URL to pull config.txt from -- Hard coding is bad m'kay... don't follow this example
String updateurl;
constexpr int WEEKDAY_COUNT = 7;
constexpr int MONTH_COUNT = 12;
constexpr int MAX_TRANSLATION_LENGTH = 24;
constexpr int MAX_SYSTEM_ID_COUNT = 16;
// Used to delay the timer when poking the updateurl
unsigned long next_update_check = 0;

TFT_eSPI tft = TFT_eSPI();

// System variables
String ssid;
String password;
String tzinfo;
String tformat;
String ntpserver;
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
bool ram_only_mode = false;

int event_tm_hour = -1;
int event_tm_min = -1;
int event_tm_sec = -1;

int next_update_modular = 15;
int ntp_sync_frequency_minutes = 60;
int ntp_sync_random_delay_seconds = 60 * 60;
int ntp_retry_frequency_minutes = 15;
int ntp_retry_random_delay_seconds = 15 * 60;
unsigned long next_ntp_sync_ms = 0;
bool ntp_sync_scheduled = false;
String build_version_code;

void apply_config_from_string(String content);
void apply_current_config_with_runtime_state();
void load_cached_config_for_index_from_storage(int index, bool allowLegacyFallback);
String sanitizeSystemId(String value);
String sanitizeConfigKey(String key);

int monthFromDateAbbrev(const char *abbrev)
{
  if (strncmp(abbrev, "Jan", 3) == 0) return 1;
  if (strncmp(abbrev, "Feb", 3) == 0) return 2;
  if (strncmp(abbrev, "Mar", 3) == 0) return 3;
  if (strncmp(abbrev, "Apr", 3) == 0) return 4;
  if (strncmp(abbrev, "May", 3) == 0) return 5;
  if (strncmp(abbrev, "Jun", 3) == 0) return 6;
  if (strncmp(abbrev, "Jul", 3) == 0) return 7;
  if (strncmp(abbrev, "Aug", 3) == 0) return 8;
  if (strncmp(abbrev, "Sep", 3) == 0) return 9;
  if (strncmp(abbrev, "Oct", 3) == 0) return 10;
  if (strncmp(abbrev, "Nov", 3) == 0) return 11;
  if (strncmp(abbrev, "Dec", 3) == 0) return 12;
  return 0;
}

String getBuildVersionCode()
{
  char dateMonth[4];
  int day = 0;
  int year = 0;
  int hour = 0;
  int minute = 0;
  char buffer[11];

  if (sscanf(__DATE__, "%3s %d %d", dateMonth, &day, &year) != 3)
  {
    return "0000000000";
  }

  if (sscanf(__TIME__, "%d:%d", &hour, &minute) != 2)
  {
    return "0000000000";
  }

  int month = monthFromDateAbbrev(dateMonth);
  if (month == 0)
  {
    return "0000000000";
  }

  snprintf(buffer, sizeof(buffer), "%02d%02d%02d%02d%02d",
           year % 100, month, day, hour, minute);
  return String(buffer);
}

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

// Start and Config CYD
void setup() 
{
  Serial.begin(115200);
  randomSeed((uint32_t)esp_random());
  pinMode(XPT2046_CS, OUTPUT);
  digitalWrite(XPT2046_CS, HIGH);

  // Start the tft display early so status text works
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setFreeFont(&seven_regular11pt7b);
  tft.drawString("CALENDAR V1.1", 0, 0);
  tft.setTextFont(1);
  tft.setTextColor(bootTextColor, TFT_BLACK);
  tft.setCursor(0, 30);
  tft.println("Calendar Start");
  build_version_code = getBuildVersionCode();

  ram_only_mode = !detect_sd_available_at_boot();
  if (ram_only_mode)
  {
    Serial.println("Boot mode: RAM_ONLY (SD missing at boot)");
    tft.println("Boot mode: RAM_ONLY");
  }
  else
  {
    Serial.println("Boot mode: SD");
  }

  list_sd_files_to_serial();
  list_littlefs_files_to_serial();

  read_system_id();
  if (system_id_count <= 0)
  {
    read_sd();
  }
  else
  {
    for (int index = 0; index < system_id_count; ++index)
    {
      load_cached_config_for_index_from_storage(index, (index == 0));
      Serial.print("Config source for ");
      Serial.print(system_id_list[index]);
      Serial.print(": ");
      Serial.println(config_source_state[index]);
    }

    if (cached_config_loaded[0])
    {
      current_config_text = cached_config_texts[0];
      apply_config_from_string(current_config_text);
    }
    else
    {
      read_sd();
    }
  }

  // Backlight after config read
  pinMode(backlightPin, OUTPUT);
  analogWrite(backlightPin, brightness);
  pinMode(photoResistorPin, INPUT);
  analogSetPinAttenuation(photoResistorPin, ADC_11db);
  Serial.print("Photoresistor configured on GPIO");
  Serial.println(photoResistorPin);
  Serial.print("Photoresistor range bright=");
  Serial.print(min(photoResistorBrightRaw, photoResistorDarkRaw));
  Serial.print(" dark=");
  Serial.println(max(photoResistorBrightRaw, photoResistorDarkRaw));

  if (ssid == "")
  {
    loadFirstWifiProfileFromLittleFs();
  }

  bool clockModeReady = false;

  if (ssid != "")
  {
    if (wifi_start_STA() == true)
    {
      clockModeReady = true;
      preload_all_cached_configs_from_server();
      if ((system_id_count > 0) && cached_config_loaded[active_system_id_index])
      {
        current_config_text = cached_config_texts[active_system_id_index];
        apply_current_config_with_runtime_state();
      }
      else
      {
        bootstrap_config_from_server();
      }

      Serial.println("Time Sync ...");
      tft.println("Time Sync ...");
      bool initialNtpSyncSucceeded = timesync();
      scheduleNextNtpSync(initialNtpSyncSucceeded);
      if (initialNtpSyncSucceeded == true)
      {
        Serial.println("Time Sync Ready");
        tft.println("Time Sync Ready");
      }
      else
      {
        tft.setTextColor(errorTextColor, TFT_BLACK);
        Serial.println("non Time sync");
        tft.println("non Time sync");
        delay(3000);
      }
    }
    else
    {
      tft.setTextColor(errorTextColor, TFT_BLACK);
      Serial.println("non WiFi connect");
      tft.println("non WiFi connect");
      delay(3000);
    }
  }
  else
  {
    tft.setTextColor(errorTextColor, TFT_BLACK);
    Serial.println("non SSID or SD Configuration");
    tft.println("non SSID or SD Configuration");
    delay(3000);
  }

  // Start the SPI for the touch screen and init the TS library
  #if ENABLE_TOUCH
  initialize_touch();
  #endif

  if (!clockModeReady)
  {
    startSetupPortal();
    refreshSetupPortalDisplay();
  }

  delay(100);
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

void loop() {
  if (isSetupPortalRunning())
  {
    processSetupPortal();
    return;
  }

  struct tm localtime;
  getLocalTime(&localtime);

  static char localtimeString[10];
  static char locaxtimeString[10];
  char dateString[40];

  processPhotoBrightness();
  processScheduledNtpSync();

  if (localtime.tm_hour != event_tm_hour) {
    event_tm_hour = localtime.tm_hour;
    Serial.println("event_tm_hour");
    snprintf(dateString, sizeof(dateString),
         "%s %d, %04d",
         MonthName[localtime.tm_mon].c_str(),
         localtime.tm_mday,
         localtime.tm_year + 1900);

    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_BLACK, TFT_WHITE);
    tft.setCursor (0,0);

    tft.setTextColor(dateTextColor, dateBgColor);
    tft.drawString(dateString, 38, 0, 4);
    drawBuildAndSystemInfo();
    renderActiveScheduleEntries(localtime);
  }

  if (localtime.tm_min != event_tm_min)
  {
    event_tm_min = localtime.tm_min;
    Serial.println("event_tm_min");
    renderActiveScheduleEntries(localtime);
  }

  if (localtime.tm_sec != event_tm_sec)
  {
    unsigned long now_ms = millis();

    if (now_ms >= next_update_check)
    {
      if ((localtime.tm_sec % next_update_modular) == 0)
      {
        poll_update_server();
        next_update_check = now_ms + 1000;
      }
    }

    event_tm_sec = localtime.tm_sec;
    if (tformat == "24")
    {
      snprintf(localtimeString, sizeof(localtimeString), "%02d:%02d", localtime.tm_hour, localtime.tm_min);
      snprintf(locaxtimeString, sizeof(locaxtimeString), "%02d %02d", localtime.tm_hour, localtime.tm_min);
    }
    else
    {
      int hour12 = localtime.tm_hour % 12;
      if (hour12 == 0) { hour12 = 12; }
      snprintf(localtimeString, sizeof(localtimeString), "%2d:%02d", hour12, localtime.tm_min);
      snprintf(locaxtimeString, sizeof(locaxtimeString), "%2d %02d", hour12, localtime.tm_min);
    }

    TFT_eSprite sprite = TFT_eSprite(&tft);
    sprite.createSprite(318, 61);
    sprite.fillSprite(TFT_BLACK);

    sprite.setFreeFont(&DSEG14_Classic_Regular_60);
    sprite.setTextColor(clockTextColor);
    sprite.setTextDatum(MC_DATUM);

    if (localtime.tm_sec % 2 == 0) {
      sprite.drawString(localtimeString, sprite.width() / 2, sprite.height() / 2);
    } else {
      sprite.drawString(locaxtimeString, sprite.width() / 2, sprite.height() / 2);
    }

    sprite.pushSprite(1, 68);
    sprite.deleteSprite();
  }

  processTouchInput();
}

// void loop() 
// {
//   struct tm localtime;
//   getLocalTime(&localtime);

//   static char localtimeString[10]; // Buffer for time in HH:MM:SS format
//   static char locaxtimeString[10]; // Buffer for time in HH MM format
//   char dateString[40]; // Buffer for long translated month names

//   // processAutoBrightness(localtime);
//   processPhotoBrightness();
  
//   // EVENT every hour
//   if (localtime.tm_hour != event_tm_hour) {
//     event_tm_hour = localtime.tm_hour;
//     Serial.println("event_tm_hour");
//     // LOCAL Date TT.MO.YYYY
//     //sprintf(dateString, "%02d.%02d.%04d", localtime.tm_mday, localtime.tm_mon + 1, localtime.tm_year + 1900);
//     snprintf(dateString, sizeof(dateString),
//          "%s %d, %04d",
//          MonthName[localtime.tm_mon].c_str(),
//          localtime.tm_mday,
//          localtime.tm_year + 1900);
//     // Calculate the time zone based on the difference between local time and UTC

//     tft.println("NTP Sync");
//     configTzTime(tzinfo.c_str(), ntpserver.c_str()); // Synchronize ESP32 system time with NTP
//     delay(1000);
//     getLocalTime(&localtime);

//     // Redraw the clock
//     tft.fillScreen(TFT_BLACK);
//     tft.setTextColor(TFT_BLACK, TFT_WHITE);
//     tft.setCursor (0,0);
    
//     // draw Date      
//     tft.setTextColor(dateTextColor, dateBgColor);
//     tft.drawString(dateString, 38, 0, 4);
//     drawBuildAndSystemInfo();
//     renderActiveScheduleEntries(localtime);
    
//   }

//   // EVENT every min
//   if (localtime.tm_min != event_tm_min)
//   {
//     event_tm_min = localtime.tm_min;
//     Serial.println("event_tm_min");
//     renderActiveScheduleEntries(localtime);
//   }

//   // EVENT every sec
//   if (localtime.tm_sec != event_tm_sec)
//   {
//     unsigned long now_ms = millis();

//     // Poll the remote server for an update to config.txt
//     if (now_ms >= next_update_check)
//     {
//       if ((localtime.tm_sec % next_update_modular) == 0)
//       {
//         poll_update_server();
//         next_update_check = now_ms + 1000;
//       }
//     }
    
//     event_tm_sec = localtime.tm_sec;
//     // Serial.println("event_tm_sec");
//     // LOCAL Time .HH:MM:SS
//     if (tformat == "24")
//     {
//       // Define localtimeString and locaxtimeString as the formatted time for 24h format
//       snprintf(localtimeString, sizeof(localtimeString), "%02d:%02d", localtime.tm_hour, localtime.tm_min);
//       snprintf(locaxtimeString, sizeof(locaxtimeString), "%02d %02d", localtime.tm_hour, localtime.tm_min);
//     }
//     else
//     {
//       int hour12 = localtime.tm_hour % 12;
//       if (hour12 == 0) { hour12 = 12; }
//       // Define localtimeString and locaxtimeString as the formatted time for 12h format
//       snprintf(localtimeString, sizeof(localtimeString), "%2d:%02d", hour12, localtime.tm_min);
//       snprintf(locaxtimeString, sizeof(locaxtimeString), "%2d %02d", hour12, localtime.tm_min);
//     }
//     // draw Time to CLOCK
//     // without Flicker with Sprite
//     TFT_eSprite sprite = TFT_eSprite(&tft);
//     sprite.createSprite(318, 61);
//     sprite.fillSprite(TFT_BLACK);  // triple zero

//     sprite.setFreeFont(&DSEG14_Classic_Regular_60);
//     //sprite.setFreeFont(&seven_regular31pt7b);
//     sprite.setTextColor(clockTextColor);  // no background overwrite

//     // Set text alignment to middle-center
//     sprite.setTextDatum(MC_DATUM);

//     // Draw centered inside sprite
//     if (localtime.tm_sec % 2 == 0) {
//       sprite.drawString(localtimeString, sprite.width() / 2, sprite.height() / 2);
//     } else {
//       sprite.drawString(locaxtimeString, sprite.width() / 2, sprite.height() / 2);
//     }

//     sprite.pushSprite(1, 68);
//     sprite.deleteSprite();
      
//   }

//   // EVENT Pen touch
//   #if ENABLE_TOUCH
//   if (touch_ready && ts.tirqTouched() && ts.touched())  {
//     TS_Point p = ts.getPoint();
//     printTouchToSerial(p);

//     if (p.y > 3200)
//     {
//       if (p.x < 800)
//       {
//         int target = computePhotoTargetBrightness(analogRead(photoResistorPin));
//         setBrightnessFromController(mindim, "Instant min dim", true, target);
//       }
//       else if (p.x > 3200)
//       {
//         int target = computePhotoTargetBrightness(analogRead(photoResistorPin));
//         setBrightnessFromController(maxdim, "Instant max dim", true, target);
//       }
//       delay(200);
//       return;
//     }

//     // Adjust brightness
//     // Top part of the screen
//     if (p.y < 800) {
//       if ((p.x >= 1200) && (p.x <= 2800))
//       {
//         handleSystemIdSwitchTouch();
//         delay(300);
//         return;
//       }

//       int brightness_step = 32;
//       if (brightness < 64) { brightness_step = 16; }
//       if (brightness < 32) { brightness_step = 8;  }
//       if (brightness < 16) { brightness_step = 4;  }
//       if (brightness < 8)  { brightness_step = 2;  }
//       if (brightness < 4)  { brightness_step = 1;  }
//       // Top-Left of the screen
//       if (p.x < 800) {
//         setBrightnessFromController(brightness - brightness_step, "Brightness", true);
//       }
//       // Top-right of the screen
//       if (p.x > 3200) {
//         setBrightnessFromController(brightness + brightness_step, "Brightness", true);
//       }
//     }
//     delay(300);
//   }
//   #endif
// }


//end
