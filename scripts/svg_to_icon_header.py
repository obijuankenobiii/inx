#!/usr/bin/env python3
"""Rasterize SVG icons into 1-bpp C headers matching BitmapRender::icon packing.

Format: row-major, MSB-first, bit 1 = paper (white), bit 0 = ink (black).
"""

from __future__ import annotations

import argparse
import io
from pathlib import Path

import cairosvg
from PIL import Image


def svg_to_bits(svg_path: Path, size: int, threshold: int = 160) -> list[int]:
    png = cairosvg.svg2png(
        url=str(svg_path),
        output_width=size,
        output_height=size,
        background_color="white",
    )
    img = Image.open(io.BytesIO(png)).convert("RGBA")
    # Composite onto white so translucent antialias becomes gray.
    bg = Image.new("RGBA", img.size, (255, 255, 255, 255))
    composed = Image.alpha_composite(bg, img).convert("L")

    bits: list[int] = []
    for y in range(size):
        for x in range(size):
            # Dark pixels are ink (0); light pixels are paper (1).
            bits.append(1 if composed.getpixel((x, y)) >= threshold else 0)
    return bits


def pack_msb(bits: list[int], width: int) -> list[int]:
    stride = (width + 7) // 8
    height = len(bits) // width
    out: list[int] = []
    for y in range(height):
        for byte_i in range(stride):
            value = 0
            for bit in range(8):
                x = byte_i * 8 + bit
                if x < width and bits[y * width + x]:
                    value |= 0x80 >> bit
            out.append(value)
    return out


def write_header(path: Path, symbol: str, bytes_out: list[int]) -> None:
    lines = [
        "#pragma once",
        "#include <cstdint>",
        "",
        f"static const uint8_t {symbol}[] = {{",
    ]
    row: list[str] = []
    for i, b in enumerate(bytes_out):
        row.append(f"0x{b:02x}")
        if len(row) == 12 or i == len(bytes_out) - 1:
            suffix = "," if i != len(bytes_out) - 1 else ""
            lines.append("    " + ", ".join(row) + suffix)
            row = []
    lines.append("};")
    lines.append("")
    path.write_text("\n".join(lines) + "\n")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("svg", type=Path)
    parser.add_argument("out", type=Path)
    parser.add_argument("symbol")
    parser.add_argument("--size", type=int, required=True)
    parser.add_argument("--threshold", type=int, default=160)
    args = parser.parse_args()

    bits = svg_to_bits(args.svg, args.size, args.threshold)
    packed = pack_msb(bits, args.size)
    write_header(args.out, args.symbol, packed)
    print(f"Wrote {args.out} ({args.symbol}, {args.size}x{args.size}, {len(packed)} bytes)")


if __name__ == "__main__":
    main()
