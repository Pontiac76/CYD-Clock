#!/usr/bin/env python3
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from datetime import datetime
import re
from urllib.parse import parse_qs, urlparse

from gen_tzinfo import build_posix_tz_from_zdump

SOURCE_FILE = Path("data/config.txt")
TEMP_DIR = Path("temp")
ZONEINFO_ROOT = Path("/usr/share/zoneinfo")
HEADER_PREFIX = "# CFG "
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


def sanitize_system_id(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9]", "", value.strip())


def normalize_section_name(value: str) -> str:
    name = value.strip().lower()
    if name == "schedule":
        return "scheduled"
    if name.startswith("schedule:"):
        return "scheduled:" + name.split(":", 1)[1]
    return name


def parse_section_header(value: str) -> tuple[str, int, bool]:
    section_priority = 1
    section_priority_explicit = False
    raw = value.strip()
    match = re.match(r"^([1-9])~(.+)$", raw)
    if match:
        section_priority = int(match.group(1))
        section_priority_explicit = True
        raw = match.group(2).strip()
    normalized = normalize_section_name(raw)
    if (not section_priority_explicit) and normalized.startswith("scheduled"):
        section_priority = 9
    return normalized, section_priority, section_priority_explicit


def parse_scheduled_system_ids(section_name: str) -> list[str]:
    if not section_name.startswith("scheduled:"):
        return []

    system_ids: list[str] = []
    raw_targets = section_name.split(":", 1)[1]

    for raw_target in raw_targets.split(","):
        system_id = sanitize_system_id(raw_target)
        if system_id == "":
            continue
        system_ids.append(system_id.lower())

    return system_ids


def encode_schedule_priority(line: str, section_priority: int, section_priority_explicit: bool) -> str:
    event_priority = 9
    entry = line.strip()
    match = re.match(r"^([1-9])~(.+)$", entry)
    if match:
        event_priority = int(match.group(1))
        entry = match.group(2).strip()
    return f"{section_priority}~{event_priority}~{entry}"


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


def build_tzinfo_from_zone(zone_name: str) -> str:
    if zone_name == "UTC":
        return "GMT0"

    return build_posix_tz_from_zdump(zone_name)


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
    tzinfo_value = None
    scheduled_common: list[str] = []
    scheduled_per_system: dict[str, list[str]] = {}
    current_section = ""
    current_section_priority = 1
    current_section_priority_explicit = False

    for raw_line in lines:
        clean = raw_line.rstrip("\r\n")
        stripped = clean.strip()

        if stripped == "":
            continue

        if stripped.startswith("#"):
            continue

        if stripped.startswith("[") and stripped.endswith("]"):
            current_section, current_section_priority, current_section_priority_explicit = parse_section_header(stripped[1:-1])
            continue

        if current_section == "scheduled":
            scheduled_common.append(
                encode_schedule_priority(stripped, current_section_priority, current_section_priority_explicit)
            )
            continue

        if current_section.startswith("scheduled:"):
            for system_id in parse_scheduled_system_ids(current_section):
                scheduled_per_system.setdefault(system_id, []).append(
                    encode_schedule_priority(stripped, current_section_priority, current_section_priority_explicit)
                )
            continue

        if "=" not in clean:
            continue

        key, value = clean.split("=", 1)
        key = key.strip()
        value = value.strip()

        parsed.append(("kv", key, value))

        if key == "timezone":
            timezone_value = value
        elif key == "tzinfo":
            tzinfo_value = value

    return parsed, timezone_value, tzinfo_value, scheduled_common, scheduled_per_system


def merge_scheduled_entries(common_lines: list[str], system_lines: list[str]) -> list[str]:
    merged = []
    seen = set()

    for line in common_lines + system_lines:
        key = line.casefold()
        if key in seen:
            continue
        seen.add(key)
        merged.append(line)

    return merged


def render_scheduled_entries(lines: list[str], start_index: int = 0) -> list[str]:
    rendered = []

    for offset, line in enumerate(lines):
        rendered.append(f"schedule{start_index + offset}={line}")

    return rendered


def build_rendered_path(system_id: str) -> Path:
    return TEMP_DIR / f"config.rendered.{system_id}"


def rebuild_config_text(system_id: str | None = None) -> bytes:
    lines = load_source_lines()
    parsed, timezone_value, tzinfo_value, scheduled_common, scheduled_per_system = parse_config_lines(lines)

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

        # log(f'timezone "{timezone_value}" resolved to "{resolved_zone}", tzinfo="{generated_tzinfo}"')
    elif tzinfo_value is None or tzinfo_value.strip() == "":
        generated_tzinfo = "GMT0"

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

    sanitized_system_id = sanitize_system_id(system_id or "")
    selected_schedule_lines = merge_scheduled_entries(
        scheduled_common,
        scheduled_per_system.get(sanitized_system_id.lower(), []) if sanitized_system_id != "" else [],
    )
    if selected_schedule_lines:
        output_lines.extend(render_scheduled_entries(selected_schedule_lines))

    text = "\n".join(output_lines)
    if not text.endswith("\n"):
        text += "\n"

    payload = text.encode("utf-8")

    if sanitized_system_id != "":
        TEMP_DIR.mkdir(parents=True, exist_ok=True)
        rendered_path = build_rendered_path(sanitized_system_id)
        rendered_path.write_bytes(payload)

    return payload


class ConfigHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        try:
            query = parse_qs(urlparse(self.path).query)
            system_id = sanitize_system_id(query.get("systemid", [""])[0])
            payload = rebuild_config_text(system_id or None)
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(payload)))
            self.send_header("Cache-Control", "no-store, no-cache, must-revalidate")
            self.end_headers()
            self.wfile.write(payload)
            if system_id != "":
                log(f'served rendered config for systemid="{system_id}"')
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
