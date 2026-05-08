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
#include "config_manager.h"
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
