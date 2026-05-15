#!/usr/bin/env python3
"""Build TinyPanel's external 16x16 Unicode bitmap font.

Input:  BDF font with Unicode ENCODING values.
Output: .uf16 file loaded by lib/PixelAssets/UnicodeFont16.cpp.
"""

from __future__ import annotations

import argparse
import struct
from dataclasses import dataclass
from pathlib import Path


MAGIC = b"TPU16\0"
VERSION = 1
WIDTH = 16
HEIGHT = 16
GLYPH_BYTES = 32
HEADER_SIZE = 24


RANGES = (
    (0x00A0, 0x00FF),  # Latin-1 symbols used by feeds and APIs
    (0x2000, 0x206F),  # general punctuation
    (0x2190, 0x21FF),  # arrows
    (0x3000, 0x303F),  # CJK punctuation
    (0x3040, 0x309F),  # hiragana
    (0x30A0, 0x30FF),  # katakana
    (0x31F0, 0x31FF),  # katakana phonetic extensions
    (0x3400, 0x4DBF),  # CJK extension A
    (0x4E00, 0x9FFF),  # CJK unified ideographs
    (0xF900, 0xFAFF),  # CJK compatibility ideographs
    (0xFF00, 0xFFEF),  # fullwidth and halfwidth forms
)


@dataclass
class BdfGlyph:
    codepoint: int
    bbx_width: int
    bbx_height: int
    bbx_x_offset: int
    bbx_y_offset: int
    rows: list[int]


def wanted(codepoint: int) -> bool:
    return any(start <= codepoint <= end for start, end in RANGES)


def parse_bdf(path: Path) -> dict[int, list[int]]:
    glyphs: dict[int, list[int]] = {}
    current_codepoint: int | None = None
    current_bbx: tuple[int, int, int, int] | None = None
    current_bitmap: list[int] | None = None
    in_bitmap = False

    with path.open("r", encoding="latin1") as f:
        for raw_line in f:
            line = raw_line.strip()
            if line.startswith("STARTCHAR "):
                current_codepoint = None
                current_bbx = None
                current_bitmap = []
                in_bitmap = False
            elif line.startswith("ENCODING "):
                parts = line.split()
                if len(parts) >= 2:
                    value = int(parts[1])
                    current_codepoint = value if value >= 0 else None
            elif line.startswith("BBX "):
                parts = line.split()
                if len(parts) == 5:
                    current_bbx = tuple(int(value) for value in parts[1:5])
            elif line == "BITMAP":
                in_bitmap = True
            elif line == "ENDCHAR":
                if (current_codepoint is not None and current_bbx is not None and current_bitmap is not None and
                        wanted(current_codepoint)):
                    glyph = BdfGlyph(
                        codepoint=current_codepoint,
                        bbx_width=current_bbx[0],
                        bbx_height=current_bbx[1],
                        bbx_x_offset=current_bbx[2],
                        bbx_y_offset=current_bbx[3],
                        rows=current_bitmap,
                    )
                    glyphs[current_codepoint] = normalize_bitmap(glyph)
                current_codepoint = None
                current_bbx = None
                current_bitmap = None
                in_bitmap = False
            elif in_bitmap and current_bitmap is not None and line:
                current_bitmap.append(int(line, 16))

    return glyphs


def normalize_bitmap(glyph: BdfGlyph) -> list[int]:
    """Convert a BDF glyph bitmap to 16 rows of 16 MSB-first pixels."""
    out = [0] * HEIGHT
    width = min(glyph.bbx_width, WIDTH)
    row_count = min(len(glyph.rows), glyph.bbx_height, HEIGHT)
    source_bits = ((glyph.bbx_width + 7) // 8) * 8
    x_offset = max(0, min(WIDTH - width, (WIDTH - glyph.bbx_width) // 2 + max(0, glyph.bbx_x_offset)))
    y_offset = max(0, min(HEIGHT - row_count, (HEIGHT - row_count) // 2))
    for i in range(row_count):
        value = glyph.rows[i]
        row = 0
        for col in range(width):
            if value & (1 << (source_bits - 1 - col)):
                row |= 1 << (WIDTH - 1 - x_offset - col)
        out[y_offset + i] = row
    return out


def pack_glyph(rows: list[int]) -> bytes:
    data = bytearray()
    for row in rows:
        data.append((row >> 8) & 0xFF)
        data.append(row & 0xFF)
    return bytes(data)


def write_font(glyphs: dict[int, list[int]], output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    codepoints = sorted(glyphs)
    count = len(codepoints)
    index_offset = HEADER_SIZE
    bitmap_offset = index_offset + count * 4

    with output.open("wb") as f:
        f.write(MAGIC)
        f.write(bytes([VERSION, WIDTH, HEIGHT, GLYPH_BYTES, 0, 0]))
        f.write(struct.pack("<III", count, index_offset, bitmap_offset))
        for codepoint in codepoints:
            f.write(struct.pack("<I", codepoint))
        for codepoint in codepoints:
            f.write(pack_glyph(glyphs[codepoint]))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("bdf", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    glyphs = parse_bdf(args.bdf)
    write_font(glyphs, args.output)
    print(f"wrote {args.output} with {len(glyphs)} glyphs")


if __name__ == "__main__":
    main()
