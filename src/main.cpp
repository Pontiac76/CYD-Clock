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
int brightness = 128; // Brightness (0-255)
int mindim = 32;
int maxdim = 128;
int hourspan = 1;
String sunrise_time = "06:00";
String sunset_time = "18:00";
unsigned long next_auto_dim_ms = 0;
unsigned long auto_dim_resume_ms = 0;
int autodim_hold_ms = 2000;
int autodim_step_ms = 1000;
int autodim_percent = 10;
bool autodim_debug = false;
unsigned long next_autodim_debug_ms = 0;

// RGB conversion
#define RGB565(r, g, b) (((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F))

// Default URL to pull config.txt from -- Hard coding is bad m'kay... don't follow this example
String updateurl;
#define DEFAULT_UPDATE_URL "http://192.168.4.2:8080"

constexpr int WEEKDAY_COUNT = 7;
constexpr int MONTH_COUNT = 12;
constexpr int MAX_TRANSLATION_LENGTH = 24;
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
  int nextValue = brightness;
  int gap = abs(target - brightness);
  int step = max(1, (gap * autodim_percent) / 100);
  if (brightness < target)
  {
    nextValue = brightness + step;
    if (nextValue > target)
    {
      nextValue = target;
    }
  }
  else if (brightness > target)
  {
    nextValue = brightness - step;
    if (nextValue < target)
    {
      nextValue = target;
    }
  }

  if (nextValue != brightness)
  {
    applyBrightnessValue(nextValue);
    if (autodim_debug)
    {
      Serial.print("AutoDim STEP current=");
      Serial.print(brightness);
      Serial.print(" target=");
      Serial.println(target);
    }
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

  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, createColor(0, 0, 90));
  tft.setTextDatum(TR_DATUM);
  tft.drawString(idText, 318, 2, 1);
  tft.drawString(build_version_code, 318, 14, 1);
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

bool read_config_text_from_sd(String &content)
{
  File configFile;
  bool success = false;

  content = "";
  if (!begin_sd_session())
  {
    goto read_config_text_from_sd_exit;
  }

  configFile = SD.open("/config.txt");
  if (!configFile)
  {
    goto read_config_text_from_sd_exit;
  }

  while (configFile.available())
  {
    content += char(configFile.read());
  }

  configFile.close();
  success = true;

read_config_text_from_sd_exit:
  end_sd_session();
  return success;
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
  rawSystemId.trim();
  system_id = sanitizeSystemId(rawSystemId);
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

  rawSystemId.trim();
  system_id = sanitizeSystemId(rawSystemId);
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
  String requestUrl = updateurl;

  if (system_id == "")
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

  requestUrl += system_id;
  return requestUrl;
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
  Serial.println("read_system_id: Begin");
  if (ram_only_mode)
  {
    if (read_system_id_from_littlefs())
    {
      if (system_id != "")
      {
        Serial.print("System ID (RAM_ONLY/LittleFS): ");
        Serial.println(system_id);
      }
      else
      {
        Serial.println("System ID file empty after sanitization (RAM_ONLY/LittleFS)");
      }
    }
    else
    {
      system_id = "";
      Serial.println("RAM_ONLY: System ID not found in LittleFS");
    }
  }
  else if (read_system_id_from_sd())
  {
    if (system_id != "")
    {
      Serial.print("System ID: ");
      Serial.println(system_id);
    }
    else
    {
      Serial.println("System ID file empty after sanitization");
    }
  }
  else if (read_system_id_from_littlefs())
  {
    if (system_id != "")
    {
      Serial.print("System ID (LittleFS): ");
      Serial.println(system_id);
    }
    else
    {
      Serial.println("System ID file empty after sanitization (LittleFS)");
    }
  }
  else
  {
    system_id = "";
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
  tft.setTextColor(createColor(128, 255, 128), TFT_BLACK);
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

  read_sd();
  read_system_id();
  list_littlefs_files_to_serial();

  // Backlight after config read
  pinMode(backlightPin, OUTPUT);
  analogWrite(backlightPin, brightness);

  if (ssid != "")
  {
    if (wifi_start_STA() == true)
    {
      bootstrap_config_from_server();

      Serial.println("Time Sync ...");
      tft.println("Time Sync ...");
      if (timesync() == true)
      {
        Serial.println("Time Sync Ready");
        tft.println("Time Sync Ready");
      }
      else
      {
        tft.setTextColor(createColor(255, 128, 128), TFT_BLACK);
        Serial.println("non Time sync");
        tft.println("non Time sync");
        delay(3000);
      }
    }
    else
    {
      tft.setTextColor(createColor(255, 128, 128), TFT_BLACK);
      Serial.println("non WiFi connect");
      tft.println("non WiFi connect");
      delay(3000);
    }
  }
  else
  {
    tft.setTextColor(createColor(255, 128, 128), TFT_BLACK);
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

  String old_ssid = ssid;
  String old_password = password;
  String old_tzinfo = tzinfo;
  String old_ntpserver = ntpserver;
  String old_tformat = tformat;
  String old_updateurl = updateurl;
  String oldWeekDays[WEEKDAY_COUNT];
  String oldMonthName[MONTH_COUNT];

  for (int index = 0; index < WEEKDAY_COUNT; ++index)
  {
    oldWeekDays[index] = WeekDays[index];
  }

  for (int index = 0; index < MONTH_COUNT; ++index)
  {
    oldMonthName[index] = MonthName[index];
  }

  if (updateurl == "")
  {
    Serial.println("Update check skipped: no updateurl");
    next_update_modular = min(next_update_modular,1440);
    return false;
  }

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

  bool changed = false;
  if (ram_only_mode)
  {
    if (current_config_text == payload)
    {
      Serial.println("Update check: no config changes (RAM_ONLY)");
      changed = false;
    }
    else
    {
      current_config_text = payload;
      changed = true;
      Serial.println("Update check: RAM_ONLY payload applied in memory");
    }
  }
  else if (!sync_config_to_sd_and_memory(payload, changed))
  {
    Serial.println("Update check: failed to sync config, applying volatile RAM config");
    current_config_text = payload;
    changed = true;
  }

  if (!changed)
  {
    reportScheduleEntriesForCurrentTime();
    return true;
  }

  int runtimeBrightnessBeforeApply = brightness;
  apply_config_from_string(current_config_text);
  // Keep the current runtime backlight level stable across config reloads.
  // This avoids jumping to the configured static brightness on every update.
  brightness = runtimeBrightnessBeforeApply;
  reportScheduleEntriesForCurrentTime();

  if ((tzinfo != old_tzinfo) || (ntpserver != old_ntpserver))
  {
    apply_runtime_NTP_config();
    event_tm_hour = -1;
    event_tm_min = -1;
    event_tm_sec = -1;
  }

  if (!stringArrayEquals(WeekDays, oldWeekDays, WEEKDAY_COUNT) ||
      !stringArrayEquals(MonthName, oldMonthName, MONTH_COUNT) ||
      (tformat != old_tformat))
  {
    event_tm_hour = -1;
    event_tm_min = -1;
    event_tm_sec = -1;
  }

  if (updateurl != old_updateurl)
  {
    Serial.print("updateurl changed to ");
    Serial.println(updateurl);
  }

  if ((ssid != old_ssid) || (password != old_password))
  {
    Serial.println("WiFi credentials changed");
    Serial.println("Reconnect will be needed");
  }

  return true;
}

void loop() 
{
  struct tm localtime;
  getLocalTime(&localtime);

  static char localtimeString[10]; // Buffer for time in HH:MM:SS format
  static char locaxtimeString[10]; // Buffer for time in HH MM format
  char dateString[40]; // Buffer for long translated month names

  processAutoBrightness(localtime);
  
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
    tft.setTextColor(TFT_WHITE, createColor(0, 0, 90));
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
    sprite.setTextColor(TFT_RED);  // no background overwrite

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
        applyBrightnessValue(mindim);
        auto_dim_resume_ms = millis() + autodim_hold_ms;
        Serial.print("Instant min dim -> ");
        Serial.print(brightness);
        Serial.print(" target=");
        Serial.println(computeAutoTargetBrightness(localtime));
      }
      else if (p.x > 3200)
      {
        applyBrightnessValue(maxdim);
        auto_dim_resume_ms = millis() + autodim_hold_ms;
        Serial.print("Instant max dim -> ");
        Serial.print(brightness);
        Serial.print(" target=");
        Serial.println(computeAutoTargetBrightness(localtime));
      }
      delay(200);
      return;
    }

    // Adjust brightness
    // Top part of the screen
    if (p.y < 800) {
      int brightness_step = 32;
      if (brightness < 64) { brightness_step = 16; }
      if (brightness < 32) { brightness_step = 8;  }
      if (brightness < 16) { brightness_step = 4;  }
      if (brightness < 8)  { brightness_step = 2;  }
      if (brightness < 4)  { brightness_step = 1;  }
      // Top-Left of the screen
      if (p.x < 800) {
        brightness = brightness - brightness_step;
      }
      // Top-right of the screen
      if (p.x > 3200) {
        brightness = brightness + brightness_step;
      }
      // Clamp the brightness - Never less than 1, never more than 255
      brightness = min(255,max(1,brightness));
      applyBrightnessValue(brightness);
      auto_dim_resume_ms = millis() + autodim_hold_ms;
      Serial.print("Brightness=");
      Serial.println(brightness);
    }
    delay(300);
  }
  #endif
}


//end
