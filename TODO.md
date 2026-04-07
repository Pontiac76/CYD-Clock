# TODO

This file is the project-facing working list for planned changes that are not yet fully tracked elsewhere. It is meant to stay lightweight, practical, and easy to update as the clock firmware and helper server evolve.

## Near Term

- Change the clock face font size and continue refining the visual layout.
- Keep exploring image-based rendering, including server-generated assets for the CYD to display.
- Move toward a more templatable UI so other people can build their own display layouts.
- Continue the config source-of-truth migration toward `data/`.
- Make LittleFS the primary known-good `config.txt` source at runtime.
- Read SD card config second and treat it as an override layer on top of the LittleFS baseline.
- Add visible network status and last successful NTP sync status to the UI.

## Highlights

- Split runtime concerns cleanly: LittleFS baseline config, SD override config, and separate Wi-Fi onboarding data.
- Keep the ESP32 focused on local parsing, matching, display state, and recovery behavior.
- Push richer authoring, normalization, section merging, deduplication, and image generation to the Python side.
- Build toward a reusable, templatable UI rather than a fixed one-off display.
- Keep room in the design for future richer event views, onboarding flows, and sensor-driven behavior without requiring major rewrites.

## Scheduled Text Follow-Up

- Add startup/runtime config layering so LittleFS is read first and SD overrides are applied second.
- Rotate through active-entry combinations over time when more entries are active than can fit on screen.
- Cache the currently shown message and redraw only when the visible entry changes, if practical.
- Add a future full-screen view for active and upcoming events, likely with auto-scrolling.

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

## Dimming Strategy

- Keep dimming on the roadmap, but do not lock the project into one approach too early.
- Evaluate time-based dimming as the simpler predictable mode.
- Evaluate ambient-light-based dimming as the more adaptive mode.
- Leave room for supporting multiple dimming modes later, such as manual, time-based, and sensor-based.

## Server-Rendered UI Assets

- The Python server will handle image rendering rather than pushing that work onto the CYD.
- Build toward multi-color SVG source assets that are easy to understand and edit.
- Define state-driven color mappings outside the SVG so one source asset can render differently depending on the clock state.
- Use filenames and state naming conventions to indicate both image purpose and visual state.
- Generate CYD-ready RGB assets for states such as Wi-Fi connected, connecting, or failed.
- Keep the rendering pipeline generic enough that the same approach can be used for other icons and display elements later.
- Use predictable generated filenames such as `WIFI_Offline`, `WIFI_Online`, and `WIFI_Reconnecting` for state-specific assets.
- Allow one source SVG to be rendered into multiple output states by remapping its source colors through a per-state palette definition.
- Keep source SVG colors visually obvious for editing, then convert them into related tone ranges for the generated runtime assets.
- Use that palette-mapping approach to create flexible icons with subtle tone variation instead of maintaining separate hand-drawn assets for each state.

### Server Tasks

- Support human-edited schedule sections such as `[Schedule]`, `[Schedule:Bedroom]`, and `[Schedule:Desk]`.
- Treat `[Schedule]` as the shared/default section that applies to all clocks.
- If a named clock identity is available, merge `[Schedule]` with the matching per-clock section before emitting flattened `scheduleN` keys.
- Keep future iCal import support compiling down into the same flat `scheduleN` format.
- Generate display-ready image assets from SVG sources and state-based color mappings.

### Clock Identity

- Use a small local file such as `/systemid.txt` rather than MAC address to identify the clock.
- Display the clock name or friendly identity on screen when appropriate in a later pass.
- Use that identity on the server side to choose which schedule block applies.
- If `systemid.txt` is missing, treat the device as unnamed and only apply the shared `[Schedule]` entries.

### Non-Goals For First Pass

These are deferred features, not rejected features. They are out of scope for the first implementation, but the framework should leave room to support them later without major rewrites.

- iCal parsing on the ESP32
- "Show event X days before" logic
- User-facing validation on the ESP32
- Schedule section parsing on the ESP32
- Mixed wildcard semantics beyond `*|*`
- Priority fields
- Dynamically fitting and showing only as many active events as the current layout allows, while rotating through the larger active set over time

Display example:

- If three events are active in the current minute but there is only room for two lines, show two entries at a time.
- The visible pair should rotate over time based on the sorted active set.
- Example rotation:
- minute 1: events 1 and 2
- minute 2: events 2 and 3
- minute 3: events 1 and 3
