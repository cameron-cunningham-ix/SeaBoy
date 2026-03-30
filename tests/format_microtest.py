#!/usr/bin/env python3
"""
format_microtest.py - Convert run_microtest.sh output to pass_list/microtest.md.

Usage:
    bash tests/run_microtest.sh | python tests/format_microtest.py [--output FILE]

--output defaults to tests/pass_list/microtest.md
"""
import argparse
import re
import sys

ICONS = {"PASS": "✅", "FAIL": "❌", "TIMEOUT": "⏱️", "SKIP": "skip"}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="tests/pass_list/microtest.md")
    args = parser.parse_args()

    results: dict[str, str] = {}  # name -> icon
    details: dict[str, str] = {}  # name -> extra info (actual/expected)

    for line in sys.stdin:
        line = line.rstrip()
        m = re.match(r'^(PASS|FAIL|TIMEOUT|SKIP)\s+(\S+)', line)
        if m:
            status, name = m.group(1), m.group(2)
            results[name] = ICONS[status]
            extra = re.search(r'\[.*?\]', line)
            if extra:
                details[name] = extra.group(0)

    passed  = sum(1 for v in results.values() if v == "✅")
    failed  = sum(1 for v in results.values() if v == "❌")
    timeout = sum(1 for v in results.values() if v == "⏱️")
    skipped = sum(1 for v in results.values() if v == "skip")

    lines = [
        "# gbmicrotest Results",
        f"Results: {passed} passed, {failed} failed, {timeout} timeout, {skipped} skipped",
        "| Test Name | Pass/Fail (✅/❌) | Details |",
        "|-----------|------|---------|",
    ]
    for name in sorted(results):
        detail = details.get(name, "")
        lines.append(f"| {name} | {results[name]} | {detail} |")
    lines.append("")  # trailing newline

    with open(args.output, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))

    print(f"Wrote {len(results)} tests to {args.output}  ({passed} pass, {failed} fail, {timeout} timeout, {skipped} skip)")

if __name__ == "__main__":
    main()
