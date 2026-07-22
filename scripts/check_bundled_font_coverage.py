#!/usr/bin/env python3
"""Validate generated bundled-font intervals and required Cyrillic coverage."""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


FONT_HEADER_RE = re.compile(r"^(?:atkinson_hyperlegible|literata)_\d+_(?:regular|bold|italic|bolditalic)\.h$")
INTERVAL_TABLE_RE = re.compile(
    r"static const EpdUnicodeInterval \w+Intervals\[\] = \{(?P<body>.*?)\n\};", re.DOTALL
)
INTERVAL_RE = re.compile(r"\{\s*(0x[0-9A-Fa-f]+|\d+)\s*,\s*(0x[0-9A-Fa-f]+|\d+)\s*,\s*(0x[0-9A-Fa-f]+|\d+)\s*\}")
GLYPH_TABLE_RE = re.compile(r"static const EpdGlyph \w+Glyphs\[\] = \{(?P<body>.*?)\n\};", re.DOTALL)
GLYPH_RE = re.compile(r"^\s*\{[^{}]+\}\s*,", re.MULTILINE)
FONT_DATA_RE = re.compile(
    r"static const EpdFontData \w+ = \{\s*"
    r"\w+Bitmaps\s*,\s*\w+Glyphs\s*,\s*\w+Intervals\s*,\s*(?P<count>\d+)\s*,",
    re.DOTALL,
)

# Russian alphabet edges plus the commonly omitted Yo variants.
REQUIRED_CODEPOINTS = (0x0410, 0x042F, 0x0430, 0x044F, 0x0401, 0x0451)


@dataclass(frozen=True)
class Interval:
    first: int
    last: int
    offset: int


def parse_int(value: str) -> int:
    return int(value, 0)


def parse_header(path: Path) -> tuple[list[Interval], int, int]:
    text = path.read_text(encoding="utf-8")
    table_match = INTERVAL_TABLE_RE.search(text)
    glyph_match = GLYPH_TABLE_RE.search(text)
    data_match = FONT_DATA_RE.search(text)
    if table_match is None or glyph_match is None or data_match is None:
        raise ValueError("could not find generated glyph table, interval table, or EpdFontData initializer")

    intervals = [
        Interval(*(parse_int(value) for value in match.groups()))
        for match in INTERVAL_RE.finditer(table_match.group("body"))
    ]
    if not intervals:
        raise ValueError("generated interval table is empty")
    glyph_count = len(GLYPH_RE.findall(glyph_match.group("body")))
    if glyph_count == 0:
        raise ValueError("generated glyph table is empty")
    return intervals, int(data_match.group("count")), glyph_count


def contains(intervals: list[Interval], codepoint: int) -> bool:
    """Mirror EpdFont::getGlyph's binary search over generated intervals."""
    left = 0
    right = len(intervals) - 1
    while left <= right:
        mid = left + (right - left) // 2
        interval = intervals[mid]
        if codepoint < interval.first:
            right = mid - 1
        elif codepoint > interval.last:
            left = mid + 1
        else:
            return True
    return False


def validate_header(path: Path) -> list[str]:
    try:
        intervals, declared_count, glyph_count = parse_header(path)
    except (OSError, UnicodeError, ValueError) as error:
        return [str(error)]

    errors: list[str] = []
    if declared_count != len(intervals):
        errors.append(f"initializer declares {declared_count} intervals but table has {len(intervals)}")

    expected_offset = 0
    previous_last = -1
    for index, interval in enumerate(intervals):
        if interval.first > interval.last:
            errors.append(f"interval {index} has first greater than last")
        if interval.first <= previous_last:
            errors.append(f"interval {index} is unsorted or overlaps its predecessor")
        if interval.offset != expected_offset:
            errors.append(f"interval {index} offset is {interval.offset:#x}, expected {expected_offset:#x}")
        expected_offset = interval.offset + interval.last - interval.first + 1
        previous_last = interval.last

    if expected_offset != glyph_count:
        errors.append(f"intervals address {expected_offset} glyphs but glyph table has {glyph_count}")

    missing = [f"U+{codepoint:04X}" for codepoint in REQUIRED_CODEPOINTS if not contains(intervals, codepoint)]
    if missing:
        errors.append("missing required Cyrillic glyphs: " + ", ".join(missing))
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--font-dir",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "src/system/font",
        help="directory containing generated bundled-font headers",
    )
    args = parser.parse_args()

    headers = sorted(path for path in args.font_dir.glob("*.h") if FONT_HEADER_RE.fullmatch(path.name))
    if not headers:
        print(f"error: no bundled Atkinson or Literata font headers found in {args.font_dir}", file=sys.stderr)
        return 2

    failures = 0
    for header in headers:
        errors = validate_header(header)
        if errors:
            failures += 1
            print(f"FAIL {header.name}")
            for error in errors:
                print(f"  {error}")

    if failures:
        print(f"Checked {len(headers)} bundled font headers: {failures} failed.", file=sys.stderr)
        return 1

    codepoints = ", ".join(f"U+{codepoint:04X}" for codepoint in REQUIRED_CODEPOINTS)
    print(f"Checked {len(headers)} bundled font headers; required Cyrillic coverage present ({codepoints}).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
