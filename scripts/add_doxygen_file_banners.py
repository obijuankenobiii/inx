#!/usr/bin/env python3
"""Prepend a minimal Doxygen @file banner when a translation unit lacks one."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


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


def has_file_tag(text: str) -> bool:
    head = text[:4000]
    return bool(re.search(r"@file\b", head))


def banner_for(path: Path) -> str:
    name = path.name
    stem = path.stem
    if path.name == "main.cpp":
        brief = "Firmware entry point, globals, and activity bootstrap."
    elif path.suffix == ".h":
        brief = f"Public interface and types for {stem}."
    else:
        brief = f"Definitions for {stem}."
    return (
        "/**\n"
        f" * @file {name}\n"
        f" * @brief {brief}\n"
        " */\n\n"
    )


def insert_banner(text: str, path: Path) -> str:
    banner = banner_for(path)
    if text.startswith("#pragma once"):
        lines = text.splitlines(keepends=True)
        if not lines:
            return banner + text
        out = [lines[0]]
        if len(lines) > 1 and lines[1].strip() == "":
            out.append(lines[1])
            rest = "".join(lines[2:])
        else:
            out.append("\n")
            rest = "".join(lines[1:])
        return "".join(out) + banner + rest
    return banner + text


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
            if not path.is_file() or not should_process(path):
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            if has_file_tag(text):
                continue
            new_text = insert_banner(text, path)
            if new_text != text:
                changed += 1
                if not args.dry_run:
                    path.write_text(new_text, encoding="utf-8")
    print(f"{'Would update' if args.dry_run else 'Updated'} {changed} files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
