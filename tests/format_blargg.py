#!/usr/bin/env python3
"""
format_blargg.py — Convert run_blargg.sh output to pass_list/blargg.md.

Usage:
    bash tests/run_blargg.sh | python tests/format_blargg.py [--output FILE]

--output defaults to tests/pass_list/blargg.md
"""
import argparse
import re
import sys

ICONS = {"PASS": "✅", "FAIL": "❌", "SKIP": "skip"}

ANSI_RE = re.compile(r'\x1b\[[0-9;]*m')

def strip_ansi(s: str) -> str:
    return ANSI_RE.sub('', s)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="tests/pass_list/blargg.md")
    args = parser.parse_args()

    results: dict[str, str] = {}  # label -> icon (insertion order = run order)

    for raw in sys.stdin:
        line = strip_ansi(raw).rstrip()

        # Skip lines:  "  SKIP  <label>  (not found)"
        m = re.match(r'^\s+SKIP\s+(.+?)\s+\(', line)
        if m:
            label = m.group(1).strip()
            if label not in results:
                results[label] = ICONS["SKIP"]
            continue

        # Pass/fail lines:  "  <label>  PASS" or "  <label>  FAIL"
        m = re.match(r'^\s{2}(.+?)\s{2,}(PASS|FAIL)\s*$', line)
        if m:
            label = m.group(1).strip()
            status = m.group(2)
            if label not in results:
                results[label] = ICONS[status]

    passed  = sum(1 for v in results.values() if v == "✅")
    failed  = sum(1 for v in results.values() if v == "❌")
    skipped = sum(1 for v in results.values() if v == "skip")

    lines = [
        "# Blargg Test Results",
        f"Results: {passed} passed, {failed} failed, {skipped} skipped",
        "| Test Name | Pass/Fail (✅/❌) |",
        "|-----------|------|",
    ]
    for name, icon in results.items():
        lines.append(f"| {name} | {icon} |")
    lines.append("")  # trailing newline

    with open(args.output, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))

    print(f"Wrote {len(results)} tests to {args.output}  ({passed} pass, {failed} fail, {skipped} skip)")

if __name__ == "__main__":
    main()
