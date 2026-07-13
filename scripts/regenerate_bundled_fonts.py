#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = [
#   "fonttools==4.59.2",
#   "freetype-py==2.5.1",
# ]
# ///

"""Regenerate the bundled Atkinson Hyperlegible and Literata font headers."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path

import freetype


ROOT = Path(__file__).resolve().parents[1]
CONVERTER = ROOT / "fontcon" / "fontconvert.py"
MANIFEST = ROOT / "fontcon" / "bundled-font-sources.json"
OUTPUT_DIR = ROOT / "src" / "system" / "font"
SIZES = (10, 12, 14, 16, 18)
STYLES = ("Regular", "Bold", "Italic", "BoldItalic")
SLUGS = {"Regular": "regular", "Bold": "bold", "Italic": "italic", "BoldItalic": "bolditalic"}
FREETYPE_VERSION = (2, 13, 2)


def fetch(url: str, expected_sha256: str, destination: Path) -> None:
    with urllib.request.urlopen(url) as response:
        data = response.read()
    actual = hashlib.sha256(data).hexdigest()
    if actual != expected_sha256:
        raise RuntimeError(f"checksum mismatch for {url}: expected {expected_sha256}, got {actual}")
    destination.write_bytes(data)


def source_url(repository: str, revision: str, path: str) -> str:
    owner_repo = repository.removeprefix("https://github.com/").rstrip("/")
    return f"https://raw.githubusercontent.com/{owner_repo}/{revision}/{path}"


def generate(name: str, size: int, faces: list[Path], two_bit: bool) -> bytes:
    command = [
        sys.executable,
        str(CONVERTER),
        name,
        str(size),
        *(str(face) for face in faces),
    ]
    if two_bit:
        command.append("--2bit")
    command.extend(("--reader-latin", "--legacy-header"))
    result = subprocess.run(command, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    generated = result.stdout.decode("utf-8")
    canonical = f"scripts/regenerate_bundled_fonts.py ({name})"
    lines = generated.splitlines(keepends=True)
    lines = [f" * Command used: {canonical}\n" if line.startswith(" * Command used:") else line for line in lines]
    return "".join(lines).encode("utf-8")


def write_or_check(path: Path, content: bytes, check: bool) -> bool:
    current = path.read_bytes() if path.exists() else None
    if current == content:
        return False
    if check:
        print(f"out of date: {path.relative_to(ROOT)}", file=sys.stderr)
    else:
        path.write_bytes(content)
        print(f"wrote {path.relative_to(ROOT)}")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail if regeneration would change a header")
    args = parser.parse_args()
    actual_freetype = freetype.version()
    if actual_freetype != FREETYPE_VERSION:
        expected = ".".join(map(str, FREETYPE_VERSION))
        actual = ".".join(map(str, actual_freetype))
        raise RuntimeError(f"FreeType {expected} is required for reproducible rasterization; found {actual}")
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    changed = False

    with tempfile.TemporaryDirectory(prefix="inx-fonts-") as temporary:
        source_dir = Path(temporary)
        sources: dict[tuple[str, str], Path] = {}
        for family, source in manifest.items():
            for style, (relative_path, checksum) in source["files"].items():
                destination = source_dir / f"{family}-{style}.ttf"
                fetch(source_url(source["repository"], source["revision"], relative_path), checksum, destination)
                sources[(family, style)] = destination

        # The UI's compact 8 pt face remains 1-bit. Reader faces are 1-bit at
        # 10 pt and 2-bit at 12 pt and above, matching the existing headers.
        name = "atkinson_hyperlegible_8_regular"
        content = generate(name, 8, [sources[("atkinson_hyperlegible", "Regular")], sources[("literata", "Regular")]], False)
        changed |= write_or_check(OUTPUT_DIR / f"{name}.h", content, args.check)

        for size in SIZES:
            for style in STYLES:
                slug = SLUGS[style]
                atkinson_name = f"atkinson_hyperlegible_{size}_{slug}"
                atkinson_faces = [sources[("atkinson_hyperlegible", style)], sources[("literata", style)]]
                content = generate(atkinson_name, size, atkinson_faces, size >= 12)
                changed |= write_or_check(OUTPUT_DIR / f"{atkinson_name}.h", content, args.check)

                literata_name = f"literata_{size}_{slug}"
                content = generate(literata_name, size, [sources[("literata", style)]], True)
                changed |= write_or_check(OUTPUT_DIR / f"{literata_name}.h", content, args.check)

    return 1 if args.check and changed else 0


if __name__ == "__main__":
    raise SystemExit(main())
