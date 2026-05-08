#include "network_manager.h"

#include "app_state.h"
#include "schedule_display.h"
#include "storage_manager.h"

#include <HTTPClient.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <esp_system.h>
#include <time.h>

void apply_config_from_string(String content);
void apply_current_config_with_runtime_state();
void load_cached_config_for_index_from_storage(int index, bool allowLegacyFallback);
bool configContentEqualsNormalized(const String &left, const String &right);
String get_config_cache_path_for_id(const String &id);
bool sync_config_to_sd_and_memory(String newContent, bool &changed);

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

bool timesync(bool drawStatus)
{
  bool exit_status = 1;
  Serial.println("Get NTP Time");
  if (drawStatus)
  {
    tft.println("Get NTP Time");
  }
  if (WiFi.status() == WL_CONNECTED)
  {
    struct tm local;
    configTzTime(tzinfo.c_str(), ntpserver.c_str()); // Synchronize ESP32 system time with NTP
    if (!getLocalTime(&local, 10000)) // Try to synchronize for 10 seconds
    {
      Serial.println("Timeserver cannot be reached !!!");
      if (drawStatus)
      {
        tft.println("Timeserver cannot be reached !!!");
      }
      exit_status = 0;
    }
    else
    {
      Serial.print("Timeserver: ");
      Serial.println(&local, "Datum: %d.%m.%y  Zeit: %H:%M:%S Test: %a,%B");
      if (drawStatus)
      {
        tft.print("Timeserver: ");
        tft.println(&local, "Datum: %d.%m.%y  Zeit: %H:%M:%S Test: %a,%B");
      }
      Serial.flush();
    }
  }
  else
  {
    Serial.println("WiFi not connected !!!");
    if (drawStatus)
    {
      tft.println("WiFi not connected !!!");
    }
    exit_status = 0;
  }
  return exit_status;
}

unsigned long computeNtpDelayMs(int baseMinutes, int randomDelaySeconds)
{
  int sanitizedBaseMinutes = int(max(1L, long(baseMinutes)));
  int sanitizedRandomSeconds = int(min(long(MAX_NTP_RANDOM_DELAY_SECONDS), max(0L, long(randomDelaySeconds))));
  unsigned long delayMs = (unsigned long)sanitizedBaseMinutes * MINUTE_MS;

  if (sanitizedRandomSeconds > 0)
  {
    delayMs += (unsigned long)random(sanitizedRandomSeconds + 1) * 1000UL;
  }

  return delayMs;
}

void scheduleNextNtpSync(bool lastSyncSucceeded)
{
  unsigned long delayMs = lastSyncSucceeded
                            ? computeNtpDelayMs(ntp_sync_frequency_minutes, ntp_sync_random_delay_seconds)
                            : computeNtpDelayMs(ntp_retry_frequency_minutes, ntp_retry_random_delay_seconds);

  next_ntp_sync_ms = millis() + delayMs;
  ntp_sync_scheduled = true;

  Serial.print("Next NTP sync in ");
  Serial.print(delayMs / 60000UL);
  Serial.print("m ");
  Serial.print((delayMs % 60000UL) / 1000UL);
  Serial.println("s");
}

void processScheduledNtpSync()
{
  if (!ntp_sync_scheduled)
  {
    scheduleNextNtpSync(true);
    return;
  }

  if (long(millis() - next_ntp_sync_ms) < 0)
  {
    return;
  }

  bool syncSucceeded = timesync(false);
  scheduleNextNtpSync(syncSucceeded);
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
        if (index == active_system_id_index)
        {
          if (!ram_only_mode && !write_config_to_sd(payload))
          {
            Serial.println("Prefetch legacy SD /config.txt write failed");
          }
          if (!writeLittleFsTextMounted("/config.txt", payload))
          {
            Serial.println("Prefetch LittleFS /config.txt write failed");
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
