#!/usr/bin/env python3
"""Genera los recursos gráficos incluidos en el repositorio.

- engine/assets/engine/font.png : fuente monoespaciada de depuración (ASCII 32..127, rejilla 16x6, celdas 1:2)

Requiere Pillow. Uso: python3 tools/make_assets.py
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parent.parent
FONT_FILE = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"


def make_font(cell_w=32, cell_h=64, cols=16, rows=6):
    img = Image.new("RGBA", (cell_w * cols, cell_h * rows), (255, 255, 255, 0))
    draw = ImageDraw.Draw(img)
    font = ImageFont.truetype(FONT_FILE, 50)
    ascent, descent = font.getmetrics()
    for i in range(cols * rows):
        ch = chr(32 + i)
        if ch == "\x7f":
            continue
        x = (i % cols) * cell_w
        y = (i // cols) * cell_h
        w = font.getlength(ch)
        draw.text((x + (cell_w - w) / 2, y + (cell_h - ascent - descent) / 2), ch, font=font, fill=(255, 255, 255, 255))
    out = ROOT / "engine/assets/engine/font.png"
    out.parent.mkdir(parents=True, exist_ok=True)
    img.save(out, optimize=True)
    print("escrito", out.relative_to(ROOT))


if __name__ == "__main__":
    make_font()
