#!python3
import freetype
import math
import argparse
import struct
import sys
import os
from collections import namedtuple

# 24-byte Glyph Structure: 10 (metrics) + 8 (offsets) + 4 (cp) + 2 (padding) = 24
GlyphProps = namedtuple("GlyphProps", ["width", "height", "advance_x", "left", "top", "data_length", "data_offset", "code_point"])

def generate_2bit_bin(style_name, size, font_paths, output_path):
    try:
        font_stack = [freetype.Face(f) for f in font_paths]
    except Exception as e:
        print(f"Error loading fonts: {e}")
        return

    for face in font_stack:
        face.set_char_size(size << 6, size << 6, 150, 150)
    
    # Standard Unicode intervals for E-Reader
    intervals = [
        (0x0000, 0x007F), (0x0080, 0x00FF), (0x0100, 0x017F),
        (0x2000, 0x206F), (0x2010, 0x203A), (0x2040, 0x205F),
        (0x20A0, 0x20CF), (0x0300, 0x036F), (0x0400, 0x04FF),
        (0x2200, 0x22FF), (0xFFFD, 0xFFFD)
    ]

    def load_glyph(cp):
        for face in font_stack:
            idx = face.get_char_index(cp)
            if idx > 0:
                face.load_glyph(idx, freetype.FT_LOAD_RENDER)
                return face
        return None

    total_bits_size = 0
    all_glyphs = []
    active_intervals = []

    for i_start, i_end in intervals:
        start_cp = -1
        for cp in range(i_start, i_end + 1):
            face = load_glyph(cp)
            if not face:
                if start_cp != -1:
                    active_intervals.append((start_cp, cp - 1))
                    start_cp = -1
                continue
            
            if start_cp == -1: start_cp = cp
            
            bitmap = face.glyph.bitmap
            pixels = []
            px = 0
            count = 0
            
            # 8-bit to 2-bit Conversion (0-3 scale)
            for v in bitmap.buffer:
                val = 0
                if v >= 192: val = 3
                elif v >= 128: val = 2
                elif v >= 64: val = 1
                
                px = (px << 2) | val
                count += 1
                if count == 4:
                    pixels.append(px)
                    px = 0; count = 0
            
            if count > 0:
                px <<= (4 - count) * 2
                pixels.append(px)

            packed = bytes(pixels)
            glyph = GlyphProps(
                bitmap.width, bitmap.rows, int(face.glyph.advance.x >> 6),
                face.glyph.bitmap_left, face.glyph.bitmap_top, 
                len(packed), total_bits_size, cp
            )
            all_glyphs.append((glyph, packed))
            total_bits_size += len(packed)
            
        if start_cp != -1:
            active_intervals.append((start_cp, i_end))

    ref_face = load_glyph(ord('|')) or load_glyph(ord('A'))
    line_h = int(ref_face.size.height >> 6)
    ascender = int(ref_face.size.ascender >> 6)
    descender = int(ref_face.size.descender >> 6)

    with open(output_path, "wb") as f:
        # Header (8 bytes)
        f.write(struct.pack("<II", 0x45504446, 1))
        
        # Style Name
        name_bs = style_name.encode('utf-8')
        f.write(struct.pack("<H", len(name_bs)))
        f.write(name_bs)
        
        # Metrics (7 bytes)
        f.write(struct.pack("<hhhB", line_h, ascender, descender, 1))
        
        # Intervals
        f.write(struct.pack("<H", len(active_intervals)))
        offset_acc = 0
        for s, e in active_intervals:
            f.write(struct.pack("<III", s, e, offset_acc))
            offset_acc += (e - s + 1)
            
        # Glyphs (4 + N*24 bytes)
        f.write(struct.pack("<I", len(all_glyphs)))
        for g, _ in all_glyphs:
            # width(H), height(H), advX(h), left(h), top(h), len(I), off(I), cp(I), pad(H)
            f.write(struct.pack("<HHhhhII I H", 
                g.width, g.height, g.advance_x, g.left, g.top, 
                g.data_length, g.data_offset, g.code_point, 0))
        
        # Raw Bitmaps
        for _, data in all_glyphs:
            f.write(data)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("style", help="Style name (e.g. Regular)")
    parser.add_argument("fontstack", nargs='+', help="Font file paths")
    args = parser.parse_args()

    # Generates exactly 10, 12, 14, 16, 18
    for s in range(10, 19, 2):
        out_file = f"{args.style}_{s}.bin"
        generate_2bit_bin(args.style, s, args.fontstack, out_file)
        print(f"Generated: {out_file}")