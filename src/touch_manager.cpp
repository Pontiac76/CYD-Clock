#include "touch_manager.h"

#include "app_state.h"
#include "brightness_manager.h"
#include "config_manager.h"
#include "display_manager.h"
#include "network_manager.h"
#include "schedule_display.h"

#include <SPI.h>
#include <WiFi.h>
#include <time.h>

SPIClass mySpi = SPIClass(HSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
bool touch_ready = false;
bool touch_initialized = false;

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

void processTouchInput()
{
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
      if (p.x < 800) {
        setBrightnessFromController(brightness - brightness_step, "Brightness", true);
      }
      if (p.x > 3200) {
        setBrightnessFromController(brightness + brightness_step, "Brightness", true);
      }
    }
    delay(300);
  }
  #endif
}
