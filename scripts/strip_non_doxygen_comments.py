#!/usr/bin/env python3
"""
Remove C/C++ line and block comments while preserving Doxygen blocks and lines.

Preserves:
  - /** ... */ and /*! ... */ blocks (including interior text)
  - // lines where the line is only whitespace + /// or //! (Doxygen line comments)
  - ///< trailing Doxygen on a line (keeps the line, only strips non-Doxygen //... if any)

Does not preserve plain /* ... */ or // non-Doxygen comments.
Handles strings, character literals, and C++11 raw strings R"delim(...)delim".
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


def strip_non_doxygen(source: str) -> str:
    n = len(source)
    out: list[str] = []
    i = 0

    def peek(k: int = 0) -> str:
        j = i + k
        return source[j] if j < n else ""

    while i < n:
        c = source[i]

        # Raw string literal R"delim( ... )delim"
        if c == "R" and peek(1) == '"' and i + 2 < n:
            j = i + 2
            delim = ""
            while j < n and source[j] != "(":
                delim += source[j]
                j += 1
            if j >= n or source[j] != "(":
                out.append(c)
                i += 1
                continue
            j += 1  # skip (
            end_marker = ")" + delim + '"'
            end_idx = source.find(end_marker, j)
            if end_idx == -1:
                out.append(source[i:])
                break
            out.append(source[i : end_idx + len(end_marker)])
            i = end_idx + len(end_marker)
            continue

        # String literal
        if c == '"':
            out.append('"')
            i += 1
            while i < n:
                ch = source[i]
                out.append(ch)
                if ch == "\\" and i + 1 < n:
                    out.append(source[i + 1])
                    i += 2
                    continue
                if ch == '"':
                    i += 1
                    break
                i += 1
            continue

        # Character literal (best-effort; ignore raw string edge cases already handled)
        if c == "'":
            out.append("'")
            i += 1
            while i < n:
                ch = source[i]
                out.append(ch)
                if ch == "\\" and i + 1 < n:
                    out.append(source[i + 1])
                    i += 2
                    continue
                if ch == "'":
                    i += 1
                    break
                i += 1
            continue

        # Block comment
        if c == "/" and peek(1) == "*":
            if i + 2 < n and (source[i + 2] == "*" or source[i + 2] == "!"):
                # Doxygen /** or /*!
                start = i
                i += 2
                while i + 1 < n and not (source[i] == "*" and source[i + 1] == "/"):
                    i += 1
                if i + 1 < n:
                    i += 2
                out.append(source[start:i])
                continue
            # Plain block comment: skip
            i += 2
            depth = 1
            while i + 1 < n and depth > 0:
                if source[i] == "/" and source[i + 1] == "*":
                    depth += 1
                    i += 2
                    continue
                if source[i] == "*" and source[i + 1] == "/":
                    depth -= 1
                    i += 2
                    continue
                i += 1
            continue

        # Line comment
        if c == "/" and peek(1) == "/":
            line_start = source.rfind("\n", 0, i)
            line_start = 0 if line_start == -1 else line_start + 1
            prefix = source[line_start:i]
            if prefix.strip() == "" and i + 2 < n:
                third = source[i + 2]
                if third == "/" or third == "!":
                    # /// or //! whole-line Doxygen: keep through newline
                    start = i
                    while i < n and source[i] != "\n":
                        i += 1
                    if i < n and source[i] == "\n":
                        i += 1
                    out.append(source[start:i])
                    continue
            # ///< trailing Doxygen: keep from ///< to EOL
            if i + 3 < n and source[i : i + 4] == "///<":
                start = i
                while i < n and source[i] != "\n":
                    i += 1
                if i < n and source[i] == "\n":
                    i += 1
                out.append(source[start:i])
                continue
            # Strip non-Doxygen line comment
            while i < n and source[i] != "\n":
                i += 1
            continue

        out.append(c)
        i += 1

    return "".join(out)


def should_process(path: Path) -> bool:
    parts = set(path.parts)
    if "font" in parts or "images" in parts:
        return False
    if "expat" in parts or "picojpeg" in parts or "builtinFonts" in parts:
        return False
    if path.name.endswith(".generated.h"):
        return False
    if "hyph-" in path.name and path.suffix == ".h" and "generated" in parts:
        return False
    return path.suffix in (".cpp", ".h", ".hpp")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("roots", nargs="+", type=Path)
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    changed = 0
    for root in args.roots:
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if not path.is_file():
                continue
            if not should_process(path):
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            new_text = strip_non_doxygen(text)
            if new_text != text:
                changed += 1
                if not args.dry_run:
                    path.write_text(new_text, encoding="utf-8")
    print(f"{'Would change' if args.dry_run else 'Changed'} {changed} files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
