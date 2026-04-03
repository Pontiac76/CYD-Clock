#!/usr/bin/env python3

import re
import subprocess
from datetime import datetime, timedelta
from calendar import monthrange

ZDUMP_LINE_RE = re.compile(
    r"""
    ^(?P<zone>\S+)\s+
    (?P<utc_dow>\w{3})\s+(?P<utc_mon>\w{3})\s+(?P<utc_day>\d{1,2})\s+
    (?P<utc_hms>\d{2}:\d{2}:\d{2})\s+(?P<utc_year>\d{4})\s+UT\s+=\s+
    (?P<loc_dow>\w{3})\s+(?P<loc_mon>\w{3})\s+(?P<loc_day>\d{1,2})\s+
    (?P<loc_hms>\d{2}:\d{2}:\d{2})\s+(?P<loc_year>\d{4})\s+
    (?P<abbr>\S+)\s+isdst=(?P<isdst>[01])\s+gmtoff=(?P<gmtoff>-?\d+)
    $
    """,
    re.VERBOSE,
)

MONTHS = {
    "Jan": 1, "Feb": 2, "Mar": 3, "Apr": 4,
    "May": 5, "Jun": 6, "Jul": 7, "Aug": 8,
    "Sep": 9, "Oct": 10, "Nov": 11, "Dec": 12,
}


def parse_zdump_line(line):
    match = ZDUMP_LINE_RE.match(line.strip())
    if not match:
        return None

    data = match.groupdict()

    local_dt = datetime(
        int(data["loc_year"]),
        MONTHS[data["loc_mon"]],
        int(data["loc_day"]),
        int(data["loc_hms"][0:2]),
        int(data["loc_hms"][3:5]),
        int(data["loc_hms"][6:8]),
    )

    return {
        "abbr": data["abbr"],
        "isdst": int(data["isdst"]),
        "gmtoff": int(data["gmtoff"]),
        "local_dt": local_dt,
    }


def nth_weekday_in_month(dt):
    posix_weekday = (dt.weekday() + 1) % 7

    first_day_weekday, _ = monthrange(dt.year, dt.month)
    first_day_posix = (first_day_weekday + 1) % 7

    first_occurrence_day = 1 + ((posix_weekday - first_day_posix) % 7)
    week_num = ((dt.day - first_occurrence_day) // 7) + 1

    return dt.month, week_num, posix_weekday


def gmtoff_to_posix_offset(gmtoff_seconds):
    total_hours = -gmtoff_seconds / 3600

    if total_hours.is_integer():
        return str(int(total_hours))

    sign = "-" if total_hours < 0 else ""
    total_hours = abs(total_hours)
    hours = int(total_hours)
    minutes = int(round((total_hours - hours) * 60))
    return f"{sign}{hours:02d}:{minutes:02d}"


def format_rule(local_transition_time):
    month, week, weekday = nth_weekday_in_month(local_transition_time)
    return f"M{month}.{week}.{weekday}/{local_transition_time:%H:%M:%S}"


def get_zdump_lines(zone):
    result = subprocess.run(
        ["zdump", "-v", zone],
        capture_output=True,
        text=True,
        check=True,
    )
    return result.stdout.splitlines()


def build_posix_tz_from_zdump(zone, year=None):
    if zone == "UTC":
        return "GMT0"

    if year is None:
        year = datetime.now().year

    parsed = []
    for line in get_zdump_lines(zone):
        item = parse_zdump_line(line)
        if item is not None:
            parsed.append(item)

    if len(parsed) == 0:
        raise RuntimeError(f"Not enough zdump data for zone {zone}")

    dst_start = None
    dst_end = None

    for previous, current in zip(parsed, parsed[1:]):
        if previous["local_dt"].year != year and current["local_dt"].year != year:
            continue

        if previous["isdst"] == 0 and current["isdst"] == 1:
            dst_start = (previous, current)

        if previous["isdst"] == 1 and current["isdst"] == 0:
            dst_end = (previous, current)

    if dst_start is not None and dst_end is not None:
        start_prev, start_curr = dst_start
        end_prev, end_curr = dst_end

        std_abbr = start_prev["abbr"]
        std_offset = gmtoff_to_posix_offset(start_prev["gmtoff"])
        dst_abbr = start_curr["abbr"]

        start_local_transition = start_prev["local_dt"] + timedelta(seconds=1)
        end_local_transition = end_prev["local_dt"] + timedelta(seconds=1)

        start_rule = format_rule(start_local_transition)
        end_rule = format_rule(end_local_transition)

        return f"{std_abbr}{std_offset}{dst_abbr},{start_rule},{end_rule}"

    std_candidates = [
        item for item in parsed
        if item["isdst"] == 0 and item["local_dt"].year == year
    ]
    if not std_candidates:
        std_candidates = [item for item in parsed if item["isdst"] == 0]

    if not std_candidates:
        raise RuntimeError(f"Could not determine standard offset for {zone}")

    std_choice = max(std_candidates, key=lambda item: item["local_dt"])
    std_abbr = std_choice["abbr"]
    std_offset = gmtoff_to_posix_offset(std_choice["gmtoff"])

    if dst_start is None or dst_end is None:
        return f"{std_abbr}{std_offset}"


if __name__ == "__main__":
    print(build_posix_tz_from_zdump("America/Toronto"))
