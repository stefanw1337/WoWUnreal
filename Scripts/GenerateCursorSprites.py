#!/usr/bin/env python3
"""
Prepare original cursor sprites for the in-game software cursor.

The source gauntlet is generated art, not copied from WoW. State variants are
small procedural overlays so the project has usable fallback cursors even when
MPQ UI data is unavailable.
"""

from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter


ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path.home() / ".codex" / "generated_images" / "019fb7c1-4a2a-7102-b7a8-4ec725fc7d9d" / "call_hyb5EhR34CiqeQuzcZYRLxlB.png"
OUT = ROOT / "Content" / "WowUI" / "Skins" / "WotlkLike" / "Cursors"
SIZE = 64


def fit_source() -> Image.Image:
    image = Image.open(SOURCE).convert("RGBA")
    bbox = image.getbbox()
    if bbox:
        image = image.crop(bbox)
    image.thumbnail((SIZE - 4, SIZE - 4), Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    canvas.alpha_composite(image, (2, 2))
    return canvas


def badge(base: Image.Image, fill: tuple[int, int, int, int], outline: tuple[int, int, int, int]) -> ImageDraw.ImageDraw:
    draw = ImageDraw.Draw(base)
    draw.ellipse((40, 40, 62, 62), fill=(0, 0, 0, 190))
    draw.ellipse((42, 42, 60, 60), fill=fill, outline=outline, width=2)
    return draw


def make_attack(base: Image.Image) -> Image.Image:
    image = base.copy()
    draw = badge(image, (120, 5, 5, 245), (255, 190, 60, 245))
    draw.line((47, 55, 56, 46), fill=(245, 245, 235, 255), width=3)
    draw.line((52, 56, 57, 51), fill=(245, 245, 235, 255), width=2)
    return image


def make_speak(base: Image.Image) -> Image.Image:
    image = base.copy()
    draw = badge(image, (25, 35, 60, 245), (255, 210, 90, 245))
    draw.rounded_rectangle((46, 46, 58, 54), radius=2, fill=(235, 235, 220, 255))
    draw.polygon(((49, 54), (52, 58), (53, 54)), fill=(235, 235, 220, 255))
    return image


def make_buy(base: Image.Image) -> Image.Image:
    image = base.copy()
    draw = badge(image, (35, 80, 30, 245), (255, 210, 90, 245))
    draw.ellipse((47, 46, 56, 56), fill=(230, 180, 45, 255), outline=(90, 55, 5, 255), width=1)
    draw.text((49, 45), "$", fill=(65, 45, 5, 255))
    return image


def make_quest(base: Image.Image) -> Image.Image:
    image = base.copy()
    draw = badge(image, (65, 45, 5, 245), (255, 215, 65, 255))
    draw.text((48, 43), "!", fill=(255, 230, 70, 255))
    return image


def make_pickup(base: Image.Image) -> Image.Image:
    image = base.copy()
    draw = badge(image, (64, 42, 22, 245), (255, 210, 90, 245))
    draw.rectangle((47, 49, 56, 56), fill=(140, 88, 35, 255), outline=(255, 205, 90, 255))
    draw.line((48, 49, 55, 49), fill=(230, 175, 70, 255), width=2)
    return image


def make_unable(base: Image.Image) -> Image.Image:
    image = base.copy()
    grayscale = Image.new("RGBA", image.size, (0, 0, 0, 0))
    for y in range(image.height):
        for x in range(image.width):
            r, g, b, a = image.getpixel((x, y))
            lum = int((r + g + b) / 3)
            grayscale.putpixel((x, y), (lum, lum, lum, a))
    draw = ImageDraw.Draw(grayscale)
    draw.line((41, 43, 61, 61), fill=(190, 20, 15, 245), width=5)
    draw.line((61, 43, 41, 61), fill=(190, 20, 15, 245), width=5)
    return grayscale.filter(ImageFilter.UnsharpMask(radius=1, percent=110, threshold=3))


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    base = fit_source()
    variants = {
        "cursor_point.png": base,
        "cursor_attack.png": make_attack(base),
        "cursor_speak.png": make_speak(base),
        "cursor_buy.png": make_buy(base),
        "cursor_quest.png": make_quest(base),
        "cursor_inspect.png": make_speak(base),
        "cursor_pickup.png": make_pickup(base),
        "cursor_unable_attack.png": make_unable(make_attack(base)),
        "cursor_unable_point.png": make_unable(base),
    }
    for name, image in variants.items():
        image.save(OUT / name)


if __name__ == "__main__":
    main()
