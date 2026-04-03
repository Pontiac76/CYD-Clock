#!/usr/bin/env python3
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from datetime import datetime
import subprocess
import sys
import re

SOURCE_FILE = Path("SD_Card/config.txt")
ZONEINFO_ROOT = Path("/usr/share/zoneinfo")
HEADER_PREFIX = "#CFG "
BIND_HOST = "0.0.0.0"
BIND_PORT = 8080


def log(msg: str) -> None:
    now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    print(f"[{now}] {msg}", flush=True)


def sanitize_timezone_input(value: str) -> str:
    value = value.strip()
    value = value.replace("\\", "/")
    value = value.replace(" ", "_")
    value = re.sub(r"/+", "/", value)
    value = re.sub(r"[^A-Za-z0-9_+\-/]", "", value)
    return value.strip("/")


def iter_zone_names():
    for path in ZONEINFO_ROOT.rglob("*"):
        if not path.is_file():
            continue

        rel = path.relative_to(ZONEINFO_ROOT).as_posix()

        if rel.startswith(("right/", "posix/", "SystemV/", "Etc/")):
            continue

        yield rel


def resolve_timezone_name(user_value: str) -> str | None:
    wanted = sanitize_timezone_input(user_value)
    if wanted == "":
        return None

    wanted_lower = wanted.lower()
    matches = [name for name in iter_zone_names() if name.lower() == wanted_lower]

    if len(matches) == 1:
        return matches[0]

    if len(matches) > 1:
        # Prefer the canonical-looking one with the fewest path components.
        matches.sort(key=lambda x: (x.count("/"), x))
        return matches[0]

    return None


def parse_offset_seconds(text: str) -> int:
    # Examples: -18000, 3600
    return int(text)


def posix_offset_from_gmtoff(gmtoff_seconds: int) -> str:
    # POSIX TZ offset uses reversed sign convention compared to UTC offset.
    # Example: gmtoff=-18000 => EST5
    total = -gmtoff_seconds
    sign = "-" if total < 0 else ""
    total = abs(total)

    hours = total // 3600
    minutes = (total % 3600) // 60
    seconds = total % 60

    if seconds != 0:
        return f"{sign}{hours}:{minutes:02d}:{seconds:02d}"
    if minutes != 0:
        return f"{sign}{hours}:{minutes:02d}"
    return f"{sign}{hours}"


def month_number_from_name(name: str) -> int:
    months = {
        "Jan": 1, "Feb": 2, "Mar": 3, "Apr": 4, "May": 5, "Jun": 6,
        "Jul": 7, "Aug": 8, "Sep": 9, "Oct": 10, "Nov": 11, "Dec": 12,
    }
    return months[name]


def weekday_number_for_posix(dt: datetime) -> int:
    # POSIX: 0=Sunday ... 6=Saturday
    return (dt.weekday() + 1) % 7


def week_of_month(dt: datetime) -> int:
    # POSIX uses 1..5
    return ((dt.day - 1) // 7) + 1


def parse_zdump_transition_line(line: str):
    # Example:
    # America/Toronto  Sun Mar  8 06:59:59 2026 UT = Sun Mar  8 01:59:59 2026 EST isdst=0 gmtoff=-18000
    #
    # We parse the LOCAL side (right-hand side of '='), abbreviation, isdst, gmtoff.
    if " = " not in line:
        return None

    left, right = line.split(" = ", 1)

    m = re.search(
        r"^(?P<wday>[A-Z][a-z]{2})\s+"
        r"(?P<mon>[A-Z][a-z]{2})\s+"
        r"(?P<day>\d{1,2})\s+"
        r"(?P<hour>\d{2}):(?P<minute>\d{2}):(?P<second>\d{2})\s+"
        r"(?P<year>\d{4})\s+"
        r"(?P<abbr>[A-Za-z0-9+\-]+)\s+"
        r"isdst=(?P<isdst>[01])\s+"
        r"gmtoff=(?P<gmtoff>-?\d+)$",
        right.strip(),
    )
    if not m:
        return None

    dt = datetime(
        year=int(m.group("year")),
        month=month_number_from_name(m.group("mon")),
        day=int(m.group("day")),
        hour=int(m.group("hour")),
        minute=int(m.group("minute")),
        second=int(m.group("second")),
    )

    return {
        "dt": dt,
        "abbr": m.group("abbr"),
        "isdst": int(m.group("isdst")),
        "gmtoff": parse_offset_seconds(m.group("gmtoff")),
    }


def build_rule_fragment(dt: datetime) -> str:
    month = dt.month
    week = week_of_month(dt)
    weekday = weekday_number_for_posix(dt)

    if dt.second == 0:
        time_part = f"{dt.hour}"
        if dt.minute != 0:
            time_part = f"{dt.hour}:{dt.minute:02d}"
    else:
        time_part = f"{dt.hour}:{dt.minute:02d}:{dt.second:02d}"

    return f"M{month}.{week}.{weekday}/{time_part}"


def build_tzinfo_from_zone(zone_name: str) -> str:
    if zone_name == "UTC":
        return "GMT0"

    year = datetime.now().year

    proc = subprocess.run(
        ["zdump", "-v", zone_name],
        capture_output=True,
        text=True,
        check=False,
    )

    if proc.returncode != 0:
        raise RuntimeError(f"zdump failed for {zone_name}: {proc.stderr.strip()}")

    transitions = []
    abbreviations = []

    for raw_line in proc.stdout.splitlines():
        line = raw_line.strip()
        if f" {year} " not in line:
            continue

        parsed = parse_zdump_transition_line(line)
        if parsed is None:
            continue

        abbreviations.append((parsed["abbr"], parsed["gmtoff"], parsed["isdst"]))

        if parsed["isdst"] == 1:
            transitions.append(parsed)

    # Find standard abbreviation/offset from current-year lines.
    std_candidates = [x for x in abbreviations if x[2] == 0]
    dst_candidates = [x for x in abbreviations if x[2] == 1]

    if not std_candidates:
        raise RuntimeError(f"Could not determine standard offset for {zone_name}")

    std_abbr, std_gmtoff, _ = std_candidates[0]
    std_offset = posix_offset_from_gmtoff(std_gmtoff)

    # No DST seen this year.
    if not dst_candidates or len(transitions) == 0:
        return f"{std_abbr}{std_offset}"

    dst_abbr, _, _ = dst_candidates[0]

    # We want the local time immediately after each transition into DST
    # and immediately after each transition back to standard time.
    start_candidates = []
    end_candidates = []

    for raw_line in proc.stdout.splitlines():
        line = raw_line.strip()
        if f" {year} " not in line:
            continue

        parsed = parse_zdump_transition_line(line)
        if parsed is None:
            continue

        if parsed["isdst"] == 1:
            start_candidates.append(parsed)
        else:
            end_candidates.append(parsed)

    # Heuristic:
    # - DST start: earliest isdst=1 transition in the year
    # - DST end: latest isdst=0 transition after DST started
    dst_start = min(start_candidates, key=lambda x: x["dt"])
    after_start_std = [x for x in end_candidates if x["dt"] > dst_start["dt"]]

    if not after_start_std:
        return f"{std_abbr}{std_offset}{dst_abbr}"

    dst_end = min(after_start_std, key=lambda x: x["dt"])

    start_rule = build_rule_fragment(dst_start["dt"])
    end_rule = build_rule_fragment(dst_end["dt"])

    return f"{std_abbr}{std_offset}{dst_abbr},{start_rule},{end_rule}"


def load_source_lines() -> list[str]:
    if not SOURCE_FILE.exists():
        return []

    text = SOURCE_FILE.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()

    if lines and lines[0].startswith(HEADER_PREFIX):
        lines = lines[1:]

    return lines


def parse_config_lines(lines: list[str]):
    parsed = []
    timezone_value = None

    for raw_line in lines:
        clean = raw_line.rstrip("\r\n")

        if clean.strip() == "":
            parsed.append(("raw", clean))
            continue

        if clean.lstrip().startswith("#"):
            parsed.append(("raw", clean))
            continue

        if "=" not in clean:
            parsed.append(("raw", clean))
            continue

        key, value = clean.split("=", 1)
        key = key.strip()
        value = value.strip()

        parsed.append(("kv", key, value))

        if key == "timezone":
            timezone_value = value

    return parsed, timezone_value


def rebuild_config_text() -> bytes:
    lines = load_source_lines()
    parsed, timezone_value = parse_config_lines(lines)

    resolved_zone = None
    generated_tzinfo = None

    if timezone_value is not None and timezone_value.strip() != "":
        resolved_zone = resolve_timezone_name(timezone_value)
        if resolved_zone is None:
            log(f'timezone "{timezone_value}" is invalid, falling back to UTC')
            resolved_zone = "UTC"

        try:
            generated_tzinfo = build_tzinfo_from_zone(resolved_zone)
        except Exception as exc:
            log(f'tzinfo generation failed for "{resolved_zone}": {exc}; falling back to GMT0')
            resolved_zone = "UTC"
            generated_tzinfo = "GMT0"

        log(f'timezone "{timezone_value}" resolved to "{resolved_zone}", tzinfo="{generated_tzinfo}"')

    if SOURCE_FILE.exists():
        mtime = datetime.fromtimestamp(SOURCE_FILE.stat().st_mtime)
    else:
        mtime = datetime.now()

    header = f"{HEADER_PREFIX}{mtime.strftime('%Y-%m-%d@%H:%M:%S')}"
    output_lines = [header]

    tzinfo_written = False
    timezone_written = False

    for item in parsed:
        if item[0] == "raw":
            output_lines.append(item[1])
            continue

        _, key, value = item

        if key == "timezone" and resolved_zone is not None:
            output_lines.append(f"timezone={resolved_zone}")
            timezone_written = True
            continue

        if key == "tzinfo" and generated_tzinfo is not None:
            output_lines.append(f"tzinfo={generated_tzinfo}")
            tzinfo_written = True
            continue

        output_lines.append(f"{key}={value}")

    if resolved_zone is not None and not timezone_written:
        output_lines.append(f"timezone={resolved_zone}")

    if generated_tzinfo is not None and not tzinfo_written:
        output_lines.append(f"tzinfo={generated_tzinfo}")

    text = "\n".join(output_lines)
    if not text.endswith("\n"):
        text += "\n"

    return text.encode("utf-8")


class ConfigHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        try:
            payload = rebuild_config_text()
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(payload)))
            self.send_header("Cache-Control", "no-store, no-cache, must-revalidate")
            self.end_headers()
            self.wfile.write(payload)
        except Exception as exc:
            msg = f"#CFG ERROR\n{str(exc)}\n".encode("utf-8", errors="replace")
            self.send_response(500)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(msg)))
            self.send_header("Cache-Control", "no-store, no-cache, must-revalidate")
            self.end_headers()
            self.wfile.write(msg)
            log(f"error serving config: {exc}")

    def log_message(self, fmt, *args):
        log(f'{self.client_address[0]} "{self.requestline}"')


def main():
    log(f"serving {SOURCE_FILE} on http://{BIND_HOST}:{BIND_PORT}/")
    server = HTTPServer((BIND_HOST, BIND_PORT), ConfigHandler)
    server.serve_forever()


if __name__ == "__main__":
    main()