#!/usr/bin/env python3
"""Generate firmware assets for the Claude Usage Meter.

- Anti-aliased fonts in TFT_eSPI "smooth font" (.vlw) format, embedded as
  PROGMEM arrays -> firmware/src/gen_fonts.h
- Pixel-art sprites as palette-indexed arrays -> firmware/src/gen_sprites.h
- PNG previews of the sprites -> docs/images/

Run:  .venv/bin/python tools/gen_assets.py
"""
import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
FONTS = ROOT / "tools" / "fonts"
OUT = ROOT / "firmware" / "src"
PREVIEW = ROOT / "docs" / "images"

ASCII = "".join(chr(c) for c in range(0x20, 0x7F))
DIGITS = "0123456789"


# --------------------------------------------------------------------------
# Fonts
# --------------------------------------------------------------------------
def load_font(file, size, weight, opsz=None):
    f = ImageFont.truetype(str(FONTS / file), size)
    axes = f.get_variation_axes()
    vals = []
    for a in axes:
        name = a["name"].decode() if isinstance(a["name"], bytes) else a["name"]
        if name.lower().startswith("weight"):
            vals.append(weight)
        elif name.lower().startswith("optical"):
            vals.append(max(a["minimum"], min(a["maximum"], opsz or size)))
        else:
            vals.append(a["default"])
    f.set_variation_by_axes(vals)
    return f


def render_glyph(font, ch):
    """Return (bitmap bytes, width, height, xAdvance, dY(top above baseline), dX)."""
    adv = round(font.getlength(ch))
    x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")
    w, h = x1 - x0, y1 - y0
    if w <= 0 or h <= 0:
        return bytes([0]), 1, 1, max(adv, 1), 0, 0
    img = Image.new("L", (w, h), 0)
    ImageDraw.Draw(img).text((-x0, -y0), ch, font=font, fill=255, anchor="ls")
    return img.tobytes(), w, h, adv, -y0, x0


def build_vlw(font, chars, size_pt, tabular_digits=False):
    glyphs = [(ord(c), *render_glyph(font, c)) for c in sorted(set(chars))]
    if tabular_digits:
        dig = [g for g in glyphs if chr(g[0]) in DIGITS]
        tab = max(g[4] for g in dig)
        fixed = []
        for g in glyphs:
            if chr(g[0]) in DIGITS:
                code, bmp, w, h, adv, dy, dx = g
                g = (code, bmp, w, h, tab, dy, (tab - w) // 2)
            fixed.append(g)
        glyphs = fixed

    def metric(ch):
        _, y0, _, y1 = font.getbbox(ch, anchor="ls")
        return -y0, y1

    ascent = metric("d")[0] if "d" in chars else max(g[5] for g in glyphs)
    descent = metric("p")[1] if "p" in chars else 0

    out = bytearray()
    out += struct.pack(">6i", len(glyphs), 11, size_pt, 0, ascent, descent)
    for code, bmp, w, h, adv, dy, dx in glyphs:
        out += struct.pack(">7i", code, h, w, adv, dy, dx, 0)
    for g in glyphs:
        out += g[1]
    out += b"\x00\x00\x00\x01"  # empty name strings + "anti-aliased" flag
    return bytes(out)


FONT_SPECS = [
    # name,        file,               px, weight, chars,                         tabular
    ("FONT_CLOCK", "Inter.ttf",         28, 500,  DIGITS + ":-",                  True),
    ("FONT_PCT",   "Inter.ttf",         26, 700,  DIGITS + "%-",                  True),
    ("FONT_PILL",  "Inter.ttf",         12, 500,  ASCII,                          False),
    ("FONT_BODY",  "Inter.ttf",         13, 400,  ASCII,                          False),
    ("FONT_MONO",  "JetBrainsMono.ttf", 14, 600,  ASCII,                          False),
    ("FONT_BIG",   "Inter.ttf",         20, 600,  ASCII,                          False),
]


def c_array(name, data):
    lines = [f"const uint8_t {name}[] PROGMEM = {{"]
    for i in range(0, len(data), 24):
        lines.append("  " + ",".join(f"0x{b:02x}" for b in data[i:i + 24]) + ",")
    lines.append("};")
    return "\n".join(lines)


# --------------------------------------------------------------------------
# Sprites (palette indexed, '.' = transparent / background)
# --------------------------------------------------------------------------
PALETTE = {
    ".": None,
    "O": 0xF07A2B,  # main orange
    "o": 0xE85A2A,  # darker orange edges
    "K": 0x000000,  # eyes
    "H": 0xA9AEE6,  # headphones highlight
    "h": 0x8A8FC4,  # headphones shade
    "L": 0x7E7E8C,  # laptop body
    "l": 0xB4B8E8,  # laptop screen-back highlight
    "D": 0x2A2630,  # desk line
    "W": 0xFFFFFF,  # logo glint on laptop
}

# Header mascot: 12x9, drawn at 2x -> 24x18. Wide blocky critter, two
# little ear bumps, square eyes, four stubby legs.
MASCOT = [
    "..OO....OO..",
    ".OOOOOOOOOO.",
    "OOOOOOOOOOOO",
    "OOKKOOOOKKOO",
    "OOKKOOOOKKOO",
    "OOOOOOOOOOOO",
    ".OOOOOOOOOO.",
    ".OO.OO.OO.OO",
    ".OO.OO.OO.OO",
]

# Footer mini mascot: 7x7, drawn at 2x -> 14x14.
MASCOT_MINI = [
    ".O...O.",
    "OOOOOOO",
    "OKOOOKO",
    "OOOOOOO",
    "OOOOOOO",
    "O.O.O.O",
    "O.O.O.O",
]

# Idle scene: 24x24, drawn at 8x -> 192x192.
IDLE_BASE = [
    "........................",
    "........................",
    "........................",
    ".........hhhhhh.........",
    ".......hhHHHHHHhh.......",
    "......hH........Hh......",
    ".....hH..oooooo..Hh.....",
    ".....h..oOOOOOOo..h.....",
    "....hh.oOOOOOOOOo.hh....",
    "...hHHhoOOOOOOOOohHHh...",
    "...hHHhOOKKOOKKOOhHHh...",
    "...hHHhOOKKOOKKOOhHHh...",
    "...hhhhOOOOOOOOOOhhhh...",
    "......oOOOOOOOOOOo......",
    "......oOOOOOOOOOOo......",
    ".....lllllllllllllll....",
    ".....LLLLLLLLLLLLLLL....",
    ".....LLLLLLLLLLLLLLL....",
    "....oLLLLLLLWLLLLLLLo...",
    "...oOLLLLLLLLLLLLLLLOo..",
    "....oLLLLLLLLLLLLLLLo...",
    "..DDDDDDDDDDDDDDDDDDDDD.",
    "........................",
    "........................",
]


def _check(name, rows, w, h):
    assert len(rows) == h, f"{name}: {len(rows)} rows, want {h}"
    for i, r in enumerate(rows):
        assert len(r) == w, f"{name} row {i}: {len(r)} cols, want {w}: {r!r}"
        assert set(r) <= set(PALETTE), f"{name} row {i}: bad chars {set(r) - set(PALETTE)}"


def idle_frames():
    """Frames: 0 = base, 1 = blink, 2/3 = typing hands alternate."""
    base = [list(r) for r in IDLE_BASE]

    def copy():
        return [r[:] for r in base]

    blink = copy()
    for y in (10, 11):
        for x in (9, 10, 13, 14):
            blink[y][x] = "O"
    for x in (9, 10, 13, 14):
        blink[11][x] = "K"  # eyes become a thin closed line

    type_a = copy()  # left hand up, right hand down
    type_a[18][4] = "."
    type_a[17][4] = "o"
    type_b = copy()  # right hand up
    type_b[18][20] = "."
    type_b[17][20] = "o"
    return [base, blink, type_a, type_b]


def sprite_indices(rows, keys):
    return bytes(keys.index(ch) for row in rows for ch in row)


def to_png(rows, scale, path, bg=(0x0E, 0x0E, 0x0E)):
    h, w = len(rows), len(rows[0])
    img = Image.new("RGB", (w, h), bg)
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            c = PALETTE[ch]
            if c is not None:
                img.putpixel((x, y), ((c >> 16) & 255, (c >> 8) & 255, c & 255))
    img.resize((w * scale, h * scale), Image.NEAREST).save(path)


def rgb565(c):
    r, g, b = (c >> 16) & 255, (c >> 8) & 255, c & 255
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    PREVIEW.mkdir(parents=True, exist_ok=True)

    # ---- fonts
    parts = ["// Generated by tools/gen_assets.py - do not edit.",
             "// Fonts: Inter, JetBrains Mono (SIL Open Font License 1.1).",
             "#pragma once", "#include <Arduino.h>", ""]
    total = 0
    for name, file, px, weight, chars, tab in FONT_SPECS:
        f = load_font(file, px, weight)
        data = build_vlw(f, chars, px, tabular_digits=tab)
        total += len(data)
        parts.append(c_array(name, data))
        parts.append("")
    (OUT / "gen_fonts.h").write_text("\n".join(parts))
    print(f"fonts: {len(FONT_SPECS)} fonts, {total} bytes")

    # ---- sprites
    _check("MASCOT", MASCOT, 12, 9)
    _check("MASCOT_MINI", MASCOT_MINI, 7, 7)
    frames = idle_frames()
    for i, fr in enumerate(frames):
        _check(f"IDLE[{i}]", ["".join(r) for r in fr], 24, 24)

    keys = list(PALETTE.keys())
    sp = ["// Generated by tools/gen_assets.py - do not edit.", "#pragma once",
          "#include <Arduino.h>", "",
          "// Palette index 0 = transparent.",
          f"static const uint16_t SPRITE_PALETTE[{len(keys)}] PROGMEM = {{" +
          ", ".join("0x0000" if PALETTE[k] is None else f"0x{rgb565(PALETTE[k]):04X}" for k in keys) + "};",
          ""]
    sp.append(c_array("SPR_MASCOT", sprite_indices(MASCOT, keys)).replace("uint8_t", "uint8_t") )
    sp.append("#define SPR_MASCOT_W 12\n#define SPR_MASCOT_H 9\n")
    sp.append(c_array("SPR_MASCOT_MINI", sprite_indices(MASCOT_MINI, keys)))
    sp.append("#define SPR_MASCOT_MINI_W 7\n#define SPR_MASCOT_MINI_H 7\n")
    for i, fr in enumerate(frames):
        sp.append(c_array(f"SPR_IDLE_{i}", sprite_indices(fr, keys)))
    sp.append("#define SPR_IDLE_W 24\n#define SPR_IDLE_H 24\n#define SPR_IDLE_FRAMES 4")
    sp.append("static const uint8_t* const SPR_IDLE[SPR_IDLE_FRAMES] = "
              "{SPR_IDLE_0, SPR_IDLE_1, SPR_IDLE_2, SPR_IDLE_3};\n")
    (OUT / "gen_sprites.h").write_text("\n".join(sp))

    to_png(MASCOT, 8, PREVIEW / "mascot.png")
    to_png(MASCOT_MINI, 8, PREVIEW / "mascot_mini.png")
    for i, fr in enumerate(frames):
        to_png(["".join(r) for r in fr], 8, PREVIEW / f"idle_{i}.png", bg=(0, 0, 0))
    print("sprites + previews written")


if __name__ == "__main__":
    main()
