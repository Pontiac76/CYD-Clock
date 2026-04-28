#include "Arduino.h"
#include <SD.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <time.h>
//#include <sunset.h>
#include "seven_regular11pt7b.h"
#include "seven_regular31pt7b.h"
#include "DSEG14_Classic_Regular_60.h"
#include "app_state.h"
#include "schedule_display.h"
#include <HTTPClient.h>
#include <LittleFS.h>

//Touch Pins
#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33
#define ENABLE_TOUCH 1

//SD Card Pin
#define SD_CS 5

//Backlight Pin
#define backlightPin 21
#ifndef CDS
#define CDS 34
#endif
constexpr int photoResistorPin = CDS;
constexpr unsigned long photoResistorLogIntervalMs = 1000;
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
#define DEFAULT_UPDATE_URL "http://192.168.4.2:8080"

constexpr int WEEKDAY_COUNT = 7;
constexpr int MONTH_COUNT = 12;
constexpr int MAX_TRANSLATION_LENGTH = 24;
constexpr int MAX_SYSTEM_ID_COUNT = 16;
// Used to delay the timer when poking the updateurl
unsigned long next_update_check = 0;

SPIClass mySpi = SPIClass(HSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
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
bool sd_ready = false;
bool touch_ready = false;
bool touch_initialized = false;
bool ram_only_mode = false;

int event_tm_hour = -1;
int event_tm_min = -1;
int event_tm_sec = -1;

int next_update_modular = 15;
String build_version_code;

void apply_config_from_string(String content);
bool poll_update_server();
void drawBuildAndSystemInfo();
String build_update_request_url_for_system_id(const String &id);
void ensure_default_update_url();
void apply_current_config_with_runtime_state();
void load_cached_config_for_index_from_storage(int index, bool allowLegacyFallback);
void preload_all_cached_configs_from_server();
void apply_runtime_NTP_config();

void logPhotoResistorReading()
{
  unsigned long nowMs = millis();
  if (nowMs < next_photoresistor_log_ms)
  {
    return;
  }

  int lightLevel = analogRead(photoResistorPin);
  Serial.print("Photoresistor GPIO");
  Serial.print(photoResistorPin);
  Serial.print(" raw=");
  Serial.println(lightLevel);

  next_photoresistor_log_ms = nowMs + photoResistorLogIntervalMs;
}

int parseClockToMinutes(const String &value)
{
  int hour = 0;
  int minute = 0;
  String cleaned = value;
  cleaned.trim();
  if (sscanf(cleaned.c_str(), "%d:%d", &hour, &minute) != 2)
  {
    return -1;
  }
  if ((hour < 0) || (hour > 23) || (minute < 0) || (minute > 59))
  {
    return -1;
  }
  return (hour * 60) + minute;
}

int clampBrightness(int value)
{
  return min(255, max(1, value));
}

void applyBrightnessValue(int value)
{
  brightness = clampBrightness(value);
  analogWrite(backlightPin, brightness);
}

void setBrightnessFromController(int value, const char *source, bool holdAutoDim, int target = -1, bool logChange = true)
{
  applyBrightnessValue(value);
  if (holdAutoDim)
  {
    auto_dim_resume_ms = millis() + autodim_hold_ms;
  }

  if (!logChange)
  {
    return;
  }

  Serial.print(source);
  Serial.print(" -> ");
  Serial.print(brightness);
  if (target >= 0)
  {
    Serial.print(" target=");
    Serial.print(target);
  }
  Serial.println();
}

int nextBrightnessStepToward(int target)
{
  int nextValue = brightness;
  int gap = abs(target - brightness);
  int step = max(1, (gap * autodim_percent) / 100);

  if (brightness < target)
  {
    nextValue = min(target, brightness + step);
  }
  else if (brightness > target)
  {
    nextValue = max(target, brightness - step);
  }

  return nextValue;
}

int computePhotoDimStepCount()
{
  return constrain(photoDimSteps, 2, 255);
}

void normalizePhotoDimSettings()
{
  photoDimSteps = computePhotoDimStepCount();
  photoDimDeadzone = constrain(photoDimDeadzone, 0, photoDimSteps - 1);
  photoDimTargetStep = -1;
  photoDimTargetBrightness = -1;
}

int computePhotoBrightnessForStep(int stepIndex)
{
  int safeMin = clampBrightness(min(mindim, maxdim));
  int safeMax = clampBrightness(max(mindim, maxdim));
  int stepCount = computePhotoDimStepCount();
  int brightnessRange = safeMax - safeMin;

  if (brightnessRange <= 0)
  {
    return safeMin;
  }

  stepIndex = constrain(stepIndex, 0, stepCount - 1);
  return safeMin + ((brightnessRange * stepIndex) / (stepCount - 1));
}

int computePhotoTargetStep(int rawLightLevel)
{
  int brightRaw = min(photoResistorBrightRaw, photoResistorDarkRaw);
  int darkRaw = max(photoResistorBrightRaw, photoResistorDarkRaw);
  int clampedRaw = constrain(rawLightLevel, brightRaw, darkRaw);
  int stepCount = computePhotoDimStepCount();

  if (brightRaw == darkRaw)
  {
    return stepCount - 1;
  }
  if (rawLightLevel <= brightRaw)
  {
    return stepCount - 1;
  }
  if (rawLightLevel >= darkRaw)
  {
    return 0;
  }

  int rawRange = darkRaw - brightRaw;
  int rawOffsetFromDark = darkRaw - clampedRaw;
  return (rawOffsetFromDark * (stepCount - 1)) / rawRange;
}

int computePhotoTargetBrightness(int rawLightLevel)
{
  return computePhotoBrightnessForStep(computePhotoTargetStep(rawLightLevel));
}

int computePhotoDimDeadzone()
{
  return constrain(photoDimDeadzone, 0, computePhotoDimStepCount() - 1);
}

void processPhotoBrightness()
{
  unsigned long nowMs = millis();
  if (nowMs < auto_dim_resume_ms)
  {
    return;
  }
  if (nowMs < next_auto_dim_ms)
  {
    return;
  }

  int rawLightLevel = analogRead(photoResistorPin);
  int computedStep = computePhotoTargetStep(rawLightLevel);
  int computedTarget = computePhotoBrightnessForStep(computedStep);
  int deadzone = computePhotoDimDeadzone();
  int brightRaw = min(photoResistorBrightRaw, photoResistorDarkRaw);
  int darkRaw = max(photoResistorBrightRaw, photoResistorDarkRaw);
  bool outsidePhotoRange = (rawLightLevel <= brightRaw) || (rawLightLevel >= darkRaw);
  bool targetChanged = (computedStep != photoDimTargetStep);

  if (photoDimTargetStep == -1)
  {
    photoDimTargetStep = computedStep;
    photoDimTargetBrightness = computedTarget;
  }
  else if (targetChanged &&
           (outsidePhotoRange || (abs(computedStep - photoDimTargetStep) >= deadzone)))
  {
    photoDimTargetStep = computedStep;
    photoDimTargetBrightness = computedTarget;
    if (autodim_debug)
    {
      Serial.print("PhotoDim target accepted=");
      Serial.print(photoDimTargetBrightness);
      Serial.print(" step=");
      Serial.print(photoDimTargetStep);
      Serial.print(" computed=");
      Serial.print(computedTarget);
      Serial.print(" deadzone=");
      Serial.print(deadzone);
      if (outsidePhotoRange)
      {
        Serial.print(" outside_range=1");
      }
      Serial.println();
    }
  }

  int nextValue = nextBrightnessStepToward(photoDimTargetBrightness);

  if (nextValue != brightness)
  {
    setBrightnessFromController(nextValue, "PhotoDim", false, photoDimTargetBrightness, autodim_debug);
    if (autodim_debug)
    {
      Serial.print("PhotoDim sensor raw=");
      Serial.print(rawLightLevel);
      Serial.print(" range=");
      Serial.print(brightRaw);
      Serial.print("-");
      Serial.print(darkRaw);
      Serial.print(" step=");
      Serial.print(computedStep);
      Serial.print(" computed=");
      Serial.print(computedTarget);
      Serial.print(" deadzone=");
      Serial.println(deadzone);
    }
  }

  next_auto_dim_ms = nowMs + max(10, autodim_step_ms);
}

int computeAutoTargetBrightness(const struct tm &localtime)
{
  int sunriseMinutes = parseClockToMinutes(sunrise_time);
  int sunsetMinutes = parseClockToMinutes(sunset_time);
  int currentMinutes = (localtime.tm_hour * 60) + localtime.tm_min;
  int safeMin = clampBrightness(min(mindim, maxdim));
  int safeMax = clampBrightness(max(mindim, maxdim));
  int transitionMinutes = max(2, hourspan * 60);
  int halfWindow = transitionMinutes / 2;

  if ((sunriseMinutes == -1) || (sunsetMinutes == -1) || (sunriseMinutes >= sunsetMinutes))
  {
    return safeMax;
  }

  int sunriseStart = sunriseMinutes - halfWindow;
  int sunriseEnd = sunriseMinutes + halfWindow;
  int sunsetStart = sunsetMinutes - halfWindow;
  int sunsetEnd = sunsetMinutes + halfWindow;

  if (currentMinutes < sunriseStart)
  {
    return safeMin;
  }
  if (currentMinutes < sunriseEnd)
  {
    int numerator = (currentMinutes - sunriseStart) * (safeMax - safeMin);
    int denominator = max(1, sunriseEnd - sunriseStart);
    return safeMin + (numerator / denominator);
  }
  if (currentMinutes < sunsetStart)
  {
    return safeMax;
  }
  if (currentMinutes < sunsetEnd)
  {
    int numerator = (currentMinutes - sunsetStart) * (safeMax - safeMin);
    int denominator = max(1, sunsetEnd - sunsetStart);
    return safeMax - (numerator / denominator);
  }

  return safeMin;
}

void processAutoBrightness(const struct tm &localtime)
{
  unsigned long nowMs = millis();
  if (nowMs < auto_dim_resume_ms)
  {
    if (autodim_debug && (nowMs >= next_autodim_debug_ms))
    {
      int target = computeAutoTargetBrightness(localtime);
      Serial.print("AutoDim HOLD current=");
      Serial.print(brightness);
      Serial.print(" target=");
      Serial.print(target);
      Serial.print(" resume_in_ms=");
      Serial.println(auto_dim_resume_ms - nowMs);
      next_autodim_debug_ms = nowMs + 5000;
    }
    return;
  }
  if (nowMs < next_auto_dim_ms)
  {
    return;
  }

  int target = computeAutoTargetBrightness(localtime);
  int nextValue = nextBrightnessStepToward(target);

  if (nextValue != brightness)
  {
    setBrightnessFromController(nextValue, "AutoDim STEP", false, target, autodim_debug);
  }
  else if (autodim_debug && (nowMs >= next_autodim_debug_ms))
  {
    Serial.print("AutoDim IDLE current=");
    Serial.print(brightness);
    Serial.print(" target=");
    Serial.println(target);
    next_autodim_debug_ms = nowMs + 5000;
  }

  next_auto_dim_ms = nowMs + max(10, autodim_step_ms);
}

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

void drawBuildAndSystemInfo()
{
  String idText = (system_id == "") ? "no-id" : system_id;
  const int rightX = 318;
  const int idY = 2;
  const int buildY = 14;
  const int lineHeight = 10;
  const int clearPad = 2;

  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(statusTextColor, statusBgColor);

  int idClearWidth = max(system_id_clear_pixel_width, static_cast<int>(tft.textWidth(idText, 1)));

  // Clear a fixed right-aligned lane sized for the longest configured system ID.
  int idClearX = max(0, rightX - idClearWidth - clearPad);
  int idClearW = min(320 - idClearX, idClearWidth + (clearPad * 2));
  tft.fillRect(idClearX, idY, idClearW, lineHeight, statusBgColor);

  tft.setTextDatum(TR_DATUM);
  tft.drawString(idText, rightX, idY, 1);
  tft.drawString(build_version_code, rightX, buildY, 1);
  tft.setTextDatum(TL_DATUM);
}

void initialize_touch()
{
  #if ENABLE_TOUCH
  if (!touch_initialized)
  {
    mySpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    ts.begin(mySpi);
    ts.setRotation(1);
    touch_initialized = true;
  }

  digitalWrite(XPT2046_CS, HIGH);
  touch_ready = true;
  #endif
}

void suspend_touch_for_sd()
{
  #if ENABLE_TOUCH
  if (touch_ready)
  {
    digitalWrite(XPT2046_CS, HIGH);
    touch_ready = false;
  }
  #endif
}

void resume_touch_after_sd()
{
  #if ENABLE_TOUCH
  if (!touch_ready)
  {
    initialize_touch();
  }
  #endif
}

bool begin_sd_session()
{
  if (ram_only_mode)
  {
    return false;
  }

  suspend_touch_for_sd();
  digitalWrite(XPT2046_CS, HIGH);

  if (sd_ready)
  {
    SD.end();
    sd_ready = false;
  }

  if (!SD.begin(SD_CS))
  {
    Serial.println("SD-Card: Failure");
    resume_touch_after_sd();
    return false;
  }

  sd_ready = true;
  return true;
}

void end_sd_session()
{
  if (sd_ready)
  {
    SD.end();
    sd_ready = false;
  }

  resume_touch_after_sd();
}

bool wifi_start_STA() //Start WiFi Mode STA
{
  int sync_count = 0;
  WiFi.mode(WIFI_STA);
  if (WiFi.status() != WL_CONNECTED)  
  {
    Serial.println("WiFi Start");
    tft.println("WiFi Start");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
      delay(100);
      Serial.print(".");
      tft.print(".");
      sync_count = ++sync_count;
      if (sync_count == 40)
      {
        Serial.println();
        tft.println();
        return 0;
        break;     
      }
      if (sync_count == 6)
      {
        WiFi.begin(ssid, password); //second try
      }      
      if (sync_count == 20)
      {
        WiFi.begin(ssid, password); //second try
      }      
    }    
  }
  Serial.println();
  tft.println();
  Serial.print(F("IP address STA: "));
  tft.print(F("IP address STA: "));
  Serial.println(WiFi.localIP());
  tft.println(WiFi.localIP());
  Serial.print(F("SSID: "));
  tft.print(F("SSID: "));
  Serial.println(WiFi.SSID());
  tft.println(WiFi.SSID());
  Serial.printf("BSSID: %s\n", WiFi.BSSIDstr().c_str());
  tft.printf("BSSID: %s\n", WiFi.BSSIDstr().c_str());
  // Serial.print(F("PW: "));
  // tft.print(F("PW: "));
  // Serial.println(WiFi.psk());
  // tft.println(WiFi.psk());
  return 1;
}

bool timesync()
{
  bool exit_status = 1;
  Serial.println("Get NTP Time");
  tft.println("Get NTP Time");
  if (WiFi.status() == WL_CONNECTED)
  {
    struct tm local;
    configTzTime(tzinfo.c_str(), ntpserver.c_str()); // Synchronize ESP32 system time with NTP
    if (!getLocalTime(&local, 10000)) // Try to synchronize for 10 seconds
    {
      Serial.println("Timeserver cannot be reached !!!");
      tft.println("Timeserver cannot be reached !!!");
      exit_status = 0;
    }
    else
    {
      Serial.print("Timeserver: ");
      tft.print("Timeserver: ");
      Serial.println(&local, "Datum: %d.%m.%y  Zeit: %H:%M:%S Test: %a,%B");
      tft.println(&local, "Datum: %d.%m.%y  Zeit: %H:%M:%S Test: %a,%B");
      Serial.flush();
    }
  }
  else
  {
    Serial.println("WiFi not connected !!!");
    tft.println("WiFi not connected !!!");
    exit_status = 0;    
  }  
  return exit_status;  
}

uint16_t createColor(uint8_t r, uint8_t g, uint8_t b) 
{
  return RGB565(r >> 3, g >> 2, b >> 3);
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

bool detect_sd_available_at_boot()
{
  digitalWrite(XPT2046_CS, HIGH);
  if (!SD.begin(SD_CS))
  {
    return false;
  }
  SD.end();
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

bool write_config_to_sd(String content)
{
  File configFile;
  size_t bytesWritten = 0;
  bool success = false;
  Serial.println("write_config_to_sd: Attempting write");
  if (!begin_sd_session())
  {
    goto write_config_to_sd_exit;
  }
  Serial.println("write_config_to_sd: Rotating existing config");

  if (SD.exists("/config.bak"))
  {
    if (!SD.remove("/config.bak"))
    {
      Serial.println("Cannot remove existing /config.bak");
      goto write_config_to_sd_exit;
    }
    Serial.println("write_config_to_sd: removed /config.bak");
  }

  if (SD.exists("/config.txt"))
  {
    if (!SD.rename("/config.txt", "/config.bak"))
    {
      Serial.println("Cannot rename /config.txt to /config.bak");
      goto write_config_to_sd_exit;
    }
    Serial.println("write_config_to_sd: renamed /config.txt to /config.bak");
  }

  Serial.println("write_config_to_sd: Attempting to open config.txt for write");

  configFile = SD.open("/config.txt", FILE_WRITE);
  if (!configFile)
  {
    Serial.println("Cannot open /config.txt for write");
    goto write_config_to_sd_exit;
  } else {
    Serial.println("write_config_to_sd: Opened for write");
  }

  bytesWritten = configFile.print(content);
  configFile.close();

  if (bytesWritten != content.length())
  {
    Serial.println("write_config_to_sd: short write");
    if (SD.exists("/config.txt"))
    {
      SD.remove("/config.txt");
    }
    if (SD.exists("/config.bak"))
    {
      SD.rename("/config.bak", "/config.txt");
      Serial.println("write_config_to_sd: restored /config.bak to /config.txt");
    }
    goto write_config_to_sd_exit;
  }

  Serial.println("write_config_to_sd: Config written");

  Serial.println("Configuration File overwritten");
  success = true;

write_config_to_sd_exit:
  end_sd_session();
  return success;
}

bool read_text_file_from_sd(const String &path, String &content)
{
  File file;
  bool success = false;

  content = "";
  if (!begin_sd_session())
  {
    goto read_text_file_from_sd_exit;
  }

  file = SD.open(path.c_str(), FILE_READ);
  if (!file)
  {
    goto read_text_file_from_sd_exit;
  }

  while (file.available())
  {
    content += char(file.read());
  }

  file.close();
  success = true;

read_text_file_from_sd_exit:
  end_sd_session();
  return success;
}

bool write_text_file_to_sd(const String &path, const String &content)
{
  File file;
  bool success = false;

  if (!begin_sd_session())
  {
    goto write_text_file_to_sd_exit;
  }

  if (SD.exists(path.c_str()))
  {
    if (!SD.remove(path.c_str()))
    {
      Serial.print("Cannot remove ");
      Serial.println(path);
      goto write_text_file_to_sd_exit;
    }
  }

  file = SD.open(path.c_str(), FILE_WRITE);
  if (!file)
  {
    Serial.print("Cannot open ");
    Serial.print(path);
    Serial.println(" for write");
    goto write_text_file_to_sd_exit;
  }

  if (file.print(content) != content.length())
  {
    Serial.print("Short write for ");
    Serial.println(path);
    file.close();
    goto write_text_file_to_sd_exit;
  }

  file.close();
  success = true;

write_text_file_to_sd_exit:
  end_sd_session();
  return success;
}

bool read_config_text_from_sd(String &content)
{
  return read_text_file_from_sd("/config.txt", content);
}

bool read_system_id_from_sd()
{
  File systemIdFile;
  String rawSystemId = "";
  bool success = false;

  if (!begin_sd_session())
  {
    goto read_system_id_from_sd_exit;
  }

  systemIdFile = SD.open("/systemid.txt");
  if (!systemIdFile)
  {
    system_id = "";
    goto read_system_id_from_sd_exit;
  }

  while (systemIdFile.available())
  {
    rawSystemId += char(systemIdFile.read());
  }

  systemIdFile.close();
  parseSystemIdList(rawSystemId);
  success = true;

read_system_id_from_sd_exit:
  end_sd_session();
  return success;
}

bool read_file_text_from_littlefs(const char *path, String &content)
{
  File file;
  bool success = false;

  content = "";
  if (!LittleFS.begin(false))
  {
    return false;
  }

  file = LittleFS.open(path, FILE_READ);
  if (!file)
  {
    goto read_file_text_from_littlefs_exit;
  }

  while (file.available())
  {
    content += char(file.read());
  }

  file.close();
  success = true;

read_file_text_from_littlefs_exit:
  LittleFS.end();
  return success;
}

bool read_config_text_from_littlefs(String &content)
{
  return read_file_text_from_littlefs("/config.txt", content);
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

void list_littlefs_files_to_serial()
{
  File root;
  File entry;

  Serial.println("LittleFS files:");

  if (!LittleFS.begin(false))
  {
    Serial.println("  <littlefs unavailable>");
    return;
  }

  root = LittleFS.open("/");
  if (!root)
  {
    Serial.println("  <cannot open root>");
    LittleFS.end();
    return;
  }

  entry = root.openNextFile();
  if (!entry)
  {
    Serial.println("  <empty>");
  }

  while (entry)
  {
    Serial.print("  ");
    Serial.print(entry.name());
    if (entry.isDirectory())
    {
      Serial.println("/");
    }
    else
    {
      Serial.print(" (");
      Serial.print(entry.size());
      Serial.println(" bytes)");
    }
    entry.close();
    entry = root.openNextFile();
  }

  root.close();
  LittleFS.end();
}

String build_update_request_url()
{
  return build_update_request_url_for_system_id(system_id);
}

String build_update_request_url_for_system_id(const String &id)
{
  String requestUrl = updateurl;

  if (id == "")
  {
    return requestUrl;
  }

  if (requestUrl.indexOf('?') == -1)
  {
    requestUrl += "/?systemid=";
  }
  else
  {
    requestUrl += "/&systemid=";
  }

  requestUrl += id;
  return requestUrl;
}

void ensure_default_update_url()
{
  if (updateurl == "")
  {
    updateurl = DEFAULT_UPDATE_URL;
    Serial.print("updateurl missing, using default: ");
    Serial.println(updateurl);
  }
}

void apply_current_config_with_runtime_state()
{
  String old_tzinfo = tzinfo;
  String old_ntpserver = ntpserver;
  String old_tformat = tformat;
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

void preload_all_cached_configs_from_server()
{
  HTTPClient http;
  String payload;
  String requestUrl;
  String cachePath;

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("prefetch skipped: WiFi not connected");
    return;
  }

  ensure_default_update_url();
  if (updateurl == "")
  {
    Serial.println("prefetch skipped: no updateurl");
    return;
  }

  for (int index = 0; index < system_id_count; ++index)
  {
    requestUrl = build_update_request_url_for_system_id(system_id_list[index]);
    Serial.print("Prefetch ");
    Serial.println(requestUrl);
    http.begin(requestUrl);
    http.setTimeout(1500);
    int httpCode = http.GET();

    if ((httpCode == HTTP_CODE_OK))
    {
      payload = http.getString();
      http.end();
      if (payload.length() > 0)
      {
        bool changed = !cached_config_loaded[index] ||
                       !configContentEqualsNormalized(cached_config_texts[index], payload);
        cached_config_texts[index] = payload;
        cached_config_loaded[index] = true;
        config_source_state[index] = "LIVE";

        if (!ram_only_mode && changed)
        {
          cachePath = get_config_cache_path_for_id(system_id_list[index]);
          if (!write_text_file_to_sd(cachePath, payload))
          {
            Serial.print("Prefetch write failed for ");
            Serial.println(cachePath);
          }
        }
        continue;
      }
    }
    else if (httpCode > 0)
    {
      Serial.print("Prefetch HTTP code ");
      Serial.print(httpCode);
      Serial.print(" for ");
      Serial.println(system_id_list[index]);
      http.end();
    }
    else
    {
      Serial.print("Prefetch GET failed for ");
      Serial.print(system_id_list[index]);
      Serial.print(": ");
      Serial.println(http.errorToString(httpCode));
      http.end();
    }

    if (!cached_config_loaded[index])
    {
      load_cached_config_for_index_from_storage(index, (index == 0));
    }
    if (cached_config_loaded[index] && config_source_state[index] == "MISSING")
    {
      config_source_state[index] = "CACHED";
    }
    if (!cached_config_loaded[index])
    {
      config_source_state[index] = "ERROR";
    }
  }
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
    Serial.println("SD-Card: Initialization");
    apply_config_from_string(current_config_text);
  }
  else if (read_config_text_from_littlefs(current_config_text))
  {
    Serial.println("LittleFS: Using fallback /config.txt");
    apply_config_from_string(current_config_text);
  }
  else
  {
    current_config_text = "";
    Serial.println("Configuration File not Found on SD or LittleFS -- Using Defaults.");
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
  if (ram_only_mode)
  {
    if (read_system_id_from_littlefs())
    {
      logSystemIdState("System ID (RAM_ONLY/LittleFS): ");
    }
    else
    {
      clearSystemIdList();
      Serial.println("RAM_ONLY: System ID not found in LittleFS");
    }
  }
  else if (read_system_id_from_sd())
  {
    logSystemIdState("System ID: ");
  }
  else if (read_system_id_from_littlefs())
  {
    logSystemIdState("System ID (LittleFS): ");
  }
  else
  {
    clearSystemIdList();
    Serial.println("System ID not found on SD or LittleFS");
  }
  Serial.println("read_system_id: End");
}

bool bootstrap_config_from_server()
{
  HTTPClient http;
  String payload;
  int httpCode;
  String requestUrl;

  Serial.println("Bootstrap config check");
  tft.println("Bootstrap config check");

  if (updateurl == "")
  {
    updateurl = DEFAULT_UPDATE_URL;
    Serial.print("updateurl missing, using default: ");
    Serial.println(updateurl);
    tft.println("using default updateurl");
  }
  else
  {
    Serial.println("updateurl already exists, bootstrap skipped");
    return false;
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi not connected, bootstrap skipped");
    tft.println("WiFi not connected");
    return false;
  }

  requestUrl = build_update_request_url();
  http.begin(requestUrl);
  http.setTimeout(5000);
  httpCode = http.GET();

  if (httpCode <= 0)
  {
    Serial.print("HTTP GET failed: ");
    Serial.println(http.errorToString(httpCode));
    tft.println("HTTP GET failed");
    http.end();
    return false;
  }

  if (httpCode != HTTP_CODE_OK)
  {
    Serial.print("HTTP response code: ");
    Serial.println(httpCode);
    tft.println("HTTP bad response");
    http.end();
    return false;
  }

  payload = http.getString();
  http.end();

  if (payload.length() == 0)
  {
    Serial.println("Downloaded config is empty");
    tft.println("Downloaded config empty");
    return false;
  }

  bool changed = false;
  if (ram_only_mode)
  {
    if (current_config_text == payload)
    {
      Serial.println("Bootstrap config unchanged (RAM_ONLY)");
      tft.println("Bootstrap unchanged");
      return false;
    }
    current_config_text = payload;
    apply_config_from_string(current_config_text);
    Serial.println("Bootstrap config applied in RAM_ONLY mode");
    tft.println("Bootstrap applied (RAM_ONLY)");
    return true;
  }

  if (!sync_config_to_sd_and_memory(payload, changed))
  {
    Serial.println("Failed to write downloaded config");
    tft.println("Write config failed");
    return false;
  }

  if (!changed)
  {
    Serial.println("Bootstrap config unchanged");
    tft.println("Bootstrap unchanged");
    return false;
  }

  Serial.println("Bootstrap config written, rebooting");
  tft.println("Bootstrap written");
  delay(1000);
  ESP.restart();
  return true;
}

// Debug Touch Position
void printTouchToSerial(TS_Point p) 
{
  Serial.print("Pressure = ");
  Serial.print(p.z);
  Serial.print(", x = ");
  Serial.print(p.x);
  Serial.print(", y = ");
  Serial.print(p.y);
  Serial.println();
}

// Start and Config CYD
void setup() 
{
  Serial.begin(115200);
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
  list_littlefs_files_to_serial();

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

  if (ssid != "")
  {
    if (wifi_start_STA() == true)
    {
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
      if (timesync() == true)
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

void apply_runtime_NTP_config()
{
  if ((WiFi.status() == WL_CONNECTED) && (tzinfo != "") && (ntpserver != ""))
  {
    Serial.println("Reapplying TZ/NTP config");
    configTzTime(tzinfo.c_str(), ntpserver.c_str());
  }
}

bool poll_update_server()
{
  HTTPClient http;
  String payload;
  int httpCode;
  String requestUrl;
  String cachePath;
  int activeIndex = active_system_id_index;

  ensure_default_update_url();

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("Update check skipped: WiFi not connected");
    next_update_modular = min(next_update_modular,1440);
    return false;
  }

  Serial.print("Update check: ");
  requestUrl = build_update_request_url();
  Serial.println(requestUrl);
  
  http.begin(requestUrl);
  http.setTimeout(1000);
  httpCode = http.GET();

  if (httpCode <= 0)
  {
    Serial.print("Update GET failed: ");
    Serial.println(http.errorToString(httpCode));
    http.end();
    return false;
  }

  if (httpCode != HTTP_CODE_OK)
  {
    Serial.print("Update HTTP code: ");
    Serial.println(httpCode);
    http.end();
    return false;
  }

  payload = http.getString();
  http.end();

  if (payload.length() == 0)
  {
    Serial.println("Update check: empty payload");
    return false;
  }

  bool changed = !configContentEqualsNormalized(current_config_text, payload);

  if ((activeIndex >= 0) && (activeIndex < system_id_count))
  {
    bool cacheChanged = !cached_config_loaded[activeIndex] ||
                        !configContentEqualsNormalized(cached_config_texts[activeIndex], payload);
    cached_config_texts[activeIndex] = payload;
    cached_config_loaded[activeIndex] = true;
    config_source_state[activeIndex] = "LIVE";
    Serial.print("Config state ");
    Serial.print(system_id_list[activeIndex]);
    Serial.println(": LIVE");

    if (!ram_only_mode && cacheChanged)
    {
      cachePath = get_config_cache_path_for_id(system_id_list[activeIndex]);
      if (!write_text_file_to_sd(cachePath, payload))
      {
        Serial.print("Update check: failed to write ");
        Serial.println(cachePath);
      }
    }
  }

  if (!changed)
  {
    Serial.println("Update check: no config delta");
    reportScheduleEntriesForCurrentTime();
    return true;
  }

  current_config_text = payload;
  apply_current_config_with_runtime_state();
  reportScheduleEntriesForCurrentTime();

  return true;
}

void handleSystemIdSwitchTouch()
{
  struct tm localtime;

  if (!advanceActiveSystemId())
  {
    if (system_id_count <= 0)
    {
      Serial.println("System ID switch ignored: no IDs loaded");
    }
    else
    {
      Serial.println("System ID switch ignored: only one ID configured");
    }
    return;
  }

  Serial.print("Active System ID switched to ");
  Serial.print(system_id);
  Serial.print(" (");
  Serial.print(active_system_id_index + 1);
  Serial.print("/");
  Serial.print(system_id_count);
  Serial.println(")");

  if (!cached_config_loaded[active_system_id_index])
  {
    load_cached_config_for_index_from_storage(active_system_id_index, false);
  }

  if (cached_config_loaded[active_system_id_index])
  {
    current_config_text = cached_config_texts[active_system_id_index];
    apply_current_config_with_runtime_state();
    Serial.print("Switch apply source: ");
    Serial.println(config_source_state[active_system_id_index]);
  }
  else
  {
    Serial.println("Switch apply source: MISSING");
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("Immediate update check skipped: WiFi not connected");
  }
  else if (!poll_update_server())
  {
    Serial.println("Immediate update check failed (server unavailable or bad response)");
  }
  else
  {
    Serial.println("Immediate update check complete");
  }

  if (!getLocalTime(&localtime, 1000))
  {
    Serial.println("Immediate redraw skipped: current time unavailable");
    drawBuildAndSystemInfo();
    return;
  }

  // Only redraw the system/build lane here. The date lane is handled by its
  // normal periodic redraw path and should not be globally repainted on switch.
  drawBuildAndSystemInfo();
  renderActiveScheduleEntries(localtime);
}

void loop() 
{
  struct tm localtime;
  getLocalTime(&localtime);

  static char localtimeString[10]; // Buffer for time in HH:MM:SS format
  static char locaxtimeString[10]; // Buffer for time in HH MM format
  char dateString[40]; // Buffer for long translated month names

  // processAutoBrightness(localtime);
  processPhotoBrightness();
  
  // EVENT every hour
  if (localtime.tm_hour != event_tm_hour) {
    event_tm_hour = localtime.tm_hour;
    Serial.println("event_tm_hour");
    // LOCAL Date TT.MO.YYYY
    //sprintf(dateString, "%02d.%02d.%04d", localtime.tm_mday, localtime.tm_mon + 1, localtime.tm_year + 1900);
    snprintf(dateString, sizeof(dateString),
         "%s %d, %04d",
         MonthName[localtime.tm_mon].c_str(),
         localtime.tm_mday,
         localtime.tm_year + 1900);
    // Calculate the time zone based on the difference between local time and UTC

    tft.println("NTP Sync");
    configTzTime(tzinfo.c_str(), ntpserver.c_str()); // Synchronize ESP32 system time with NTP
    delay(1000);
    getLocalTime(&localtime);

    // Redraw the clock
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_BLACK, TFT_WHITE);
    tft.setCursor (0,0);
    
    // draw Date      
    tft.setTextColor(dateTextColor, dateBgColor);
    tft.drawString(dateString, 38, 0, 4);
    drawBuildAndSystemInfo();
    renderActiveScheduleEntries(localtime);
    
  }

  // EVENT every min
  if (localtime.tm_min != event_tm_min)
  {
    event_tm_min = localtime.tm_min;
    Serial.println("event_tm_min");
    renderActiveScheduleEntries(localtime);
  }

  // EVENT every sec
  if (localtime.tm_sec != event_tm_sec)
  {
    unsigned long now_ms = millis();

    // Poll the remote server for an update to config.txt
    if (now_ms >= next_update_check)
    {
      if ((localtime.tm_sec % next_update_modular) == 0)
      {
        poll_update_server();
        next_update_check = now_ms + 1000;
      }
    }
    
    event_tm_sec = localtime.tm_sec;
    // Serial.println("event_tm_sec");
    // LOCAL Time .HH:MM:SS
    if (tformat == "24")
    {
      // Define localtimeString and locaxtimeString as the formatted time for 24h format
      snprintf(localtimeString, sizeof(localtimeString), "%02d:%02d", localtime.tm_hour, localtime.tm_min);
      snprintf(locaxtimeString, sizeof(locaxtimeString), "%02d %02d", localtime.tm_hour, localtime.tm_min);
    }
    else
    {
      int hour12 = localtime.tm_hour % 12;
      if (hour12 == 0) { hour12 = 12; }
      // Define localtimeString and locaxtimeString as the formatted time for 12h format
      snprintf(localtimeString, sizeof(localtimeString), "%2d:%02d", hour12, localtime.tm_min);
      snprintf(locaxtimeString, sizeof(locaxtimeString), "%2d %02d", hour12, localtime.tm_min);
    }
    // draw Time to CLOCK
    // without Flicker with Sprite
    TFT_eSprite sprite = TFT_eSprite(&tft);
    sprite.createSprite(318, 61);
    sprite.fillSprite(TFT_BLACK);  // triple zero

    sprite.setFreeFont(&DSEG14_Classic_Regular_60);
    //sprite.setFreeFont(&seven_regular31pt7b);
    sprite.setTextColor(clockTextColor);  // no background overwrite

    // Set text alignment to middle-center
    sprite.setTextDatum(MC_DATUM);

    // Draw centered inside sprite
    if (localtime.tm_sec % 2 == 0) {
      sprite.drawString(localtimeString, sprite.width() / 2, sprite.height() / 2);
    } else {
      sprite.drawString(locaxtimeString, sprite.width() / 2, sprite.height() / 2);
    }

    sprite.pushSprite(1, 68);
    sprite.deleteSprite();
      
  }

  // EVENT Pen touch
  #if ENABLE_TOUCH
  if (touch_ready && ts.tirqTouched() && ts.touched())  {
    TS_Point p = ts.getPoint();
    printTouchToSerial(p);

    if (p.y > 3200)
    {
      if (p.x < 800)
      {
        int target = computePhotoTargetBrightness(analogRead(photoResistorPin));
        setBrightnessFromController(mindim, "Instant min dim", true, target);
      }
      else if (p.x > 3200)
      {
        int target = computePhotoTargetBrightness(analogRead(photoResistorPin));
        setBrightnessFromController(maxdim, "Instant max dim", true, target);
      }
      delay(200);
      return;
    }

    // Adjust brightness
    // Top part of the screen
    if (p.y < 800) {
      if ((p.x >= 1200) && (p.x <= 2800))
      {
        handleSystemIdSwitchTouch();
        delay(300);
        return;
      }

      int brightness_step = 32;
      if (brightness < 64) { brightness_step = 16; }
      if (brightness < 32) { brightness_step = 8;  }
      if (brightness < 16) { brightness_step = 4;  }
      if (brightness < 8)  { brightness_step = 2;  }
      if (brightness < 4)  { brightness_step = 1;  }
      // Top-Left of the screen
      if (p.x < 800) {
        setBrightnessFromController(brightness - brightness_step, "Brightness", true);
      }
      // Top-right of the screen
      if (p.x > 3200) {
        setBrightnessFromController(brightness + brightness_step, "Brightness", true);
      }
    }
    delay(300);
  }
  #endif
}


//end
