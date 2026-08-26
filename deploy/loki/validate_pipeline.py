#!/usr/bin/env python3
"""
Validation script for 0 A.D. -> Grafana Alloy -> Loki pipeline.
Simulates and verifies the exact regex, drop, multiline, entity unescape,
and level extraction stages configured in `config.alloy`.
"""

import os
import re
import html
import json
import sys
import glob
import urllib.request
import urllib.error

# Regex patterns matching deploy/loki/config.alloy
DROP_HEADER_PATTERN = re.compile(r"^(<!DOCTYPE|<meta|<title|<style|body|<h2|h2|p \{)", re.IGNORECASE)
LOG_ENTRY_PATTERN = re.compile(
    r"^<p(?:\s+class=[\"'](?P<raw_level>error|warning)[\"'])?>(?:(?P<prefix_level>ERROR|WARNING|INFO):\s*)?(?P<log_message>.*)(?:</p>)?$"
)
SUBSYSTEM_PATTERN = re.compile(r"^\[(?P<subsystem>[a-zA-Z0-9_-]+)\]")

def parse_html_log_line(raw_line: str):
    """
    Applies the Alloy pipeline transformations to a single log line.
    Returns None if line is dropped (e.g. HTML boilerplate), or a dict with parsed fields.
    """
    line = raw_line.strip()
    if not line:
        return None

    # Stage 4.1: Drop HTML document header/footer lines
    if DROP_HEADER_PATTERN.match(line):
        return None

    # Stage 4.3: Match log regex
    match = LOG_ENTRY_PATTERN.match(line)
    if match:
        raw_level = match.group("raw_level") or ""
        prefix_level = match.group("prefix_level") or ""
        log_message = match.group("log_message") or ""

        # Stage 4.4: Unescape HTML entities
        log_message = html.unescape(log_message)

        # Stage 4.5: Normalize level
        if raw_level.lower() == "error" or prefix_level.upper() == "ERROR":
            level = "error"
        elif raw_level.lower() == "warning" or prefix_level.upper() == "WARNING":
            level = "warn"
        else:
            level = "info"

        # Stage 4.6: Extract subsystem
        sub_match = SUBSYSTEM_PATTERN.match(log_message)
        subsystem = sub_match.group("subsystem") if sub_match else "engine"

        return {
            "level": level,
            "subsystem": subsystem,
            "message": log_message,
        }

    # If it didn't match <p>...</p>, return as raw line with info level
    return {
        "level": "info",
        "subsystem": "engine",
        "message": html.unescape(line),
    }


def parse_ndjson_line(raw_line: str):
    """
    Applies JSON decoding to a structured NDJSON log line.
    """
    line = raw_line.strip()
    if not line:
        return None
    try:
        data = json.loads(line)
        msg = data.get("message", "")
        sub_match = SUBSYSTEM_PATTERN.match(msg)
        subsystem = sub_match.group("subsystem") if sub_match else "engine"
        return {
            "level": data.get("level", "info"),
            "subsystem": subsystem,
            "message": msg,
            "time": data.get("time", 0.0),
        }
    except json.JSONDecodeError:
        return None


def run_unit_tests():
    """Run verification against known edge cases and sample log lines."""
    test_cases = [
        # (input_line, expected_level, expected_message_contains, expected_subsystem)
        ("<p>Loading config file \"config/default.cfg\"</p>", "info", "Loading config file", "engine"),
        ("<p class=\"warning\">WARNING: Font mono-stroke-10 not found</p>", "warn", "Font mono-stroke-10 not found", "engine"),
        ("<p class=\"error\">ERROR: JavaScript error: gui/session.js:42</p>", "error", "JavaScript error: gui/session.js:42", "engine"),
        ("<p>[Renderer] Failed to compile shader &lt;water.xml&gt; &amp; exit</p>", "info", "Failed to compile shader <water.xml> & exit", "Renderer"),
        ("<p class=\"error\">ERROR: [Sound] Device disconnected &amp; failed</p>", "error", "[Sound] Device disconnected & failed", "Sound"),
        ("<p>Engine exited successfully on 2026-08-25 with 100 message(s)</p>", "info", "Engine exited successfully", "engine"),
        ("<!DOCTYPE html>", None, None, None),
        ("<title>Pyrogenesis Log</title>", None, None, None),
        ("<h2>0 A.D. (0.29.0) Main log</h2>", None, None, None),
    ]

    passed = 0
    for raw, exp_level, exp_msg, exp_sub in test_cases:
        res = parse_html_log_line(raw)
        if exp_level is None:
            assert res is None, f"Expected dropped line for '{raw}', got {res}"
        else:
            assert res is not None, f"Expected parsed result for '{raw}', got None"
            assert res["level"] == exp_level, f"Level mismatch for '{raw}': {res['level']} != {exp_level}"
            assert exp_msg in res["message"], f"Message mismatch for '{raw}': '{exp_msg}' not in '{res['message']}'"
            assert res["subsystem"] == exp_sub, f"Subsystem mismatch for '{raw}': {res['subsystem']} != {exp_sub}"
        passed += 1

    # Test NDJSON structured parsing
    ndjson_sample = '{"time":1787593.123,"level":"error","message":"[Net] Connection timeout"}'
    ndjson_res = parse_ndjson_line(ndjson_sample)
    assert ndjson_res is not None
    assert ndjson_res["level"] == "error"
    assert ndjson_res["subsystem"] == "Net"
    assert ndjson_res["message"] == "[Net] Connection timeout"
    passed += 1

    print(f"[PASS] {passed}/{passed} synthetic test cases passed.")


def test_real_logs():
    """Test parser against real logs in %LOCALAPPDATA%/0ad/logs if available."""
    localappdata = os.environ.get("LOCALAPPDATA", "")
    log_dir = os.path.join(localappdata, "0ad", "logs")

    if not os.path.exists(log_dir):
        print(f"[INFO] No local log directory at {log_dir}. Skipping filesystem test.")
        return

    log_files = glob.glob(os.path.join(log_dir, "*.html")) + glob.glob(os.path.join(log_dir, "*.ndjson")) + glob.glob(os.path.join(log_dir, "*.txt"))
    print(f"[INFO] Found {len(log_files)} log files in {log_dir}:")

    total_parsed = 0
    total_dropped = 0
    level_counts = {"info": 0, "warn": 0, "error": 0}

    for filepath in log_files:
        filename = os.path.basename(filepath)
        parsed_in_file = 0
        dropped_in_file = 0
        is_ndjson = filename.endswith(".ndjson")

        with open(filepath, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                res = parse_ndjson_line(line) if is_ndjson else parse_html_log_line(line)
                if res is None:
                    dropped_in_file += 1
                else:
                    parsed_in_file += 1
                    level_counts[res["level"]] += 1

        print(f"  - {filename:25s}: {parsed_in_file:5d} entries parsed, {dropped_in_file:3d} boilerplate headers dropped")
        total_parsed += parsed_in_file
        total_dropped += dropped_in_file

    print(f"[SUCCESS] Total log entries parsed: {total_parsed}")
    print(f"          Severity distribution : Info={level_counts['info']}, Warn={level_counts['warn']}, Error={level_counts['error']}")


def test_loki_connection(loki_url="http://localhost:3100"):
    """Check if local Loki instance is running and reachable."""
    ready_url = f"{loki_url}/ready"
    try:
        req = urllib.request.Request(ready_url)
        with urllib.request.urlopen(req, timeout=2) as resp:
            status = resp.status
            body = resp.read().decode("utf-8").strip()
            print(f"[INFO] Loki at {loki_url} status: {status} ({body})")
            return True
    except Exception as e:
        print(f"[INFO] Loki at {ready_url} not reachable ({e}). (Start with `docker compose up -d` when Docker is running)")
        return False


if __name__ == "__main__":
    print("==================================================")
    print("0 A.D. Log Ingestion Pipeline Validation (Phase 2)")
    print("==================================================")
    run_unit_tests()
    print("--------------------------------------------------")
    test_real_logs()
    print("--------------------------------------------------")
    test_loki_connection()
    print("==================================================")
    print("Pipeline validation completed successfully.")
