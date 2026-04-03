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

// Used to delay the timer when poking the updateurl
unsigned long next_update_check = 0;

SPIClass mySpi = SPIClass(VSPI);
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
String Dummy;

int yy_mem = 0;
int mm_mem = 0;

int event_tm_hour = -1;
int event_tm_min = -1;
int event_tm_sec = -1;

int next_update_modular = 15;

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
  value.replace("\r", "");
  value.replace("\n", "");
  // Serial.print(key + F("="));
  // Serial.println(value);
  if (configKeyEquals(key, "ssid")) {
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

  if (!SD.begin(SD_CS))
  {
    Serial.println("SD-Card: Failure during write");
    return false;
  }

  if (SD.exists("/config.txt"))
  {
    if (!SD.remove("/config.txt"))
    {
      Serial.println("Cannot remove existing /config.txt");
      SD.end();
      return false;
    }
  }

  configFile = SD.open("/config.txt", FILE_WRITE);
  if (!configFile)
  {
    Serial.println("Cannot open /config.txt for write");
    SD.end();
    return false;
  }

  configFile.print(content);
  configFile.close();
  SD.end();

  Serial.println("Configuration File overwritten");
  return true;
}

// read config.txt from SD Card
void read_sd()
{
  if (!SD.begin(SD_CS)) 
  {
    Serial.println("SD-Card: Failure");
    return;
  }
  Serial.println("SD-Card: Initialization");

  File configFile = SD.open("/config.txt");
  if (configFile) 
  {
    while (configFile.available()) 
    {
      String line = configFile.readStringUntil('\n');
      parseConfigLine(line);
    }
    configFile.close();
  } 
  else 
  {
    Serial.println("Configuration File not Found -- Using Defaults.");
  }
  SD.end();
}

bool bootstrap_config_from_server()
{
  HTTPClient http;
  String payload;
  int httpCode;

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

  http.begin(updateurl);
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

  if (!write_config_to_sd(payload))
  {
    Serial.println("Failed to write downloaded config");
    tft.println("Write config failed");
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
  mySpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(mySpi);
  ts.setRotation(1);

  delay(100);
}

void apply_config_from_string(String content)
{
  int start = 0;
  int end = 0;
  String line;

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

  String old_ssid = ssid;
  String old_password = password;
  String old_tzinfo = tzinfo;
  String old_ntpserver = ntpserver;
  String old_updateurl = updateurl;

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
  Serial.println(updateurl);

  http.begin(updateurl);
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

  apply_config_from_string(payload);

  if ((tzinfo != old_tzinfo) || (ntpserver != old_ntpserver))
  {
    apply_runtime_NTP_config();
    // Serial.println("Timezone/NTP settings updated");
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
    
  }

  // EVENT every min
  if (localtime.tm_min != event_tm_min)
  {
    event_tm_min = localtime.tm_min;
    Serial.println("event_tm_min");
    // UTC Time HH:MM
    sprintf(utctimeString, "%02d:%02d", utctime->tm_hour, utctime->tm_min);
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

    sprite.pushSprite(1, 78);
    sprite.deleteSprite();
    //SoftTimer(500);
      
  }

  // EVENT Pen touch
  if (ts.tirqTouched() && ts.touched())  {
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
}


//end
