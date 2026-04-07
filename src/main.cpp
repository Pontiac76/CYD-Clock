#include "Arduino.h"
#include <SD.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <time.h>
#include <TimeLib.h>
//#include <sunset.h>
#include <Wire.h>
#include "seven_regular11pt7b.h"
#include "seven_regular31pt7b.h"
#include "DSEG14_Classic_Regular_60.h"
#include <HTTPClient.h>

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

// RGB conversion
#define RGB565(r, g, b) (((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F))

// Timer Elapsed Constant
#define HAS_TIMER_ELAPSED 0

// Default URL to pull config.txt from -- Hard coding is bad m'kay... don't follow this example
String updateurl;
#define DEFAULT_UPDATE_URL "http://192.168.4.2:8080"

constexpr int WEEKDAY_COUNT = 7;
constexpr int MONTH_COUNT = 12;
constexpr int MAX_TRANSLATION_LENGTH = 24;
constexpr int MAX_SCHEDULE_ENTRIES = 32;
constexpr int MAX_VISIBLE_SCHEDULE_ENTRIES = 3;
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
String latitude;
String longitude;
String WeekDays[WEEKDAY_COUNT] = {
  "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"
};
String MonthName[MONTH_COUNT] = {
  "January", "February", "March", "April", "May", "June",
  "July", "August", "September", "October", "November", "December"
};
String ScheduleEntries[MAX_SCHEDULE_ENTRIES];
String Dummy;
String current_config_text;
String system_id;
bool sd_ready = false;
bool touch_ready = false;
bool touch_initialized = false;

int yy_mem = 0;
int mm_mem = 0;

int event_tm_hour = -1;
int event_tm_min = -1;
int event_tm_sec = -1;

int next_update_modular = 15;

void apply_config_from_string(String content);
bool shouldShowScheduleEntry(const String &entry, const struct tm &localtime);

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

// Calculate the weekday according to ISO 8601 (1 = Mon, 2 = Tue, 3 = Wed, 4 = Thu, 5 = Fri, 6 = Sat, 7 = Sun)
uint8_t GetWeekday(uint16_t y, uint8_t m, uint8_t d) 
{
  static int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  y -= m < 3;
  uint8_t wd = (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
  return (wd == 0 ? 7 : wd);
}

// Check whether the year is a leap year
bool IsLeapYear(uint16_t y) 
{
  return  !(y % 4) && ((y % 100) || !(y % 400)); // Leap year calculation (true = leap year, false = not a leap year)
}

// Number of days in the month
uint8_t GetDaysOfMonth(uint16_t y, uint8_t m) 
{
  static const uint8_t mdays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m == 2 && IsLeapYear(y)) {
    return 29;
  } else {
    return mdays[m - 1];
  }
}

// Calculate the week number according to ISO 8601
uint16_t GetDayOfYear(uint16_t y, uint8_t m, uint8_t d);
uint8_t GetWeekNumber(uint16_t y, uint8_t m, uint8_t d) 
{
  bool LeapYear;
  uint16_t doy = GetDayOfYear(y, m, d);  // Determine the day number within the year
  uint8_t wd = GetWeekday(y, m, d);      // Determine the weekday
  uint8_t wnr = (doy - wd + 10) / 7;     // Calculate the week number (adjusted)

  if (wnr == 0) {                        // If the week number is zero, the day falls at the start of the year (special case 1)
    wd = GetWeekday(y - 1, 12, 31);      // Determine the last weekday of the previous year
    LeapYear = IsLeapYear(y - 1);        // Determine whether the previous year was a leap year
    if (wd < 4) {                        // If December 31 falls before Thursday, then...
      wnr = 1;                           // it belongs to the first week of the year
    } else {                             // otherwise determine whether there is a 53rd week (special case 3)
      wnr = ((wd == 4) || (LeapYear && wd == 5)) ? 53 : 52;
    }
  } else if (wnr == 53) {                // If the week number is 53, the day falls at the end of the year (special case 2)
    wd = GetWeekday(y, 12, 31);          // Determine the last weekday of this year
    LeapYear = IsLeapYear(y);            // Determine whether this year is a leap year
    if (wd < 4) {                        // If December 31 falls before Thursday, then...
      wnr = 1;                           // it belongs to the first week of the next year
    } else {                             // otherwise determine whether there is a 53rd week (special case 3)
      wnr = ((wd == 4) || (LeapYear && wd == 5)) ? 53 : 52;
    }
  }
  return wnr;
}

// Calculate the number of days (day of the year)
uint16_t GetDayOfYear(uint16_t y, uint8_t m, uint8_t d) 
{
  static const uint16_t mdays[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  return d + mdays[m - 1] + (m >= 2 && IsLeapYear(y));
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

int parseScheduleIndex(const String &normalizedKey)
{
  const String prefix = "schedule";
  String indexText;

  if (!normalizedKey.startsWith(prefix))
  {
    return -1;
  }

  indexText = normalizedKey.substring(prefix.length());
  if (indexText == "")
  {
    return -1;
  }

  for (int index = 0; index < indexText.length(); ++index)
  {
    if (!isDigit(indexText.charAt(index)))
    {
      return -1;
    }
  }

  int parsedIndex = indexText.toInt();
  if ((parsedIndex < 0) || (parsedIndex >= MAX_SCHEDULE_ENTRIES))
  {
    return -1;
  }

  return parsedIndex;
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

bool parseTimeSpec(const String &value, int &minutesSinceMidnight, bool &wildcard)
{
  int separatorIndex;
  String hourPart;
  String minutePart;

  wildcard = false;
  minutesSinceMidnight = 0;

  if (value == "*")
  {
    wildcard = true;
    return true;
  }

  separatorIndex = value.indexOf(':');
  if (separatorIndex == -1)
  {
    return false;
  }

  hourPart = value.substring(0, separatorIndex);
  minutePart = value.substring(separatorIndex + 1);
  hourPart.trim();
  minutePart.trim();

  for (int index = 0; index < hourPart.length(); ++index)
  {
    if (!isDigit(hourPart.charAt(index)))
    {
      return false;
    }
  }

  for (int index = 0; index < minutePart.length(); ++index)
  {
    if (!isDigit(minutePart.charAt(index)))
    {
      return false;
    }
  }

  int hour = hourPart.toInt();
  int minute = minutePart.toInt();
  if ((hour < 0) || (hour > 23) || (minute < 0) || (minute > 59))
  {
    return false;
  }

  minutesSinceMidnight = (hour * 60) + minute;
  return true;
}

String formatMinutesForDisplay(int minutesSinceMidnight)
{
  char buffer[16];
  int hour = minutesSinceMidnight / 60;
  int minute = minutesSinceMidnight % 60;

  if (tformat == "24")
  {
    snprintf(buffer, sizeof(buffer), "%02d:%02d", hour, minute);
    return String(buffer);
  }

  int hour12 = hour % 12;
  const char *suffix = (hour < 12) ? "am" : "pm";
  if (hour12 == 0)
  {
    hour12 = 12;
  }

  snprintf(buffer, sizeof(buffer), "%2d:%02d%s", hour12, minute, suffix);
  return String(buffer);
}

String formatScheduleDisplayRange(const String &entry)
{
  int firstSeparator = entry.indexOf('|');
  int secondSeparator;
  int thirdSeparator;
  String startSpec;
  String endSpec;
  int startMinutes = 0;
  int endMinutes = 0;
  bool startWildcard = false;
  bool endWildcard = false;

  if (firstSeparator == -1)
  {
    return "";
  }

  secondSeparator = entry.indexOf('|', firstSeparator + 1);
  if (secondSeparator == -1)
  {
    return "";
  }

  thirdSeparator = entry.indexOf('|', secondSeparator + 1);
  if (thirdSeparator == -1)
  {
    return "";
  }

  startSpec = entry.substring(firstSeparator + 1, secondSeparator);
  endSpec = entry.substring(secondSeparator + 1, thirdSeparator);
  startSpec.trim();
  endSpec.trim();

  if (!parseTimeSpec(startSpec, startMinutes, startWildcard) ||
      !parseTimeSpec(endSpec, endMinutes, endWildcard))
  {
    return "";
  }

  if (startWildcard && endWildcard)
  {
    return "All Day        ";
  }

  if (startWildcard)
  {
    return String("Until ") + formatMinutesForDisplay(endMinutes) + String("  ");
  }

  if (endWildcard)
  {
    return String("From ") + formatMinutesForDisplay(startMinutes) + String("   ");
  }

  return formatMinutesForDisplay(startMinutes) + "-" + formatMinutesForDisplay(endMinutes);
}

String extractScheduleTitle(const String &entry)
{
  int firstSeparator = entry.indexOf('|');
  int secondSeparator;
  int thirdSeparator;

  if (firstSeparator == -1)
  {
    return entry;
  }

  secondSeparator = entry.indexOf('|', firstSeparator + 1);
  if (secondSeparator == -1)
  {
    return entry;
  }

  thirdSeparator = entry.indexOf('|', secondSeparator + 1);
  if (thirdSeparator == -1)
  {
    return entry;
  }

  String title = entry.substring(thirdSeparator + 1);
  title.trim();
  return title;
}

String buildScheduleDisplayText(const String &entry)
{
  String displayRange = formatScheduleDisplayRange(entry);
  String scheduleTitle = extractScheduleTitle(entry);

  if (displayRange == "")
  {
    return scheduleTitle;
  }

  return "[" + displayRange + "] " + scheduleTitle;
}

bool parseScheduleTimeWindow(const String &entry, int &startMinutes, bool &startWildcard, int &endMinutes, bool &endWildcard)
{
  int firstSeparator = entry.indexOf('|');
  int secondSeparator;
  int thirdSeparator;
  String startSpec;
  String endSpec;

  startMinutes = 0;
  endMinutes = 0;
  startWildcard = false;
  endWildcard = false;

  if (firstSeparator == -1)
  {
    return false;
  }

  secondSeparator = entry.indexOf('|', firstSeparator + 1);
  if (secondSeparator == -1)
  {
    return false;
  }

  thirdSeparator = entry.indexOf('|', secondSeparator + 1);
  if (thirdSeparator == -1)
  {
    return false;
  }

  startSpec = entry.substring(firstSeparator + 1, secondSeparator);
  endSpec = entry.substring(secondSeparator + 1, thirdSeparator);
  startSpec.trim();
  endSpec.trim();

  return parseTimeSpec(startSpec, startMinutes, startWildcard) &&
         parseTimeSpec(endSpec, endMinutes, endWildcard);
}

int getScheduleSortRank(const String &entry)
{
  int startMinutes = 0;
  int endMinutes = 0;
  bool startWildcard = false;
  bool endWildcard = false;

  if (!parseScheduleTimeWindow(entry, startMinutes, startWildcard, endMinutes, endWildcard))
  {
    return 3;
  }

  if (startWildcard && endWildcard)
  {
    return 0;
  }

  if (startWildcard)
  {
    return 1;
  }

  return 2;
}

bool shouldScheduleEntrySortBefore(const String &leftEntry, const String &rightEntry)
{
  int leftRank = getScheduleSortRank(leftEntry);
  int rightRank = getScheduleSortRank(rightEntry);
  int leftStartMinutes = 0;
  int rightStartMinutes = 0;
  int leftEndMinutes = 0;
  int rightEndMinutes = 0;
  bool leftStartWildcard = false;
  bool rightStartWildcard = false;
  bool leftEndWildcard = false;
  bool rightEndWildcard = false;
  String leftTitle;
  String rightTitle;

  if (leftRank != rightRank)
  {
    return leftRank < rightRank;
  }

  parseScheduleTimeWindow(leftEntry, leftStartMinutes, leftStartWildcard, leftEndMinutes, leftEndWildcard);
  parseScheduleTimeWindow(rightEntry, rightStartMinutes, rightStartWildcard, rightEndMinutes, rightEndWildcard);

  if (leftRank == 2 && (leftStartMinutes != rightStartMinutes))
  {
    return leftStartMinutes < rightStartMinutes;
  }

  leftTitle = extractScheduleTitle(leftEntry);
  rightTitle = extractScheduleTitle(rightEntry);
  leftTitle.trim();
  rightTitle.trim();
  leftTitle.toLowerCase();
  rightTitle.toLowerCase();

  return leftTitle < rightTitle;
}

int collectSortedVisibleScheduleIndices(const struct tm &localtime, int destination[], int capacity)
{
  int count = 0;

  for (int index = 0; index < MAX_SCHEDULE_ENTRIES; ++index)
  {
    if (ScheduleEntries[index] == "")
    {
      continue;
    }

    if (!shouldShowScheduleEntry(ScheduleEntries[index], localtime))
    {
      continue;
    }

    if (count < capacity)
    {
      destination[count] = index;
      ++count;
    }
  }

  for (int left = 0; left < count - 1; ++left)
  {
    for (int right = left + 1; right < count; ++right)
    {
      if (shouldScheduleEntrySortBefore(ScheduleEntries[destination[right]], ScheduleEntries[destination[left]]))
      {
        int temp = destination[left];
        destination[left] = destination[right];
        destination[right] = temp;
      }
    }
  }

  return count;
}

bool isCurrentTimeInRange(const struct tm &localtime, const String &startSpec, const String &endSpec)
{
  int currentMinutes = (localtime.tm_hour * 60) + localtime.tm_min;
  int startMinutes = 0;
  int endMinutes = 0;
  bool startWildcard = false;
  bool endWildcard = false;

  if (!parseTimeSpec(startSpec, startMinutes, startWildcard) ||
      !parseTimeSpec(endSpec, endMinutes, endWildcard))
  {
    return false;
  }

  if (startWildcard && endWildcard)
  {
    return true;
  }

  if (startWildcard)
  {
    return currentMinutes <= endMinutes;
  }

  if (endWildcard)
  {
    return currentMinutes >= startMinutes;
  }

  if (startMinutes <= endMinutes)
  {
    return (currentMinutes >= startMinutes) && (currentMinutes <= endMinutes);
  }

  return (currentMinutes >= startMinutes) || (currentMinutes <= endMinutes);
}

int parseIsoDateToDayNumber(const String &value)
{
  int year;
  int month;
  int day;

  if (sscanf(value.c_str(), "%d-%d-%d", &year, &month, &day) != 3)
  {
    return -1;
  }

  return year * 10000 + month * 100 + day;
}

bool matchesMonthDay(const struct tm &localtime, const String &value)
{
  int month;
  int day;

  if (sscanf(value.c_str(), "%d-%d", &month, &day) != 2)
  {
    return false;
  }

  return ((localtime.tm_mon + 1) == month) && (localtime.tm_mday == day);
}

bool matchesDayOfWeek(const struct tm &localtime, String value)
{
  static const char *DAY_NAMES[] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };

  value.trim();
  value.toUpperCase();
  if (value.length() >= 3)
  {
    value = value.substring(0, 3);
  }
  return value == DAY_NAMES[localtime.tm_wday];
}

long calculateDayDeltaFromAnchor(const struct tm &localtime, int year, int month, int day)
{
  struct tm anchorTime = {};
  struct tm currentTime = localtime;
  time_t anchorEpoch;
  time_t currentEpoch;

  anchorTime.tm_year = year - 1900;
  anchorTime.tm_mon = month - 1;
  anchorTime.tm_mday = day;
  anchorTime.tm_hour = 12;
  anchorTime.tm_min = 0;
  anchorTime.tm_sec = 0;
  anchorTime.tm_isdst = -1;

  currentTime.tm_hour = 12;
  currentTime.tm_min = 0;
  currentTime.tm_sec = 0;
  currentTime.tm_isdst = -1;

  anchorEpoch = mktime(&anchorTime);
  currentEpoch = mktime(&currentTime);
  if ((anchorEpoch == -1) || (currentEpoch == -1))
  {
    return -1;
  }

  return long((currentEpoch - anchorEpoch) / 86400);
}

bool matchesDailyRule(const struct tm &localtime, String ruleValue)
{
  int intervalDays;
  long dayDelta;

  ruleValue.trim();
  if (ruleValue == "")
  {
    return true;
  }
  intervalDays = ruleValue.toInt();
  if (intervalDays <= 0)
  {
    return false;
  }

  dayDelta = calculateDayDeltaFromAnchor(localtime, 1970, 1, 1);
  if (dayDelta < 0)
  {
    return false;
  }

  return ((dayDelta % intervalDays) == 0);
}

bool matchesModRule(const struct tm &localtime, String ruleValue)
{
  int firstSeparator = ruleValue.indexOf(':');
  int intervalDays;
  int anchorDayNumber;
  long dayDelta;

  if (firstSeparator == -1)
  {
    intervalDays = ruleValue.toInt();
    if (intervalDays <= 0)
    {
      return false;
    }

    dayDelta = calculateDayDeltaFromAnchor(localtime, 1970, 1, 1);
    if (dayDelta < 0)
    {
      return false;
    }

    return ((dayDelta % intervalDays) == 0);
  }

  intervalDays = ruleValue.substring(0, firstSeparator).toInt();
  if (intervalDays <= 0)
  {
    return false;
  }

  anchorDayNumber = parseIsoDateToDayNumber(ruleValue.substring(firstSeparator + 1));
  if (anchorDayNumber == -1)
  {
    return false;
  }

  dayDelta = calculateDayDeltaFromAnchor(
    localtime,
    anchorDayNumber / 10000,
    (anchorDayNumber / 100) % 100,
    anchorDayNumber % 100
  );
  if (dayDelta < 0)
  {
    return false;
  }

  return ((dayDelta % intervalDays) == 0);
}

bool doesScheduleEntryMatchDate(const struct tm &localtime, String ruleSpec)
{
  int separatorIndex = ruleSpec.indexOf(':');
  String ruleName = ruleSpec;
  String ruleValue = "";
  int currentDayNumber;

  if (separatorIndex != -1)
  {
    ruleName = ruleSpec.substring(0, separatorIndex);
    ruleValue = ruleSpec.substring(separatorIndex + 1);
  }

  ruleName.trim();
  ruleName.toLowerCase();
  ruleValue.trim();

  if (ruleName == "daily")
  {
    return matchesDailyRule(localtime, ruleValue);
  }

  if (ruleName == "dow")
  {
    return matchesDayOfWeek(localtime, ruleValue);
  }

  if (ruleName == "date")
  {
    currentDayNumber = (localtime.tm_year + 1900) * 10000 + (localtime.tm_mon + 1) * 100 + localtime.tm_mday;
    if (parseIsoDateToDayNumber(ruleValue) != -1)
    {
      return currentDayNumber == parseIsoDateToDayNumber(ruleValue);
    }

    return matchesMonthDay(localtime, ruleValue);
  }

  if (ruleName == "mod")
  {
    return matchesModRule(localtime, ruleValue);
  }

  return false;
}

bool shouldShowScheduleEntry(const String &entry, const struct tm &localtime)
{
  int firstSeparator = entry.indexOf('|');
  int secondSeparator;
  int thirdSeparator;
  String ruleSpec;
  String startSpec;
  String endSpec;

  if (firstSeparator == -1)
  {
    return false;
  }

  secondSeparator = entry.indexOf('|', firstSeparator + 1);
  if (secondSeparator == -1)
  {
    return false;
  }

  thirdSeparator = entry.indexOf('|', secondSeparator + 1);
  if (thirdSeparator == -1)
  {
    return false;
  }

  ruleSpec = entry.substring(0, firstSeparator);
  startSpec = entry.substring(firstSeparator + 1, secondSeparator);
  endSpec = entry.substring(secondSeparator + 1, thirdSeparator);

  ruleSpec.trim();
  startSpec.trim();
  endSpec.trim();

  return doesScheduleEntryMatchDate(localtime, ruleSpec) &&
         isCurrentTimeInRange(localtime, startSpec, endSpec);
}

void clearScheduleEntries()
{
  for (int index = 0; index < MAX_SCHEDULE_ENTRIES; ++index)
  {
    ScheduleEntries[index] = "";
  }
}

void printScheduleEntriesToSerial(const struct tm *localtime = nullptr)
{
  int printedCount = 0;

  Serial.println("Loaded alarms:");
  for (int index = 0; index < MAX_SCHEDULE_ENTRIES; ++index)
  {
    if (ScheduleEntries[index] == "")
    {
      continue;
    }

    Serial.print("  schedule");
    Serial.print(index);
    Serial.print("=");
    Serial.print(buildScheduleDisplayText(ScheduleEntries[index]));
    if (localtime != nullptr)
    {
      Serial.print(" -> ");
      Serial.println(shouldShowScheduleEntry(ScheduleEntries[index], *localtime) ? "SHOW" : "hide");
    }
    else
    {
      Serial.println();
    }
    ++printedCount;
  }

  if (printedCount == 0)
  {
    Serial.println("  <none>");
  }
}

void renderActiveScheduleEntries(const struct tm &localtime)
{
  const int boxX = 6;
  const int boxY = 144;
  const int boxW = 308;
  const int lineHeight = 32;
  const int boxH = 240 - boxY;
  int visibleIndices[MAX_SCHEDULE_ENTRIES];
  int visibleCount = collectSortedVisibleScheduleIndices(localtime, visibleIndices, MAX_SCHEDULE_ENTRIES);
  int drawnCount = 0;

  tft.fillRect(boxX, boxY, boxW, boxH, TFT_BLACK);
  tft.setTextColor(createColor(255, 220, 160), TFT_BLACK);
  tft.setTextFont(2);
  tft.setTextSize(2);

  for (int visibleIndex = 0; visibleIndex < visibleCount; ++visibleIndex)
  {
    String displayText;
    int index = visibleIndices[visibleIndex];

    displayText = extractScheduleTitle(ScheduleEntries[index]);
    tft.drawString(displayText, boxX, boxY + (drawnCount * lineHeight), 2);
    ++drawnCount;

    if (drawnCount >= MAX_VISIBLE_SCHEDULE_ENTRIES)
    {
      break;
    }
  }

  tft.setTextSize(1);
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
  } else if (configKeyEquals(key, "latitude")) {
    latitude = value;
  } else if (configKeyEquals(key, "longitude")) {
    longitude = value;
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

void reportScheduleEntriesForCurrentTime()
{
  struct tm localtime;

  if (!getLocalTime(&localtime, 1000))
  {
    Serial.println("Loaded alarms: unable to evaluate current time");
    return;
  }

  printScheduleEntriesToSerial(&localtime);
}

// read config.txt from SD Card
void read_sd()
{
  Serial.println("read_sd: Begin");
  if (read_config_text_from_sd(current_config_text))
  {
    Serial.println("SD-Card: Initialization");
    apply_config_from_string(current_config_text);
  }
  else
  {
    current_config_text = "";
    Serial.println("Configuration File not Found -- Using Defaults.");
  }
  Serial.println("read_sd: End");
}

void read_system_id()
{
  Serial.println("read_system_id: Begin");
  if (read_system_id_from_sd())
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
  else
  {
    Serial.println("System ID not found");
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

// Soft Timer for loop
bool SoftTimer(unsigned long time_period_set)
{
  static unsigned long time_start_ms;
  static unsigned long time_period_ms;
  static bool time_flag = false;
  bool bflag = false;

  if (time_period_set != 0)
  {
    time_start_ms = millis();
    time_period_ms = time_period_set;
    time_flag = true;
    // Serial.println(time_start_ms);
    // Serial.println(time_period_ms);
  }
  else
  {
    if (time_flag && ((millis() - time_start_ms) >= time_period_ms))
    {
      time_flag = false;
      bflag = true;
    }
  }
  return bflag;
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

  read_sd();
  read_system_id();

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
  if (!sync_config_to_sd_and_memory(payload, changed))
  {
    Serial.println("Update check: failed to sync config");
    return false;
  }

  if (!changed)
  {
    reportScheduleEntriesForCurrentTime();
    return true;
  }

  apply_config_from_string(current_config_text);
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
  time_t now;
  time(&now);
  struct tm *utctime = gmtime(&now);

  static char localtimeString[10]; // Buffer for time in HH:MM:SS format
  static char locaxtimeString[10]; // Buffer for time in HH MM format
  char utctimeString[7]; // Buffer for time in HH:MM format
  char dateString[40]; // Buffer for long translated month names
  char hourString[3]; //HH
  int timeZone;
  
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

    //timeZone = (mktime(&localtime) - mktime(utctime)) / 3600; // Difference in hours
    
	  struct tm localtime_copy = localtime;
	  localtime_copy.tm_isdst = 0; // Disable DST
	  timeZone = (mktime(&localtime_copy) - mktime(utctime)) / 3600;
	
   
    // Redraw the clock
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_BLACK, TFT_WHITE);
    tft.setCursor (0,0);
    
    // draw Date      
    tft.setTextColor(TFT_WHITE, createColor(0, 0, 90));
    tft.drawString(dateString, 38, 0, 4);
    renderActiveScheduleEntries(localtime);
    
  }

  // EVENT every min
  if (localtime.tm_min != event_tm_min)
  {
    event_tm_min = localtime.tm_min;
    Serial.println("event_tm_min");
    // UTC Time HH:MM
    sprintf(utctimeString, "%02d:%02d", utctime->tm_hour, utctime->tm_min);
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
    //if (SoftTimer(HAS_TIMER_ELAPSED)) {
      sprite.drawString(localtimeString, sprite.width() / 2, sprite.height() / 2);
    } else {
      sprite.drawString(locaxtimeString, sprite.width() / 2, sprite.height() / 2);
    }

    sprite.pushSprite(1, 68);
    sprite.deleteSprite();
    //SoftTimer(500);
      
  }

  // EVENT Pen touch
  #if ENABLE_TOUCH
  if (touch_ready && ts.tirqTouched() && ts.touched())  {
    TS_Point p = ts.getPoint();
    printTouchToSerial(p);
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
      analogWrite(backlightPin, brightness);
      Serial.print("Brightness=");
      Serial.println(brightness);
    }
    delay(300);
  }
  #endif
}


//end
