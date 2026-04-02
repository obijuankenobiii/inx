#!python3
import freetype
import zlib
import sys
import re
import math
import argparse
import struct
import os
from collections import namedtuple

# Originally from https://github.com/vroland/epdiy

parser = argparse.ArgumentParser(description="Generate a binary font file from a font to be used with epdiy.")
parser.add_argument("name", action="store", help="name of the font.")
parser.add_argument("size", type=int, nargs='?', help="font size to use (optional if --all is used).")
parser.add_argument("fontstack", action="store", nargs='+', help="list of font files, ordered by descending priority.")
parser.add_argument("--2bit", dest="is2Bit", action="store_true", help="generate 2-bit greyscale bitmap instead of 1-bit black and white.")
parser.add_argument("--additional-intervals", dest="additional_intervals", action="append", help="Additional code point intervals to export as min,max. This argument can be repeated.")
parser.add_argument("--output", "-o", dest="output", action="store", help="output binary file path.")
parser.add_argument("--all", dest="generate_all", action="store_true", help="generate all font sizes (8,10,12,14,16,18).")
args = parser.parse_args()

GlyphProps = namedtuple("GlyphProps", ["width", "height", "advance_x", "left", "top", "data_length", "data_offset", "code_point"])

def generate_font(font_name, size, font_stack_paths, is2Bit, intervals, add_ints, output_file=None):
    font_stack = [freetype.Face(f) for f in font_stack_paths]
    
    # inclusive unicode code point intervals
    intervals = intervals if intervals else [
        (0x0000, 0x007F),  # Basic Latin
        (0x0080, 0x00FF),  # Latin-1 Supplement
        (0x0100, 0x017F),  # Latin Extended-A
        (0x2000, 0x206F),  # General Punctuation
        (0x2010, 0x203A),  # dashes, quotes, prime marks
        (0x2040, 0x205F),  # misc punctuation
        (0x20A0, 0x20CF),  # common currency symbols
        (0x0300, 0x036F),  # Combining Diacritical Marks
        (0x0400, 0x04FF),  # Cyrillic
        (0x2070, 0x209F),  # Superscripts and Subscripts
        (0x2200, 0x22FF),  # General math operators
        (0x2190, 0x21FF),  # Arrows
        (0xFFFD, 0xFFFD),  # Replacement Character
    ]
    
    add_ints = add_ints if add_ints else []
    
    def norm_floor(val):
        return int(math.floor(val / (1 << 6)))
    
    def norm_ceil(val):
        return int(math.ceil(val / (1 << 6)))
    
    def chunks(l, n):
        for i in range(0, len(l), n):
            yield l[i:i + n]
    
    def load_glyph(code_point):
        face_index = 0
        while face_index < len(font_stack):
            face = font_stack[face_index]
            glyph_index = face.get_char_index(code_point)
            if glyph_index > 0:
                face.load_glyph(glyph_index, freetype.FT_LOAD_RENDER)
                return face
            face_index += 1
        # Don't print for every missing glyph to avoid spam
        # print(f"code point {code_point} ({hex(code_point)}) not found in font stack!", file=sys.stderr)
        return None
    
    unmerged_intervals = sorted(intervals + add_ints)
    intervals = []
    unvalidated_intervals = []
    for i_start, i_end in unmerged_intervals:
        if len(unvalidated_intervals) > 0 and i_start + 1 <= unvalidated_intervals[-1][1]:
            unvalidated_intervals[-1] = (unvalidated_intervals[-1][0], max(unvalidated_intervals[-1][1], i_end))
            continue
        unvalidated_intervals.append((i_start, i_end))
    
    for i_start, i_end in unvalidated_intervals:
        start = i_start
        for code_point in range(i_start, i_end + 1):
            face = load_glyph(code_point)
            if face is None:
                if start < code_point:
                    intervals.append((start, code_point - 1))
                start = code_point + 1
        if start != i_end + 1:
            intervals.append((start, i_end))
    
    # Skip if no valid intervals
    if not intervals:
        print(f"Warning: No valid glyphs found for size {size}pt", file=sys.stderr)
        return
    
    for face in font_stack:
        face.set_char_size(size << 6, size << 6, 150, 150)
    
    total_size = 0
    all_glyphs = []
    
    for i_start, i_end in intervals:
        for code_point in range(i_start, i_end + 1):
            face = load_glyph(code_point)
            if face is None:
                continue
                
            bitmap = face.glyph.bitmap
            
            # Skip if bitmap is empty
            if bitmap.width == 0 or bitmap.rows == 0:
                continue
    
            # Build out 4-bit greyscale bitmap
            pixels4g = []
            px = 0
            for i, v in enumerate(bitmap.buffer):
                y = i / bitmap.width
                x = i % bitmap.width
                if x % 2 == 0:
                    px = (v >> 4)
                else:
                    px = px | (v & 0xF0)
                    pixels4g.append(px);
                    px = 0
                # eol
                if x == bitmap.width - 1 and bitmap.width % 2 > 0:
                    pixels4g.append(px)
                    px = 0
    
            if is2Bit:
                # 0-3 white, 4-7 light grey, 8-11 dark grey, 12-15 black
                pixels2b = []
                px = 0
                pitch = (bitmap.width // 2) + (bitmap.width % 2)
                for y in range(bitmap.rows):
                    for x in range(bitmap.width):
                        px = px << 2
                        bm = pixels4g[y * pitch + (x // 2)]
                        bm = (bm >> ((x % 2) * 4)) & 0xF
    
                        if bm >= 12:
                            px += 3
                        elif bm >= 8:
                            px += 2
                        elif bm >= 4:
                            px += 1
    
                        if (y * bitmap.width + x) % 4 == 3:
                            pixels2b.append(px)
                            px = 0
                if (bitmap.width * bitmap.rows) % 4 != 0:
                    px = px << (4 - (bitmap.width * bitmap.rows) % 4) * 2
                    pixels2b.append(px)
    
                pixels = pixels2b
            else:
                # Downsample to 1-bit bitmap
                pixelsbw = []
                px = 0
                pitch = (bitmap.width // 2) + (bitmap.width % 2)
                for y in range(bitmap.rows):
                    for x in range(bitmap.width):
                        px = px << 1
                        bm = pixels4g[y * pitch + (x // 2)]
                        px += 1 if ((x & 1) == 0 and bm & 0xE0 > 0) or ((x & 1) == 1 and bm & 0xE > 0) else 0
    
                        if (y * bitmap.width + x) % 8 == 7:
                            pixelsbw.append(px)
                            px = 0
                if (bitmap.width * bitmap.rows) % 8 != 0:
                    px = px << (8 - (bitmap.width * bitmap.rows) % 8)
                    pixelsbw.append(px)
    
                pixels = pixelsbw
    
            packed = bytes(pixels)
            glyph = GlyphProps(
                width = bitmap.width,
                height = bitmap.rows,
                advance_x = norm_floor(face.glyph.advance.x),
                left = face.glyph.bitmap_left,
                top = face.glyph.bitmap_top,
                data_length = len(packed),
                data_offset = total_size,
                code_point = code_point,
            )
            total_size += len(packed)
            all_glyphs.append((glyph, packed))
    
    # Skip if no glyphs found
    if not all_glyphs:
        print(f"Warning: No glyphs generated for size {size}pt", file=sys.stderr)
        return
    
    # Get face metrics (use first valid glyph)
    face = load_glyph(ord('|'))
    if face is None:
        face = load_glyph(ord('A'))
    if face is None:
        # Fallback metrics
        ascender = size
        descender = -size // 4
        line_height = size + size // 4
    else:
        ascender = norm_ceil(face.size.ascender)
        descender = norm_floor(face.size.descender)
        line_height = norm_ceil(face.size.height)
    
    # If output file specified, write binary
    if output_file:
        with open(output_file, 'wb') as f:
            # Write header
            # Magic number "EPDF" (0x45504446)
            f.write(struct.pack('<I', 0x45504446))
            
            # Version (1)
            f.write(struct.pack('<I', 1))
            
            # Font info
            name_bytes = font_name.encode('utf-8')
            f.write(struct.pack('<H', len(name_bytes)))  # name length
            f.write(name_bytes)  # name
            
            # Metrics
            f.write(struct.pack('<h', line_height))  # line_height
            f.write(struct.pack('<h', ascender))     # ascender
            f.write(struct.pack('<h', descender))    # descender
            f.write(struct.pack('<B', 1 if is2Bit else 0))  # is2Bit
            
            # Number of intervals
            f.write(struct.pack('<H', len(intervals)))
            
            # Write intervals
            for i_start, i_end in intervals:
                f.write(struct.pack('<I', i_start))
                f.write(struct.pack('<I', i_end))
                f.write(struct.pack('<I', 0))  # offset placeholder, will be updated later
            
            # Remember position for offsets
            intervals_offset_pos = f.tell() - (len(intervals) * 12) + 8
            
            # Write number of glyphs
            f.write(struct.pack('<I', len(all_glyphs)))
            
            # Write glyph properties
            glyph_offsets = []
            for glyph, _ in all_glyphs:
                glyph_offsets.append(f.tell())
                f.write(struct.pack('<H', glyph.width))
                f.write(struct.pack('<H', glyph.height))
                f.write(struct.pack('<h', glyph.advance_x))
                f.write(struct.pack('<h', glyph.left))
                f.write(struct.pack('<h', glyph.top))
                f.write(struct.pack('<I', glyph.data_length))
                f.write(struct.pack('<I', glyph.data_offset))
                f.write(struct.pack('<I', glyph.code_point))
            
            # Write bitmap data
            for _, packed in all_glyphs:
                f.write(packed)
            
            # Update interval offsets
            offset = 0
            f.seek(intervals_offset_pos)
            for i_start, i_end in intervals:
                f.write(struct.pack('<I', offset))
                offset += (i_end - i_start + 1)
                f.seek(f.tell() + 8)  # Skip i_start and i_end
            
            # Write glyph offsets at the end
            f.write(struct.pack('<I', len(glyph_offsets)))
            for offset_pos in glyph_offsets:
                f.write(struct.pack('<I', offset_pos))
            
            # Get final size
            final_size = f.tell()
        
        print(f"Binary font saved to {output_file}")
        print(f"  Name: {font_name}")
        print(f"  Size: {size}pt")
        print(f"  Mode: {'2-bit' if is2Bit else '1-bit'}")
        print(f"  Glyphs: {len(all_glyphs)}")
        print(f"  Total size: {final_size} bytes")
    else:
        # Output header file (original behavior)
        print(f"""/**
 * generated by fontconvert.py
 * name: {font_name}
 * size: {size}
 * mode: {'2-bit' if is2Bit else '1-bit'}
 * Command used: {' '.join(sys.argv)}
 */
#pragma once
#include "EpdFontData.h"
""")
    
        glyph_data = []
        glyph_props = []
        for index, glyph in enumerate(all_glyphs):
            props, packed = glyph
            glyph_data.extend([b for b in packed])
            glyph_props.append(props)
    
        print(f"static const uint8_t {font_name}Bitmaps[{len(glyph_data)}] = {{")
        for c in chunks(glyph_data, 16):
            print ("    " + " ".join(f"0x{b:02X}," for b in c))
        print ("};\n");
    
        print(f"static const EpdGlyph {font_name}Glyphs[] = {{")
        for i, g in enumerate(glyph_props):
            print ("    { " + ", ".join([f"{a}" for a in list(g[:-1])]),"},", f"// {chr(g.code_point) if g.code_point != 92 else '<backslash>'}")
        print ("};\n");
    
        print(f"static const EpdUnicodeInterval {font_name}Intervals[] = {{")
        offset = 0
        for i_start, i_end in intervals:
            print (f"    {{ 0x{i_start:X}, 0x{i_end:X}, 0x{offset:X} }},")
            offset += i_end - i_start + 1
        print ("};\n");
    
        print(f"static const EpdFontData {font_name} = {{")
        print(f"    {font_name}Bitmaps,")
        print(f"    {font_name}Glyphs,")
        print(f"    {font_name}Intervals,")
        print(f"    {len(intervals)},")
        print(f"    {line_height},")
        print(f"    {ascender},")
        print(f"    {descender},")
        print(f"    {'true' if is2Bit else 'false'},")
        print("};")

# Main execution
if args.generate_all:
    sizes = [8, 10, 12, 14, 16, 18]
    print(f"Generating all font sizes: {sizes}")
    
    for size in sizes:
        print(f"\n{'='*50}")
        print(f"Generating {size}pt font...")
        
        # Generate output filename based on size if --output was specified
        if args.output:
            base, ext = os.path.splitext(args.output)
            output_file = f"{base}_{size}{ext}"
        else:
            output_file = None
        
        generate_font(
            font_name=f"{args.name}_{size}",
            size=size,
            font_stack_paths=args.fontstack,
            is2Bit=args.is2Bit,
            intervals=None,  # Will use default
            add_ints=args.additional_intervals,
            output_file=output_file
        )
    
    print(f"\n{'='*50}")
    print(f"✅ Completed! Generated all {len(sizes)} font sizes: {sizes}")
    
else:
    # Original single-size generation
    if args.size is None:
        print("Error: Please specify a font size or use --all")
        sys.exit(1)
    
    generate_font(
        font_name=args.name,
        size=args.size,
        font_stack_paths=args.fontstack,
        is2Bit=args.is2Bit,
        intervals=None,
        add_ints=args.additional_intervals,
        output_file=args.output
    )