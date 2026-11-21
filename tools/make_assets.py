#!/usr/bin/env python3
"""Genera los recursos gráficos incluidos en el repositorio.

- engine/assets/engine/font.png : fuente monoespaciada de depuración (ASCII 32..127, rejilla 16x6, celdas 1:2)
- sample/assets/textures/orb.png : partícula del juego de ejemplo

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


def make_orb(size=128):
    # Círculo blanco con brillo suave: el color lo pone el sprite al multiplicar.
    scale = 4
    big = size * scale
    mask = Image.new("L", (big, big), 0)
    ImageDraw.Draw(mask).ellipse((scale, scale, big - scale, big - scale), fill=255)
    mask = mask.resize((size, size), Image.LANCZOS)
    shade = Image.new("L", (size, size), 0)
    d = ImageDraw.Draw(shade)
    for r in range(size // 2, 0, -1):
        v = int(170 + 85 * (1 - r / (size / 2)) ** 0.6)
        c = size * 0.42
        d.ellipse((c - r, c - r, c + r, c + r), fill=v)
    shade = shade.filter(ImageFilter.GaussianBlur(size / 24))
    img = Image.merge("RGBA", (shade, shade, shade, mask))
    out = ROOT / "sample/assets/textures/orb.png"
    out.parent.mkdir(parents=True, exist_ok=True)
    img.save(out, optimize=True)
    print("escrito", out.relative_to(ROOT))


if __name__ == "__main__":
    make_font()
    make_orb()
