# Refactor Status and Next Steps

This file reflects the current code layout after the module split work.

## Current File Layout

```text
include/app_state.h
include/brightness_manager.h
include/config_manager.h
include/display_manager.h
include/network_manager.h
include/schedule_display.h
include/setup_portal.h
include/storage_manager.h
include/touch_manager.h

src/brightness_manager.cpp
src/config_manager.cpp
src/display_manager.cpp
src/main.cpp
src/network_manager.cpp
src/schedule_display.cpp
src/setup_portal.cpp
src/storage_manager.cpp
src/touch_manager.cpp
```

## Completed Refactors

The original planned module refactors have been completed and build successfully.

### 1. Storage Manager — Done

```text
include/storage_manager.h
src/storage_manager.cpp
```

Owns:

- SD session begin/end
- LittleFS mount helpers
- SD/LittleFS text read/write helpers
- config file read/write primitives
- SD/LittleFS file listing
- `ram_only_mode`

### 2. Display Manager — Done

```text
include/display_manager.h
src/display_manager.cpp
```

Owns:

- `tft`
- display colors
- build/version display helpers
- QR drawing
- setup status drawing
- build/system info lane
- clock redraw event trackers:
  - `event_tm_hour`
  - `event_tm_min`
  - `event_tm_sec`

### 3. Brightness Manager — Done

```text
include/brightness_manager.h
src/brightness_manager.cpp
```

Owns:

- backlight/photoresistor pins
- brightness state
- photoresistor calibration values
- autodim/photo-dim state
- brightness control helpers
- photoresistor processing
- auto brightness helpers

### 4. Touch Manager — Done

```text
include/touch_manager.h
src/touch_manager.cpp
```

Owns:

- touch pin constants
- touch SPI object
- touchscreen object
- touch init/suspend/resume helpers
- touch debug logging
- touch handling from `loop()` via `processTouchInput()`
- system ID switch touch handling

### 5. Network Manager — Done

```text
include/network_manager.h
src/network_manager.cpp
```

Owns:

- Wi-Fi station connect
- NTP sync and resync scheduling
- update URL building
- update server polling
- startup config bootstrap from server
- cached config prefetch from server
- update/NTP timing globals
- `DEFAULT_UPDATE_URL`

### 6. Setup Portal — Done

```text
include/setup_portal.h
src/setup_portal.cpp
```

Owns:

- setup AP/captive portal
- DNS server and setup web server
- setup web routes
- setup form handling
- AP scan HTML rendering
- submitted Wi-Fi/config/system ID writes
- setup QR/status display coordination
- first Wi-Fi profile load from LittleFS

### 7. Config Manager — Done

```text
include/config_manager.h
src/config_manager.cpp
```

Owns:

- config parsing and application
- config key normalization/comparison
- color parsing
- translation parsing
- system ID parsing/list/switch/cache state
- config cache load/fallback logic
- config sync-to-SD logic
- config content normalized compare
- Wi-Fi/config strings:
  - `ssid`
  - `password`
  - `tzinfo`
  - `tformat`
  - `ntpserver`
  - `updateurl`
- `WeekDays`, `MonthName`, and `ScheduleEntries`

## App State Policy

`include/app_state.h` is now a shared declaration file, not the owner of state.

Current rules:

- Prefer module-private state plus getter/setter functions first.
- Add an `extern` only when state must be shared across modules.
- Every `extern` must be grouped under the `.cpp` file where it is defined.
- Do not define storage/defaults in `app_state.h`.
- Definitions/defaults belong in the owning module `.cpp` file.

Example:

```cpp
// app_state.h
// Defined in src/brightness_manager.cpp
extern int brightness;
```

```cpp
// brightness_manager.cpp
int brightness = 128;
```

## Current `main.cpp` Responsibilities

`main.cpp` is much smaller now, but still owns orchestration.

It currently contains:

- early serial/random/touch-CS setup
- early TFT boot screen setup
- boot/config/network/setup-portal orchestration
- backlight/photoresistor hardware setup
- main clock render loop
- update polling timer call site
- touch processing call site

Current shape is approximately:

```cpp
void setup()
{
  // serial/random/basic pin init
  // TFT boot screen
  // detect SD/RAM-only mode
  // list storage files
  // read system IDs and config
  // initialize backlight/photoresistor
  // load Wi-Fi profile if needed
  // connect Wi-Fi or start setup portal
  // preload/bootstrap config
  // NTP sync
  // initialize touch
}

void loop()
{
  if (isSetupPortalRunning())
  {
    processSetupPortal();
    return;
  }

  getLocalTime(&localtime);
  processPhotoBrightness();
  processScheduledNtpSync();
  // hourly date/build/schedule redraw
  // minute schedule redraw
  // second update polling and clock sprite redraw
  processTouchInput();
}
```

## Remaining Cleanup Suggestions

These are not required for correctness, but would make `main.cpp` closer to minimum.

### A. Move Hardware Initialization

Create:

```text
include/hardware_manager.h
src/hardware_manager.cpp
```

Candidate functions:

```cpp
void initializeEarlyHardware();
void initializeBootDisplay();
void initializeConfiguredHardware();
```

Move setup bits such as:

```cpp
Serial.begin(115200);
randomSeed((uint32_t)esp_random());
pinMode(XPT2046_CS, OUTPUT);
digitalWrite(XPT2046_CS, HIGH);

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

pinMode(backlightPin, OUTPUT);
analogWrite(backlightPin, brightness);
pinMode(photoResistorPin, INPUT);
analogSetPinAttenuation(photoResistorPin, ADC_11db);
```

### B. Move Boot/Application Orchestration

Create:

```text
include/app_controller.h
src/app_controller.cpp
```

Candidate functions:

```cpp
void setupApplication();
void processApplicationLoop();
```

Or split setup into:

```cpp
void loadBootConfiguration();
bool initializeClockModeOrSetupPortal();
```

This would absorb the current middle of `setup()`:

- detect SD/RAM-only mode
- list files
- read system IDs
- load cached config
- read fallback config
- load Wi-Fi profile
- connect Wi-Fi
- prefetch/bootstrap config
- initial NTP sync
- setup portal fallback

### C. Move Clock Rendering Loop Into Display Manager

Create:

```cpp
void renderClockIfNeeded(const struct tm &localtime);
```

Move from `loop()`:

- hourly date redraw
- build/system info redraw
- schedule redraw triggers
- clock sprite creation/drawing/push

Then the main loop becomes simpler:

```cpp
void loop()
{
  if (isSetupPortalRunning())
  {
    processSetupPortal();
    return;
  }

  struct tm localtime;
  getLocalTime(&localtime);

  processPhotoBrightness();
  processScheduledNtpSync();
  processUpdatePolling(localtime);
  renderClockIfNeeded(localtime);
  processTouchInput();
}
```

### D. Move Update Polling Wrapper Into Network Manager

`next_update_check` is now defined in `network_manager.cpp`, but `main.cpp` still contains the timer wrapper logic.

Move this:

```cpp
if (now_ms >= next_update_check)
{
  if ((localtime.tm_sec % next_update_modular) == 0)
  {
    poll_update_server();
    next_update_check = now_ms + 1000;
  }
}
```

Into:

```cpp
void processUpdatePolling(const struct tm &localtime);
```

### E. Replace Some Shared Globals With Accessors

`app_state.h` is organized now, but it still exposes broad mutable state.

Longer-term, prefer functions such as:

```cpp
int getBrightness();
const String &getCurrentSystemId();
int getSystemIdCount();
const String &getSystemIdAt(int index);
void resetClockRedrawState();
```

Do this gradually. Do not replace everything at once.

## Desired End State

A realistic final `main.cpp` could be:

```cpp
#include "app_controller.h"

void setup()
{
  setupApplication();
}

void loop()
{
  processApplicationLoop();
}
```

A less extreme version would also be acceptable:

```cpp
void setup()
{
  initializeEarlyHardware();
  initializeBootDisplay();
  setupApplication();
}

void loop()
{
  processApplicationLoop();
}
```

## Build Command

Use the targeted environment build:

```sh
/c/Users/Stephen/.platformio/penv/Scripts/python.exe -m platformio run -e cyd
```
