#include "Arduino.h"
#include <TFT_eSPI.h>
#include <time.h>
//#include <sunset.h>
#include "seven_regular11pt7b.h"
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
