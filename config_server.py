#!/usr/bin/env python3
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from datetime import datetime
import re

from gen_tzinfo import build_posix_tz_from_zdump

SOURCE_FILE = Path("data/config.txt")
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

    for raw_line in lines:
        clean = raw_line.rstrip("\r\n")

        if clean.strip() == "":
            continue

        if clean.lstrip().startswith("#"):
            continue

        if "=" not in clean:
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

        # log(f'timezone "{timezone_value}" resolved to "{resolved_zone}", tzinfo="{generated_tzinfo}"')

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
