# TODO

This file tracks remaining work only.

## Next Up

- Change the clock face font size and continue refining the visual layout.
- Add visible network status to the UI.
- Add last successful NTP sync status to the UI.
- Continue the config source-of-truth migration toward `data/`.
- Make LittleFS the primary known-good `config.txt` source at runtime.
- Read SD card config second and treat it as an override layer on top of the LittleFS baseline.

## Scheduled Text Follow-Up

- Rotate through active-entry combinations over time when more entries are active than can fit on screen.
- Cache the currently shown schedule content and skip redraws when nothing visible has changed.
- Add a future full-screen view for active and upcoming events, likely with auto-scrolling.
- Revisit how many visible schedule lines can fit once the clock-face sizing is settled.

## Connectivity And Recovery

- Add on-screen network status indicators.
- Show the age or timestamp of the last successful NTP synchronization.
- If an NTP check fails, disconnect Wi-Fi and attempt to reconnect before retrying sync.
- Track Wi-Fi state in a way that can later drive status icons and color states.
- Separate network credentials from general clock config.
- Plan safe failure-testing methods that do not require pulling storage or disrupting the whole network.
- Test server-unreachable behavior independently from Wi-Fi failure when possible.
- Test NTP failure independently from general network failure when possible.
- Use targeted firewall or service blocking to simulate unreachable resources during development.
- Build toward repeatable retry and recovery tests for dead APs, bad credentials, and fallback AP selection.

## Wi-Fi Configuration And Onboarding

- Move Wi-Fi credentials out of `config.txt` into a dedicated `wifi.txt` file.
- Support a list of known Wi-Fi access points rather than a single SSID/password pair.
- Allow fallback across multiple home APs, phone hotspot, travel router, and other known networks.
- Build toward a UI flow for selecting a Wi-Fi network on-device.
- Build toward an input method for entering or updating Wi-Fi passwords on-device.

## Dimming Follow-Up

- Decide whether time-based dimming remains the primary model.
- Evaluate ambient-light-based dimming as an alternative or additional mode.
- Leave room for supporting multiple dimming modes later, such as manual, time-based, and sensor-based.

## Display Asset Strategy

- Keep exploring static image assets that can be created externally and placed on SD as needed.
- Keep image naming predictable so display states can be swapped without code churn.
- Keep the clock face dynamic: generate LCD-style segments in RAM and render the composed bitmap to the display.
- Define a segment model that supports runtime scaling, colorization, and style preferences without relying on baked fonts.
- Keep room for combining static icons and segment-rendered text/clock elements in the same UI.
- Use predictable generated filenames such as `WIFI_Offline`, `WIFI_Online`, and `WIFI_Reconnecting` for state-specific assets.
- Allow one source SVG to be rendered into multiple output states by remapping its source colors through a per-state palette definition.
- Keep source SVG colors visually obvious for editing, then convert them into related tone ranges for the generated runtime assets.
- Use that palette-mapping approach to create flexible icons with subtle tone variation instead of maintaining separate hand-drawn assets for each state.

## Server Tasks

- Keep future iCal import support compiling down into the same flat `scheduleN` format.
- Keep emitting display-ready schedule/config payloads without assuming server-side SVG rendering unless that path is chosen explicitly.
- Generate display-ready image assets from SVG sources and state-based color mappings if the server-rendered icon path is adopted.
- Continue using shared/default schedule sections plus per-system overrides.
- Remove perfect duplicate schedule entries after section merging so the same event is not emitted twice.

## Non-Goals For The Next Pass

These are deferred features, not rejected features. They should not drive the next implementation step, but the structure should not block them later.

- iCal parsing on the ESP32
- "Show event X days before" logic
- User-facing validation on the ESP32
- Schedule section parsing on the ESP32
- Mixed wildcard semantics beyond `*|*`
- Full dynamic layout fitting for arbitrarily many active events

Overflow display example:

- If three events are active in the current minute but there is only room for two lines, show two entries at a time.
- The visible pair should rotate over time based on the sorted active set.
- Example rotation:
- minute 1: events 1 and 2
- minute 2: events 2 and 3
- minute 3: events 1 and 3
