# CYD Clock

`CYD Clock` is a PlatformIO project for the ESP32-2432S028R "Cheap Yellow Display" board. It turns the device into a touchscreen network clock/calendar with a large TFT time display, SD-card based configuration, and optional live config updates from a small HTTP server.

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
