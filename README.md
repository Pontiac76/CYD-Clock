# CYD Clock

`CYD Clock` is a PlatformIO project for the ESP32-2432S028R "Cheap Yellow Display" board. It turns the device into a touchscreen network clock with a large TFT time display, SD-card based configuration, and optional live config updates from a small HTTP server.

The firmware reads `config.txt` from the SD card at startup, connects to Wi-Fi, applies time zone and NTP settings, and renders the current date and time on the display. It also supports translated weekday and month names, adjustable screen brightness through touch input, and a bootstrap/update flow that can pull refreshed configuration from a remote URL.

## What It Does

- Displays the current local date and time on the CYD TFT screen
- Syncs time from an NTP server using configurable time zone data
- Loads runtime settings from `config.txt` on the SD card
- Supports long-form translated month and weekday names with English defaults
- Lets the user adjust backlight brightness using the touchscreen
- Can fetch updated configuration from a simple HTTP config server

## Project Layout

- `src/main.cpp` contains the device firmware
- `data/config.txt.sample` is an example runtime configuration file.  This file needs to be copied or renamed to config.txt with your SSID and password settings
- `config_server.py` serves config data to the device over HTTP and generates `tzinfo` values from IANA time zone names
- `gen_tzinfo.py` is a standalone helper for generating POSIX-style `tzinfo` strings from IANA time zone names using `zdump`
- `platformio.ini` defines the PlatformIO environment, board, and libraries

## Hardware / Software Stack

- ESP32-2432S028R board
- Arduino framework via PlatformIO
- `TFT_eSPI` for the display
- `XPT2046_Touchscreen` for touch input
- ESP32 Wi-Fi and time APIs for network time sync
- SD card support for local configuration

## Configuration

The firmware expects a `config.txt` file containing values such as:

- Wi-Fi SSID and password
- Time zone / `tzinfo`
- NTP server
- 12h or 24h time format
- Brightness
- Latitude / longitude
- `WeekDays` and `MonthName` translation overrides
- `updateurl` for remote config refresh

Config keys are handled case-insensitively, and weekday/month translations fall back to built-in English defaults when not provided.

## Notes

The `config_server.py` helper is intended to run on a Linux system that has access to `zdump` and the IANA zoneinfo database, so it can take a friendly `timezone` value such as `America/Toronto`, resolve it, and generate the POSIX-style `tzinfo` string the ESP32 firmware uses.

In its current form, `config_server.py` reads the source config file, normalizes the `timezone` entry, regenerates `tzinfo` when needed, adds a timestamp header, and serves a cleaned config payload over HTTP on port `8080`.

The `gen_tzinfo.py` script performs the same timezone-conversion job as a standalone utility, and appears to serve as a simple helper or prototype for the `tzinfo` generation logic now built into `config_server.py`.

On Windows, the `windows_8080.ps1` script forwards port `8080` from Windows into WSL so the CYD can reach a config server running there and obtain updates.

## Original Source
https://github.com/barni007-pro/w_Display_CYD

CYD-Clock began as a local adaptation of the `w_Display_CYD` codebase. I originally chose that project because it closely matched the behavior and structure I wanted.

The original project was built for the Arduino IDE, but I found the build workflow frustratingly slow for iterative development. Even very small changes always triggered a full rebuild, and I did not find a practical way to change that behavior within the Arduino IDE. After spending a couple of days porting the project, I moved the code to PlatformIO, which felt more natural for my workflow and significantly improved build times.

That said, the Arduino IDE and PlatformIO manage dependencies and resources differently, so the original code did not transfer over without work. Bringing it into PlatformIO required meaningful changes to get everything functioning correctly.

While CYD-Clock has its roots in `w_Display_CYD`, this repository is not a fork. I started from a downloaded ZIP of the original project, extracted it locally, and then substantially reworked it to fit my own goals. Since then, the project has changed significantly: core logic has been overhauled, input sanitization has been added, and the calendar functionality has been removed entirely.

CYD-Clock is a personal, non-commercial project. It is somewhat niche, somewhat advanced, and depends on external tooling and home-hosted resources that most users are unlikely to have or want to maintain.
