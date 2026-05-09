# TODO

This file tracks remaining work only.

## Next Up

- Continue shrinking `main.cpp` toward orchestration only.
  - Move hardware initialization into a `hardware_manager` or similar.
  - Move boot/setup orchestration into an `app_controller` or similar.
  - Move clock render-loop logic into `display_manager`.
  - Move update polling timer wrapper into `network_manager`.
- Replace broad shared globals in `app_state.h` with module getters/setters where it makes sense.
- Change/refine the clock face font size and visual layout.
- Add visible network status to the UI.
- Add last successful NTP sync status to the UI.

## Refactor Follow-Up

- Create a hardware initialization module for:
  - serial startup
  - random seed
  - touch CS pin setup
  - early TFT boot/status display
  - configured backlight/photoresistor setup
- Create an application controller/bootstrap module for:
  - SD/RAM-only boot detection
  - storage listing during boot
  - system ID/config loading
  - Wi-Fi/profile setup
  - clock-mode vs setup-portal decision
  - initial NTP sync
- Add `processUpdatePolling(const struct tm &localtime)` to `network_manager`.
- Add `renderClockIfNeeded(const struct tm &localtime)` to `display_manager`.
- Gradually replace direct `extern` access with narrower APIs.

## Config Source And Data Sensitivity

- Continue the config source-of-truth migration toward `data/` and `_private/` separation.
- Make LittleFS the primary known-good `config.txt` source at runtime.
- Read SD card config second and treat it as an override layer on top of the LittleFS baseline.
- Treat `data/config.defaults.txt` and any files pushed to LittleFS as public/default data only.
- Ensure the local config server reads private config from `_private/config.txt` rather than public `data/config.defaults.txt` when serving personalized config.
- Review whether any remaining sensitive data can accidentally be committed or pushed into LittleFS defaults.

## Wi-Fi Configuration And Onboarding

Completed baseline:

- Dedicated LittleFS `/wifi.txt` support exists.
- Setup AP/captive portal flow exists.
- QR-code setup flow exists.
- Setup form can write Wi-Fi profile, config, and system IDs.

Remaining:

- Support a list of known Wi-Fi access points rather than only using the first profile.
- Allow fallback across multiple home APs, phone hotspot, travel router, and other known networks.
- Build toward a UI flow for selecting a Wi-Fi network on-device or through LAN setup mode.
- Add long-press gesture to enter configuration/setup mode intentionally.
- On configuration-mode entry, determine whether the unit is online by checking current network connection state.
- If already connected to LAN, start only the web configuration server and do not enter AP mode.
- If not connected, start setup AP mode and use QR flow only to get the CYD onto working Wi-Fi.
- After Wi-Fi recovery/setup completes, return to normal clock mode.
- Require a second long-press after network recovery to enter the full configuration web interface.
- In LAN configuration mode, show the device IP address on the CYD display.
- Show a short six-digit code on the CYD display and require it in the web configuration page as lightweight physical-presence verification.
- Add QR code for phones to open the LAN configuration page.
- Keep the CYD display limited to QR codes, status, IP address, and short verification codes; avoid building an on-device virtual keyboard.

## Connectivity And Recovery

- Add on-screen network status indicators.
- Show the age or timestamp of the last successful NTP synchronization.
- Track Wi-Fi state in a way that can drive status icons and color states.
- If an NTP check fails, disconnect Wi-Fi and attempt to reconnect before retrying sync.
- Separate network credentials fully from general clock config.
- Plan safe failure-testing methods that do not require pulling storage or disrupting the whole network.
- Test server-unreachable behavior independently from Wi-Fi failure when possible.
- Test NTP failure independently from general network failure when possible.
- Use targeted firewall or service blocking to simulate unreachable resources during development.
- Build repeatable retry/recovery tests for dead APs, bad credentials, and fallback AP selection.

## OTA / Manual Firmware Update Planning

Current direction:

- Do not auto-install firmware updates.
- Show an update-available indication at runtime.
- Require explicit local user action, likely through a long-press menu, before installing firmware.

Remaining planning/tasks:

- Decide between standard dual-OTA partitions and custom recovery-partition design.
- If using recovery partition:
  - Create minimal recovery firmware with SD, TFT/status/menu, touch, partition read/write, checksum, and reboot only.
  - Keep recovery firmware independent of Wi-Fi, clock rendering, setup portal, and config parsing.
  - Define SD firmware directory layout, e.g. `/firmware/factory.bin`, `/firmware/factory.sha256`, downloads, and backups.
  - Investigate reading the running app partition and writing a factory backup to SD on first boot if missing.
  - Decide whether `factory.bin` is a full app-partition image or exact firmware image bytes.
  - Define checksum metadata format.
  - Define recovery restore behavior when update write fails.
- If using standard dual OTA:
  - Create OTA-capable partition table.
  - Ensure firmware size remains safely below OTA slot size.
- Add update-available state to UI, possibly as a low-priority runtime schedule/status entry.

## Scheduled Text Follow-Up

- Rotate through active-entry combinations over time when more entries are active than can fit on screen.
- Cache the currently shown schedule content and skip redraws when nothing visible has changed.
- Add a future full-screen view for active and upcoming events, likely with auto-scrolling.
- Revisit how many visible schedule lines can fit once the clock-face sizing is settled.

Overflow display example:

- If three events are active in the current minute but there is only room for two lines, show two entries at a time.
- The visible pair should rotate over time based on the sorted active set.
- Example rotation:
  - minute 1: events 1 and 2
  - minute 2: events 2 and 3
  - minute 3: events 1 and 3

## Dimming Follow-Up

- Decide whether time-based dimming remains the primary model.
- Evaluate ambient-light-based dimming as an alternative or additional mode.
- Leave room for supporting multiple dimming modes later, such as manual, time-based, and sensor-based.
- Consider replacing remaining direct brightness globals with `brightness_manager` accessors.

## Display Asset Strategy

- Keep exploring static image assets that can be created externally and placed on SD as needed.
- Keep image naming predictable so display states can be swapped without code churn.
- Keep the clock face dynamic: generate LCD-style segments in RAM and render the composed bitmap to the display.
- Define a segment model that supports runtime scaling, colorization, and style preferences without relying on baked fonts.
- Keep room for combining static icons and segment-rendered text/clock elements in the same UI.
- Use predictable generated filenames such as `WIFI_Offline`, `WIFI_Online`, and `WIFI_Reconnecting` for state-specific assets.
- Allow one source SVG to be rendered into multiple output states by remapping its source colors through a per-state palette definition.
- Keep source SVG colors visually obvious for editing, then convert them into related tone ranges for generated runtime assets.
- Use palette mapping to create flexible icons with subtle tone variation instead of maintaining separate hand-drawn assets for each state.

## Server Tasks

- Keep future iCal import support compiling down into the same flat `scheduleN` format.
- Keep emitting display-ready schedule/config payloads without assuming server-side SVG rendering unless that path is chosen explicitly.
- Generate display-ready image assets from SVG sources and state-based color mappings if the server-rendered icon path is adopted.
- Continue using shared/default schedule sections plus per-system overrides.
- Remove perfect duplicate schedule entries after section merging so the same event is not emitted twice.
- Add firmware metadata endpoint if manual firmware update notification is implemented.

## Non-Goals For The Next Pass

These are deferred features, not rejected features. They should not drive the next implementation step, but the structure should not block them later.

- iCal parsing on the ESP32
- "Show event X days before" logic
- User-facing validation on the ESP32
- Schedule section parsing on the ESP32
- Mixed wildcard semantics beyond `*|*`
- Full dynamic layout fitting for arbitrarily many active events
- Fully automatic unattended firmware installation
