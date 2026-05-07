#include "storage_manager.h"

#include "app_state.h"

#include <LittleFS.h>
#include <SD.h>

constexpr int SD_CS = 5;
constexpr int XPT2046_CS = 33;

bool sd_ready = false;
bool littlefs_ready = false;

void suspend_touch_for_sd();
void resume_touch_after_sd();

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

bool ensureLittleFsMounted()
{
  if (littlefs_ready)
  {
    return true;
  }

  if (!LittleFS.begin(false))
  {
    littlefs_ready = false;
    return false;
  }

  littlefs_ready = true;
  return true;
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

bool read_file_text_from_littlefs(const char *path, String &content)
{
  File file;
  bool success = false;

  content = "";
  if (!ensureLittleFsMounted())
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
  return success;
}

bool readLittleFsTextMounted(const char *path, String &content)
{
  return read_file_text_from_littlefs(path, content);
}

bool writeLittleFsTextMounted(const char *path, const String &content)
{
  if (!ensureLittleFsMounted())
  {
    return false;
  }

  File file = LittleFS.open(path, FILE_WRITE);
  if (!file)
  {
    return false;
  }

  size_t bytesWritten = file.print(content);
  file.close();
  return bytesWritten == content.length();
}

bool read_config_text_from_littlefs(String &content)
{
  return read_file_text_from_littlefs("/config.txt", content);
}

void list_sd_files_to_serial()
{
  File root;
  File entry;

  Serial.println("SD files:");

  if (!begin_sd_session())
  {
    Serial.println("  <sd unavailable>");
    return;
  }

  root = SD.open("/");
  if (!root)
  {
    Serial.println("  <cannot open root>");
    end_sd_session();
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
  end_sd_session();
}

void list_littlefs_files_to_serial()
{
  File root;
  File entry;

  Serial.println("LittleFS files:");

  if (!ensureLittleFsMounted())
  {
    Serial.println("  <littlefs unavailable>");
    return;
  }

  root = LittleFS.open("/");
  if (!root)
  {
    Serial.println("  <cannot open root>");
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
}
