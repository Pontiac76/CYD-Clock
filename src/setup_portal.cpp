#include "setup_portal.h"

#include "app_state.h"
#include "display_manager.h"
#include "network_manager.h"
#include "storage_manager.h"

#include <DNSServer.h>
#include <FS.h>
#include <HTTPClient.h>
#include <LittleFS.h>
using namespace fs;
#include <WebServer.h>
#include <WiFi.h>

constexpr const char *SETUP_AP_SSID = "CYD-Clock-Setup";
constexpr const char *SETUP_AP_PASSWORD = "cydclocksetup";
constexpr const char *SETUP_INDEX_PATH = "/html/index.html";
constexpr const char *SETUP_CSS_PATH = "/html/index.css";
constexpr const char *SETUP_AUTOUPDATE_PATH = "/autoupdate.txt";
constexpr const char *SETUP_WIFI_PATH = "/wifi.txt";
constexpr byte DNS_PORT = 53;

DNSServer setupDnsServer;
WebServer setupWebServer(80);
bool setup_portal_running = false;
int setup_portal_station_count = -1;
bool setup_submission_pending = false;
unsigned long setup_submission_process_ms = 0;
String setup_submitted_ssid;
String setup_submitted_password;
String setup_submitted_title;
String setup_submitted_autoupdate_url;
String setup_submitted_system_ids;

String sanitizeSystemId(String value);
String sanitizeConfigKey(String key);
String buildSystemIdFileText(String rawValue);

bool isSetupPortalRunning()
{
  return setup_portal_running;
}

void processSetupPortal()
{
  setupDnsServer.processNextRequest();
  setupWebServer.handleClient();
  refreshSetupPortalDisplay();
  processSetupSubmission();
}

void refreshSetupPortalDisplay()
{
  int currentStationCount = WiFi.softAPgetStationNum();
  if (currentStationCount == setup_portal_station_count)
  {
    return;
  }

  setup_portal_station_count = currentStationCount;
  if (currentStationCount > 0)
  {
    Serial.print("Setup portal client connected count=");
    Serial.println(currentStationCount);
    drawSetupPortalQrCode();
  }
  else
  {
    Serial.println("Setup portal waiting for client");
    drawSetupJoinQrCode();
  }
}

bool sendLittleFsFile(const char *path, const char *contentType)
{
  if (!ensureLittleFsMounted())
  {
    return false;
  }

  File file = LittleFS.open(path, FILE_READ);
  if (!file)
  {
    return false;
  }

  if (file.size() == 0)
  {
    file.close();
    return false;
  }

  setupWebServer.streamFile(file, contentType);
  file.close();
  return true;
}

String escapeHtmlAttribute(String value)
{
  value.replace("&", "&amp;");
  value.replace("\"", "&quot;");
  value.replace("<", "&lt;");
  value.replace(">", "&gt;");
  return value;
}

String escapeHtmlText(String value)
{
  value.replace("&", "&amp;");
  value.replace("<", "&lt;");
  value.replace(">", "&gt;");
  return value;
}

void sortStrings(String values[], int count)
{
  for (int outer = 0; outer < count - 1; ++outer)
  {
    for (int inner = outer + 1; inner < count; ++inner)
    {
      if (values[inner].compareTo(values[outer]) < 0)
      {
        String swapValue = values[outer];
        values[outer] = values[inner];
        values[inner] = swapValue;
      }
    }
  }
}

String buildApListHtml()
{
  constexpr int maxRenderedNetworks = 32;
  String networks[maxRenderedNetworks];
  int networkCount = WiFi.scanNetworks();
  int renderedCount = 0;

  for (int index = 0; (index < networkCount) && (renderedCount < maxRenderedNetworks); ++index)
  {
    String networkSsid = WiFi.SSID(index);
    networkSsid.trim();
    if (networkSsid == "")
    {
      continue;
    }

    bool duplicate = false;
    for (int existing = 0; existing < renderedCount; ++existing)
    {
      if (networks[existing] == networkSsid)
      {
        duplicate = true;
        break;
      }
    }
    if (duplicate)
    {
      continue;
    }

    networks[renderedCount] = networkSsid;
    ++renderedCount;
  }

  WiFi.scanDelete();

  if (renderedCount <= 0)
  {
    return "<p class=\"empty\">No APs found. Refresh the page to scan again.</p>";
  }

  sortStrings(networks, renderedCount);

  String html = "<div class=\"ap-links\">";
  for (int index = 0; index < renderedCount; ++index)
  {
    String escapedAttribute = escapeHtmlAttribute(networks[index]);
    String escapedText = escapeHtmlText(networks[index]);
    html += "<a href=\"#\" data-ssid=\"";
    html += escapedAttribute;
    html += "\">";
    html += escapedText;
    html += "</a>";
  }
  html += "</div>";
  return html;
}

String sanitizeSetupField(String value)
{
  value.replace("\r", "");
  value.replace("\n", "");
  value.trim();
  return value;
}

String sanitizeWifiSectionTitle(String value)
{
  value = sanitizeSetupField(value);
  value.replace("[", "");
  value.replace("]", "");
  if (value == "")
  {
    value = "Default";
  }
  return value;
}

String readDefaultAutoUpdateUrl()
{
  String defaultUrl;
  if (!readLittleFsTextMounted(SETUP_AUTOUPDATE_PATH, defaultUrl))
  {
    defaultUrl = DEFAULT_UPDATE_URL;
  }

  defaultUrl = sanitizeSetupField(defaultUrl);
  if (defaultUrl == "")
  {
    defaultUrl = DEFAULT_UPDATE_URL;
  }
  return defaultUrl;
}

bool loadFirstWifiProfileFromLittleFs()
{
  String wifiText;
  bool inFirstSection = false;
  bool sawSection = false;
  int start = 0;

  if (!readLittleFsTextMounted(SETUP_WIFI_PATH, wifiText))
  {
    Serial.println("WiFi profile: LittleFS /wifi.txt missing");
    return false;
  }

  ssid = "";
  password = "";

  while (start < wifiText.length())
  {
    int end = wifiText.indexOf('\n', start);
    String line;
    if (end == -1)
    {
      line = wifiText.substring(start);
      start = wifiText.length();
    }
    else
    {
      line = wifiText.substring(start, end);
      start = end + 1;
    }

    line.replace("\r", "");
    line.trim();
    if ((line == "") || line.startsWith("#"))
    {
      continue;
    }

    if (line.startsWith("[") && line.endsWith("]"))
    {
      if (sawSection)
      {
        break;
      }
      sawSection = true;
      inFirstSection = true;
      continue;
    }

    if (!inFirstSection)
    {
      continue;
    }

    int separator = line.indexOf('=');
    if (separator == -1)
    {
      continue;
    }

    String key = line.substring(0, separator);
    String value = line.substring(separator + 1);
    key = sanitizeConfigKey(key);
    value.replace("\r", "");
    value.replace("\n", "");
    value.trim();

    if (key == "ssid")
    {
      ssid = value;
    }
    else if (key == "password")
    {
      password = value;
    }
    else if ((key == "autoupdate") || (key == "updateurl"))
    {
      updateurl = value;
    }
  }

  if (updateurl == "")
  {
    updateurl = readDefaultAutoUpdateUrl();
  }

  if (ssid == "")
  {
    Serial.println("WiFi profile: no SSID in first /wifi.txt section");
    return false;
  }

  Serial.print("WiFi profile loaded: ");
  Serial.println(ssid);
  return true;
}

String buildSystemIdConfigValue(String rawValue)
{
  String content = buildSystemIdFileText(rawValue);
  content.replace("\n", ";");
  while (content.endsWith(";"))
  {
    content = content.substring(0, content.length() - 1);
  }
  return content;
}

String buildWifiConfigText(const String &title, const String &networkSsid, const String &networkPassword, const String &autoUpdateUrl)
{
  String content;
  String systemIdValue = buildSystemIdConfigValue(setup_submitted_system_ids);
  content.reserve(title.length() + networkSsid.length() + networkPassword.length() + autoUpdateUrl.length() + systemIdValue.length() + 64);
  content += "[";
  content += title;
  content += "]\n";
  content += "ssid=";
  content += networkSsid;
  content += "\n";
  content += "password=";
  content += networkPassword;
  content += "\n";
  content += "autoupdate=";
  content += autoUpdateUrl;
  content += "\n";
  if (systemIdValue != "")
  {
    content += "systemid=";
    content += systemIdValue;
    content += "\n";
  }
  return content;
}

bool writeSubmittedWifiConfig()
{
  String wifiConfig = buildWifiConfigText(
    setup_submitted_title,
    setup_submitted_ssid,
    setup_submitted_password,
    setup_submitted_autoupdate_url);

  return writeLittleFsTextMounted(SETUP_WIFI_PATH, wifiConfig);
}

String buildSetupConfigText()
{
  String content;
  content.reserve(setup_submitted_autoupdate_url.length() + 16);
  content += "updateurl=";
  content += setup_submitted_autoupdate_url;
  content += "\n";
  return content;
}

bool writeSubmittedConfigFiles()
{
  String configText = buildSetupConfigText();
  bool littleFsSuccess = writeLittleFsTextMounted("/config.txt", configText);
  bool sdSuccess = true;
  if (!ram_only_mode)
  {
    sdSuccess = write_config_to_sd(configText);
  }

  if (!littleFsSuccess)
  {
    Serial.println("Setup: failed to write LittleFS /config.txt");
  }
  if (ram_only_mode)
  {
    Serial.println("Setup: SD unavailable; /config.txt mirror skipped");
  }
  if (!sdSuccess)
  {
    Serial.println("Setup: failed to mirror SD /config.txt");
  }

  return littleFsSuccess;
}

String buildSystemIdFieldValue()
{
  String value;
  for (int index = 0; index < system_id_count; ++index)
  {
    if (value != "")
    {
      value += ";";
    }
    value += system_id_list[index];
  }

  String wifiText;
  if (readLittleFsTextMounted(SETUP_WIFI_PATH, wifiText))
  {
    int start = 0;
    while (start < wifiText.length())
    {
      int end = wifiText.indexOf('\n', start);
      String line;
      if (end == -1)
      {
        line = wifiText.substring(start);
        start = wifiText.length();
      }
      else
      {
        line = wifiText.substring(start, end);
        start = end + 1;
      }

      line.replace("\r", "");
      line.trim();
      int separator = line.indexOf('=');
      if (separator == -1)
      {
        continue;
      }

      String key = line.substring(0, separator);
      key.trim();
      key.toLowerCase();
      if (key != "systemid")
      {
        continue;
      }

      String systemIdText = buildSystemIdConfigValue(line.substring(separator + 1));
      if (systemIdText == "")
      {
        continue;
      }

      if (value != "")
      {
        value += ";";
      }
      value += systemIdText;
    }
  }
  return value;
}

String buildSystemIdFileText(String rawValue)
{
  String content;
  int start = 0;

  rawValue.replace("\r", "");
  rawValue.replace("\n", ";");

  while (start <= rawValue.length())
  {
    int end = rawValue.indexOf(';', start);
    String token;
    if (end == -1)
    {
      token = rawValue.substring(start);
      start = rawValue.length() + 1;
    }
    else
    {
      token = rawValue.substring(start, end);
      start = end + 1;
    }

    token = sanitizeSystemId(token);
    if (token == "")
    {
      continue;
    }

    if (content != "")
    {
      content += "\n";
    }
    content += token;
  }

  if (content != "")
  {
    content += "\n";
  }
  return content;
}

bool writeSubmittedSystemIds()
{
  String content = buildSystemIdFileText(setup_submitted_system_ids);
  if (content == "")
  {
    return true;
  }

  bool littleFsSuccess = writeLittleFsTextMounted("/systemid.txt", content);
  bool sdSuccess = true;
  if (!ram_only_mode)
  {
    sdSuccess = write_text_file_to_sd("/systemid.txt", content);
  }

  if (!littleFsSuccess)
  {
    Serial.println("Setup: failed to write LittleFS /systemid.txt");
  }
  if (!sdSuccess)
  {
    Serial.println("Setup: failed to mirror SD /systemid.txt");
  }

  return littleFsSuccess;
}

void sendSetupFallbackPage()
{
  setupWebServer.send(
    200,
    "text/html",
    "<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>CYD setup</title></head><body><h1>CYD setup</h1>"
    "<p>LittleFS /html/index.html is missing or empty.</p></body></html>");
}

void handleAutoUpdateText()
{
  setupWebServer.send(200, "text/plain", readDefaultAutoUpdateUrl());
}

void handleSetupSubmit()
{
  setup_submitted_ssid = sanitizeSetupField(setupWebServer.arg("ssid"));
  setup_submitted_password = sanitizeSetupField(setupWebServer.arg("password"));
  setup_submitted_title = sanitizeWifiSectionTitle(setupWebServer.arg("title"));
  setup_submitted_autoupdate_url = sanitizeSetupField(setupWebServer.arg("autoupdate"));
  setup_submitted_system_ids = sanitizeSetupField(setupWebServer.arg("systemid"));

  if (setup_submitted_autoupdate_url == "")
  {
    setup_submitted_autoupdate_url = readDefaultAutoUpdateUrl();
  }

  if (setup_submitted_ssid == "")
  {
    setupWebServer.send(400, "text/plain", "AP name is required.");
    return;
  }

  setup_submission_pending = true;
  setup_submission_process_ms = millis() + 1000;
  setupWebServer.send(
    200,
    "text/html",
    "<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>CYD setup</title></head><body><h1>Setup submitted</h1>"
    "<p>The CYD is validating Wi-Fi and will reboot after setup completes.</p></body></html>");
}

void sendCaptivePortalRedirect()
{
  setupWebServer.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  setupWebServer.send(302, "text/plain", "");
}

void handleSetupIndex()
{
  String html;
  if (!readLittleFsTextMounted(SETUP_INDEX_PATH, html) || (html == ""))
  {
    sendSetupFallbackPage();
    return;
  }

  html.replace("{{AUTOUPDATE_URL}}", escapeHtmlAttribute(readDefaultAutoUpdateUrl()));
  html.replace("{{SYSTEM_ID_LIST}}", escapeHtmlAttribute(buildSystemIdFieldValue()));
  html.replace("{{AP_LIST}}", buildApListHtml());
  setupWebServer.send(200, "text/html", html);
}

void handleSetupCss()
{
  if (!sendLittleFsFile(SETUP_CSS_PATH, "text/css"))
  {
    setupWebServer.send(404, "text/plain", "Not found");
  }
}

void fetchSubmittedAutoUpdateUrl()
{
  HTTPClient http;
  Serial.print("Setup autoupdate GET: ");
  Serial.println(setup_submitted_autoupdate_url);

  http.begin(setup_submitted_autoupdate_url);
  http.setTimeout(5000);
  int httpCode = http.GET();
  if (httpCode <= 0)
  {
    Serial.print("Setup autoupdate failed: ");
    Serial.println(http.errorToString(httpCode));
    http.end();
    return;
  }

  Serial.print("Setup autoupdate HTTP code: ");
  Serial.println(httpCode);
  http.end();
}

void processSetupSubmission()
{
  if (!setup_submission_pending)
  {
    return;
  }
  if (millis() < setup_submission_process_ms)
  {
    return;
  }

  setup_submission_pending = false;
  drawSetupStatus("Connecting WiFi", setup_submitted_ssid);

  setupWebServer.stop();
  setupDnsServer.stop();
  setup_portal_running = false;
  delay(250);

  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(setup_submitted_ssid.c_str(), setup_submitted_password.c_str());

  int connectAttempts = 0;
  while ((WiFi.status() != WL_CONNECTED) && (connectAttempts < 150))
  {
    delay(100);
    ++connectAttempts;
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("Setup WiFi connect failed");
    rebootAfterSetupStatus("WiFi failed", setup_submitted_ssid);
    return;
  }

  Serial.print("Setup WiFi connected IP: ");
  Serial.println(WiFi.localIP());

  if (WiFi.localIP() == IPAddress(0, 0, 0, 0))
  {
    rebootAfterSetupStatus("WiFi failed", "No IP address");
    return;
  }

  if (!writeSubmittedWifiConfig())
  {
    rebootAfterSetupStatus("Write wifi.txt failed");
    return;
  }

  if (!writeSubmittedSystemIds())
  {
    rebootAfterSetupStatus("Write systemid failed");
    return;
  }

  if (!writeSubmittedConfigFiles())
  {
    rebootAfterSetupStatus("Write config failed");
    return;
  }

  fetchSubmittedAutoUpdateUrl();
  rebootAfterSetupStatus("Setup complete", "Rebooting...");
}

void startSetupPortal()
{
  if (setup_portal_running)
  {
    return;
  }

  if (!ensureLittleFsMounted())
  {
    Serial.println("Setup portal: LittleFS mount failed");
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAP(SETUP_AP_SSID, SETUP_AP_PASSWORD);
  IPAddress apIp = WiFi.softAPIP();

  setupDnsServer.start(DNS_PORT, "*", apIp);

  setupWebServer.on("/", HTTP_GET, handleSetupIndex);
  setupWebServer.on("/html/index.html", HTTP_GET, handleSetupIndex);
  setupWebServer.on("/html/index.css", HTTP_GET, handleSetupCss);
  setupWebServer.on("/autoupdate.txt", HTTP_GET, handleAutoUpdateText);
  setupWebServer.on("/setup", HTTP_POST, handleSetupSubmit);

  setupWebServer.on("/generate_204", HTTP_GET, sendCaptivePortalRedirect);
  setupWebServer.on("/gen_204", HTTP_GET, sendCaptivePortalRedirect);
  setupWebServer.on("/hotspot-detect.html", HTTP_GET, handleSetupIndex);
  setupWebServer.on("/library/test/success.html", HTTP_GET, handleSetupIndex);
  setupWebServer.on("/ncsi.txt", HTTP_GET, handleSetupIndex);
  setupWebServer.on("/connecttest.txt", HTTP_GET, handleSetupIndex);
  setupWebServer.onNotFound(sendCaptivePortalRedirect);

  setupWebServer.begin();
  setup_portal_running = true;

  Serial.print("Setup portal AP: ");
  Serial.println(SETUP_AP_SSID);
  Serial.print("Setup portal URL: http://");
  Serial.println(apIp);
}

