#!/usr/bin/env python3
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parent
FONT_DIR = Path("/usr/share/fonts/truetype")
SANS_BOLD = FONT_DIR / "noto" / "NotoSans-Bold.ttf"
MONO_BOLD = FONT_DIR / "dejavu" / "DejaVuSansMono-Bold.ttf"


def font(path, size):
    if path.exists():
        return ImageFont.truetype(str(path), size)
    return ImageFont.truetype(str(FONT_DIR / "dejavu" / "DejaVuSans-Bold.ttf"), size)


def draw_icon(size):
    scale = 4
    canvas = Image.new("RGB", (size * scale, size * scale), "#121722")
    draw = ImageDraw.Draw(canvas)

    for y in range(size * scale):
        mix = y / max(1, size * scale - 1)
        color = (
            int(18 + 8 * mix),
            int(23 + 18 * mix),
            int(34 + 12 * mix),
        )
        draw.line((0, y, size * scale, y), fill=color)

    draw.ellipse(
        (
            int(size * .18 * scale),
            int(-size * .18 * scale),
            int(size * 1.18 * scale),
            int(size * .82 * scale),
        ),
        fill="#1f6d57",
    )

    pad = int(size * .14 * scale)
    box = (pad, pad, size * scale - pad, size * scale - pad)
    draw.rounded_rectangle(
        box,
        radius=int(size * .12 * scale),
        fill="#f9fbf7",
        outline="#9de4b2",
        width=max(2, int(size * .012 * scale)),
    )

    chrome_h = int(size * .18 * scale)
    draw.rounded_rectangle(
        (box[0], box[1], box[2], box[1] + chrome_h),
        radius=int(size * .12 * scale),
        fill="#eef2f0",
    )
    draw.rectangle((box[0], box[1] + chrome_h // 2, box[2], box[1] + chrome_h), fill="#eef2f0")

    dot_r = max(2, int(size * .017 * scale))
    dot_y = box[1] + chrome_h // 2
    for i, color in enumerate(("#e25545", "#e7b83e", "#39a86b")):
        dot_x = box[0] + int(size * (.09 + i * .055) * scale)
        draw.ellipse((dot_x - dot_r, dot_y - dot_r, dot_x + dot_r, dot_y + dot_r), fill=color)

    c_face = font(SANS_BOLD, int(size * .46 * scale))
    prompt_face = font(MONO_BOLD, int(size * .18 * scale))
    c_text = "C"
    c_box = draw.textbbox((0, 0), c_text, font=c_face)
    c_w = c_box[2] - c_box[0]
    c_h = c_box[3] - c_box[1]
    draw.text(
        ((size * scale - c_w) / 2, size * .36 * scale - c_h / 2),
        c_text,
        font=c_face,
        fill="#121722",
    )

    prompt = ">_"
    p_box = draw.textbbox((0, 0), prompt, font=prompt_face)
    p_w = p_box[2] - p_box[0]
    draw.text(
        ((size * scale - p_w) / 2, size * .67 * scale),
        prompt,
        font=prompt_face,
        fill="#1a6f5a",
    )

    return canvas.resize((size, size), Image.Resampling.LANCZOS)


def main():
    for size, name in (
        (192, "icon-192.png"),
        (512, "icon-512.png"),
        (180, "apple-touch-icon.png"),
    ):
        draw_icon(size).save(ROOT / name, "PNG", optimize=True)


if __name__ == "__main__":
    main()
