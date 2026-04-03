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

## Scheduled Text System

Goal:
The clock should be able to receive flat `textentryN` rules from the server and display the correct scheduled text based on local date and time, reliably and with minimal firmware-side complexity.

### Design Direction

- The Python server owns the human-friendly authoring format.
- The ESP32 consumes only flat `key=value` config.
- Scheduled entries are emitted as numbered keys such as `textentry0`, `textentry1`, and so on.
- The server may also emit `clockname=<friendly text>` so the device can show its friendly name on screen.
- If the SD card contains `clockname.txt`, the ESP32 should read it and use that value to identify the clock and display its friendly name somewhere on screen.
- If `clockname.txt` does not exist, the clock should behave as an unnamed/default device and only consume schedule entries intended for all clocks.
- The ESP32 should evaluate schedule matching on minute boundaries, not every second.
- Dynamic schedule state must be cleared before applying fresh downloaded config so removed rules do not linger in memory.
- If multiple entries are active at the same time, the CYD should be able to show more than one entry at once, up to the available number of display lines.
- If more entries are active than can fit on screen, the CYD should rotate through the active set over time instead of dropping down to a single winner.
- Active entries should be ordered oldest to newest using case-insensitive sorting for display rotation.
- The CYD should determine the active set and display order locally, since recurring and one-time dates need to be interpreted against the current local date.
- Rotation should be based on the sorted active set and should allow different combinations of visible entries across successive minute updates when there are more active entries than display slots.

### Flat Rule Format

`textentryN=enabled|dayspec|start|end|text`

Examples:

- `textentry0=1|daily|20:00|00:00|Feed Dogs`
- `textentry1=1|dow:FRI|18:00|23:00|Weekend mode`
- `textentry2=1|date:2026-04-03|00:00|23:59|Doctor Appointment`
- `textentry3=1|date:04-03|00:00|23:59|Birthday Reminder`
- `textentry4=1|mod:30|*|*|Charge EBike`
- `textentry5=1|mod:30:2026-01-01|*|*|Filter Reminder`

Supported first-pass `dayspec` values:

- `daily`
- `dow:MON` through `dow:SUN`
- `date:YYYY-MM-DD`
- `date:MM-DD`
- `mod:N`
- `mod:N:YYYY-MM-DD`

Modular day notes:

- `mod:N` uses modular day-count math.
- `mod:N` without an explicit anchor date uses the fixed epoch `2000-01-01`.
- `mod:N:YYYY-MM-DD` is also valid and overrides the default modular anchor date.

Time-window rules:

- If `start <= end`, treat it as a same-day window.
- If `start > end`, treat it as an overnight window across midnight.
- If `start=*` and `end=*`, treat it as all day.
- Mixed wildcard forms are not required for first pass.

### Firmware Tasks

- Add a bounded schedule entry struct and storage array in the ESP32 firmware.
- Add config parsing for `textentryN` and `clockname`.
- Add startup/runtime config layering so LittleFS is read first and SD overrides are applied second.
- Clear/reset dynamic schedule entries before applying a fresh downloaded config payload.
- Parse each `textentryN` by splitting on `|`.
- Implement day matching for `daily`.
- Implement day matching for `dow:MON` style weekly rules.
- Implement day matching for `date:MM-DD` recurring yearly dates.
- Implement day matching for `date:YYYY-MM-DD` one-time dates.
- Implement day matching for `mod:N` recurring day-count rules from the fixed epoch `2000-01-01`.
- Implement day matching for `mod:N:YYYY-MM-DD` recurring day-count rules using a custom anchor date.
- Implement time-window matching, including overnight windows and `*|*`.
- Evaluate scheduled text on minute changes, not second changes.
- Build the active entry list on minute changes.
- Determine how many lines of scheduled text can be shown at once in the current UI.
- Sort or otherwise order active entries oldest to newest in a stable, case-insensitive way suitable for display rotation.
- Show as many active entries at once as the available display lines allow.
- Rotate through active-entry combinations over time when more entries are active than can fit on screen.
- Cache the currently shown message and redraw only when the visible entry changes, if practical.
- Keep message drawing independent from the time sprite so clock redraws do not wipe scheduled text.
- Add basic serial debug output for schedule parsing and matching only if it helps with bring-up.
- Add a future full-screen view for active and upcoming events, likely with auto-scrolling.

### Suggested Helper Functions

- `parseTextEntry(...)`
- `parseTimeToMinutes(...)`
- `matchesDaySpec(...)`
- `matchesTimeWindow(...)`
- `daysSinceEpoch2000(...)`
- `getActiveScheduledEntries(...)`
- `sortActiveScheduledEntries(...)`
- `getDisplayedScheduledMessage(...)`

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

- Support human-edited schedule sections such as `[Schedules]`, `[Schedules:Bedroom]`, and `[Schedules:Desk]`.
- Treat `[Schedules]` as the shared/default section that applies to all clocks.
- If a named clock identity is available, merge `[Schedules]` with the matching per-clock section before emitting flattened `textentryN` keys.
- If no named clock identity is available, emit only entries from `[Schedules]`.
- Remove perfect duplicate schedule entries after section merging so the same event is not emitted twice.
- Normalize friendly input formats and aliases before serving output.
- Use clock identity to select device-specific schedule blocks.
- Emit `clockname=<friendly text>` for display when appropriate.
- Keep future iCal import support compiling down into the same flat `textentryN` format.
- Generate display-ready image assets from SVG sources and state-based color mappings.

### Clock Identity

- Use a small local file such as `/clockname.txt` rather than MAC address to identify the clock.
- Display the clock name on screen when `clockname.txt` is present.
- Use that identity on the server side to choose which schedule block applies.
- If `clockname.txt` is missing, treat the device as unnamed and only apply the shared `[Schedules]` entries.

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
