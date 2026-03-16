#!/usr/bin/env python3
"""
format_samesuite.py — Convert run_samesuite.sh output to pass_list/samesuite.md.

Usage:
    bash tests/run_samesuite.sh | python tests/format_samesuite.py [--output FILE]

--output defaults to tests/pass_list/samesuite.md
"""
import argparse
import re
import sys

ICONS = {"PASS": "✅", "FAIL": "❌", "SKIP": "skip"}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="tests/pass_list/samesuite.md")
    args = parser.parse_args()

    results: dict[str, str] = {}

    for line in sys.stdin:
        line = line.rstrip()
        m = re.match(r'^(PASS|FAIL|SKIP)\s+(\S+)', line)
        if m:
            status, rel = m.group(1), m.group(2)
            results[rel] = ICONS[status]

    passed = sum(1 for v in results.values() if v == "✅")
    failed = sum(1 for v in results.values() if v == "❌")
    skipped = sum(1 for v in results.values() if v == "skip")

    lines = [
        "# SameSuite Results",
        f"Results: {passed} passed, {failed} failed, {skipped} skipped",
        "| Test Name | Pass/Fail (✅/❌) |",
        "|-----------|------|",
    ]
    for name in sorted(results):
        lines.append(f"| {name} | {results[name]} |")
    lines.append("")  # trailing newline

    with open(args.output, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))

    print(f"Wrote {len(results)} tests to {args.output}  ({passed} pass, {failed} fail, {skipped} skip)")

if __name__ == "__main__":
    main()
