#!/usr/bin/env python3
"""
Generate original, project-local UI skin textures.

These images are procedurally generated and intentionally do not copy or embed
Blizzard UI art. They provide a WotLK-inspired fallback skin for offline/dev
use, while the client can later support optional local MPQ UI texture loading.
"""

from __future__ import annotations

import random
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "Content" / "WowUI" / "Skins" / "WotlkLike"


def clamp(value: int) -> int:
    return max(0, min(255, value))


def noisy_fill(size: tuple[int, int], base: tuple[int, int, int, int], noise: int, seed: int) -> Image.Image:
    rng = random.Random(seed)
    image = Image.new("RGBA", size, base)
    pixels = image.load()
    width, height = size
    for y in range(height):
        for x in range(width):
            delta = rng.randint(-noise, noise)
            pixels[x, y] = (
                clamp(base[0] + delta),
                clamp(base[1] + delta),
                clamp(base[2] + delta),
                base[3],
            )
    return image.filter(ImageFilter.GaussianBlur(0.35))


def draw_frame(path: Path, size: int, fill: tuple[int, int, int, int], seed: int) -> None:
    image = noisy_fill((size, size), fill, 10, seed)
    draw = ImageDraw.Draw(image)

    outer_dark = (10, 8, 6, 255)
    outer_light = (126, 104, 62, 245)
    inner_light = (82, 73, 54, 235)
    inner_dark = (22, 18, 14, 245)

    for inset in range(0, 7):
        color = outer_light if inset in (1, 2) else outer_dark
        draw.rectangle((inset, inset, size - 1 - inset, size - 1 - inset), outline=color)

    draw.rectangle((8, 8, size - 9, size - 9), outline=inner_light)
    draw.rectangle((10, 10, size - 11, size - 11), outline=inner_dark)

    for corner_x, corner_y in ((5, 5), (size - 10, 5), (5, size - 10), (size - 10, size - 10)):
        draw.ellipse((corner_x, corner_y, corner_x + 5, corner_y + 5), fill=(148, 116, 48, 230))
        draw.point((corner_x + 2, corner_y + 1), fill=(236, 205, 95, 240))

    image.save(path)


def draw_panel(path: Path, size: int, base: tuple[int, int, int, int], seed: int) -> None:
    image = noisy_fill((size, size), base, 13, seed)
    draw = ImageDraw.Draw(image)
    draw.rectangle((0, 0, size - 1, size - 1), outline=(72, 60, 43, 185))
    draw.rectangle((2, 2, size - 3, size - 3), outline=(13, 11, 9, 210))
    image.save(path)


def draw_button(path: Path, size: int, base: tuple[int, int, int, int], seed: int) -> None:
    image = noisy_fill((size, size), base, 8, seed)
    draw = ImageDraw.Draw(image)
    draw.rectangle((0, 0, size - 1, size - 1), outline=(8, 7, 5, 255))
    draw.rectangle((2, 2, size - 3, size - 3), outline=(166, 128, 54, 245))
    draw.rectangle((4, 4, size - 5, size - 5), outline=(35, 27, 18, 245))
    draw.line((4, 4, size - 5, 4), fill=(238, 190, 75, 160))
    draw.line((4, 4, 4, size - 5), fill=(238, 190, 75, 110))
    draw.line((4, size - 5, size - 5, size - 5), fill=(0, 0, 0, 130))
    draw.line((size - 5, 4, size - 5, size - 5), fill=(0, 0, 0, 130))
    image.save(path)


def draw_separator(path: Path) -> None:
    image = Image.new("RGBA", (64, 4), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.line((0, 0, 63, 0), fill=(16, 11, 5, 230))
    draw.line((0, 1, 63, 1), fill=(162, 111, 35, 170))
    draw.line((0, 2, 63, 2), fill=(67, 42, 13, 180))
    draw.line((0, 3, 63, 3), fill=(0, 0, 0, 180))
    image.save(path)


def draw_menu_arrow(path: Path, hover: bool) -> None:
    image = Image.new("RGBA", (18, 18), (4, 4, 3, 200))
    draw = ImageDraw.Draw(image)
    border = (195, 145, 45, 240) if hover else (83, 72, 43, 210)
    face = (244, 205, 67, 255) if hover else (176, 132, 30, 230)
    draw.rounded_rectangle((1, 1, 16, 16), radius=3, outline=border, fill=(8, 8, 7, 210))
    draw.polygon(((5, 7), (13, 7), (9, 12)), fill=face)
    image.save(path)


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    draw_frame(OUT / "frame_outer_64.png", 64, (32, 26, 18, 235), 1001)
    draw_frame(OUT / "frame_inner_64.png", 64, (43, 34, 22, 232), 1002)
    draw_panel(OUT / "panel_dark_64.png", 64, (8, 8, 7, 178), 2001)
    draw_panel(OUT / "panel_warm_64.png", 64, (46, 37, 25, 210), 2002)
    draw_panel(OUT / "panel_parchment_64.png", 64, (62, 50, 33, 220), 2003)
    draw_panel(OUT / "tracker_panel_64.png", 64, (5, 5, 4, 72), 2004)
    draw_button(OUT / "button_red_32.png", 32, (99, 8, 3, 242), 3001)
    draw_button(OUT / "button_dark_32.png", 32, (20, 18, 14, 238), 3002)
    draw_button(OUT / "selection_blue_32.png", 32, (26, 45, 85, 205), 3003)
    draw_separator(OUT / "separator_gold_64x4.png")
    draw_menu_arrow(OUT / "menu_arrow_18.png", False)
    draw_menu_arrow(OUT / "menu_arrow_hover_18.png", True)


if __name__ == "__main__":
    main()
