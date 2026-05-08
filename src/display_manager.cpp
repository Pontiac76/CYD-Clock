#include "display_manager.h"

#include "app_state.h"

#include <TFT_eSPI.h>
#include <esp_system.h>
#include <qrcode.h>

#define RGB565(r, g, b) (((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F))

TFT_eSPI tft = TFT_eSPI();

uint16_t clockTextColor = TFT_RED;
uint16_t dateTextColor = TFT_WHITE;
uint16_t dateBgColor = RGB565(0, 0, 90 >> 3);
uint16_t statusTextColor = TFT_WHITE;
uint16_t statusBgColor = RGB565(0, 0, 90 >> 3);
uint16_t scheduleTextColor = RGB565(255 >> 3, 220 >> 2, 160 >> 3);
uint16_t bootTextColor = RGB565(128 >> 3, 255 >> 2, 128 >> 3);
uint16_t errorTextColor = RGB565(255 >> 3, 128 >> 2, 128 >> 3);
String build_version_code;

int event_tm_hour = -1;
int event_tm_min = -1;
int event_tm_sec = -1;

constexpr const char *SETUP_WIFI_QR_PAYLOAD = "WIFI:T:WPA;S:CYD-Clock-Setup;P:cydclocksetup;;";
constexpr const char *SETUP_PORTAL_URL = "http://192.168.4.1/";

uint16_t createColor(uint8_t r, uint8_t g, uint8_t b)
{
  return RGB565(r >> 3, g >> 2, b >> 3);
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

void drawQrCode(const char *payload, const char *caption)
{
  constexpr uint8_t qrVersion = 5;
  constexpr uint8_t quietZoneModules = 4;
  QRCode qrcode;
  uint8_t qrcodeBytes[qrcode_getBufferSize(qrVersion)];

  qrcode_initText(&qrcode, qrcodeBytes, qrVersion, ECC_LOW, payload);

  int totalModules = qrcode.size + (quietZoneModules * 2);
  int availableWidth = static_cast<int>(tft.width());
  int availableHeight = static_cast<int>(tft.height()) - 32;
  int moduleSize = min(availableWidth, availableHeight) / totalModules;
  moduleSize = max(1, moduleSize);

  int qrPixelSize = totalModules * moduleSize;
  int qrX = (tft.width() - qrPixelSize) / 2;
  int qrY = 8;
  int codeX = qrX + (quietZoneModules * moduleSize);
  int codeY = qrY + (quietZoneModules * moduleSize);

  tft.fillScreen(TFT_WHITE);
  tft.fillRect(qrX, qrY, qrPixelSize, qrPixelSize, TFT_WHITE);

  for (uint8_t y = 0; y < qrcode.size; ++y)
  {
    for (uint8_t x = 0; x < qrcode.size; ++x)
    {
      if (qrcode_getModule(&qrcode, x, y))
      {
        tft.fillRect(codeX + (x * moduleSize), codeY + (y * moduleSize), moduleSize, moduleSize, TFT_BLACK);
      }
    }
  }

  if (caption != nullptr)
  {
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(TFT_BLACK, TFT_WHITE);
    tft.drawString(caption, tft.width() / 2, min(tft.height() - 12, qrY + qrPixelSize + 14), 2);
    tft.setTextDatum(TL_DATUM);
  }
}

void drawSetupJoinQrCode()
{
  drawQrCode(SETUP_WIFI_QR_PAYLOAD, "Join CYD setup AP");
}

void drawSetupPortalQrCode()
{
  drawQrCode(SETUP_PORTAL_URL, "Open CYD setup page");

  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(1);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.drawString(SETUP_PORTAL_URL, tft.width() / 2, tft.height() - 2, 1);
  tft.setTextDatum(TL_DATUM);
}

void drawSetupStatus(const char *line1, const String &line2)
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(bootTextColor, TFT_BLACK);
  tft.drawString(line1, tft.width() / 2, 90, 2);
  if (line2 != "")
  {
    tft.drawString(line2, tft.width() / 2, 116, 2);
  }
  tft.setTextDatum(TL_DATUM);
}

void rebootAfterSetupStatus(const char *line1, const String &line2)
{
  drawSetupStatus(line1, line2);
  delay(3000);
  ESP.restart();
}
