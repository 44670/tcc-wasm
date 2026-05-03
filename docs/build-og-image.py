#!/usr/bin/env python3
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parent
OUT = ROOT / "og-image.jpg"

WIDTH = 1200
HEIGHT = 630
SCALE = 3

FONT_DIR = Path("/usr/share/fonts/truetype")
SANS = FONT_DIR / "noto" / "NotoSans-Regular.ttf"
SANS_BOLD = FONT_DIR / "noto" / "NotoSans-Bold.ttf"
MONO = FONT_DIR / "dejavu" / "DejaVuSansMono.ttf"
MONO_BOLD = FONT_DIR / "dejavu" / "DejaVuSansMono-Bold.ttf"


def font(path, size):
    if path.exists():
        return ImageFont.truetype(str(path), size)
    return ImageFont.truetype(str(FONT_DIR / "dejavu" / "DejaVuSans.ttf"), size)


def rr(draw, xy, radius, fill, outline=None, width=1):
    xy = tuple(round(v * SCALE) for v in xy)
    draw.rounded_rectangle(
        xy,
        radius=round(radius * SCALE),
        fill=fill,
        outline=outline,
        width=round(width * SCALE),
    )


def text(draw, xy, label, fill, face):
    draw.text((xy[0] * SCALE, xy[1] * SCALE), label, fill=fill, font=face)


def line(draw, xy, fill, width=1):
    draw.line(tuple(round(v * SCALE) for v in xy), fill=fill, width=round(width * SCALE))


def main():
    img = Image.new("RGB", (WIDTH * SCALE, HEIGHT * SCALE), "#121722")
    draw = ImageDraw.Draw(img)

    # Subtle landing-page style backdrop.
    for y in range(HEIGHT * SCALE):
        mix = y / (HEIGHT * SCALE - 1)
        r = int(18 + 8 * mix)
        g = int(23 + 13 * mix)
        b = int(34 + 9 * mix)
        draw.line((0, y, WIDTH * SCALE, y), fill=(r, g, b))

    for x, alpha in ((870, 34), (970, 28), (1070, 22)):
        draw.ellipse(
            (
                (x - 320) * SCALE,
                -180 * SCALE,
                (x + 320) * SCALE,
                460 * SCALE,
            ),
            fill=(33, 77 + alpha, 84),
        )

    # Editor window.
    rr(draw, (82, 78, 694, 552), 10, "#f9fbf7", "#4b5563", 1)
    rr(draw, (82, 78, 694, 118), 10, "#eef2f0", "#d6dbe1", 1)
    draw.rectangle((82 * SCALE, 101 * SCALE, 694 * SCALE, 118 * SCALE), fill="#eef2f0")
    for i, color in enumerate(("#e25545", "#e7b83e", "#39a86b")):
        draw.ellipse(
            (
                (106 + i * 24) * SCALE,
                94 * SCALE,
                (118 + i * 24) * SCALE,
                106 * SCALE,
            ),
            fill=color,
        )
    text(draw, (190, 89), "main.c", "#3a4250", font(SANS_BOLD, 18 * SCALE))

    draw.rectangle((82 * SCALE, 118 * SCALE, 137 * SCALE, 552 * SCALE), fill="#edf2f1")
    line(draw, (137, 118, 137, 552), "#d8dde6", 1)
    mono = font(MONO, 23 * SCALE)
    mono_bold = font(MONO_BOLD, 23 * SCALE)
    for idx in range(1, 13):
        text(draw, (104, 139 + (idx - 1) * 31), str(idx), "#778292", mono)

    code_x = 160
    y = 139
    code = [
        [("#include", "#0f62b8", mono_bold), (" <stdio.h>", "#a43c32", mono)],
        [("#include", "#0f62b8", mono_bold), (" <stdlib.h>", "#a43c32", mono)],
        [],
        [("int", "#9a4f12", mono_bold), (" main(void) {", "#17202c", mono)],
        [("    int", "#9a4f12", mono_bold), (" scores[] = {42, 7, 19, 88};", "#17202c", mono)],
        [("    qsort", "#1a6f5a", mono_bold), ("(scores, 4, sizeof scores[0], cmp);", "#17202c", mono)],
        [("    printf", "#1a6f5a", mono_bold), ("(\"%d\\n\", scores[0]);", "#a43c32", mono)],
        [("    return", "#0f62b8", mono_bold), (" 0;", "#17202c", mono)],
        [("}", "#17202c", mono)],
    ]
    for row in code:
        x = code_x
        if not row:
            y += 31
            continue
        for chunk, color, face in row:
            text(draw, (x, y), chunk, color, face)
            x += draw.textlength(chunk, font=face) / SCALE
        y += 31

    rr(draw, (445, 316, 530, 350), 6, "#ffffff", "#adc2d8", 1)
    text(draw, (465, 321), "qsort", "#334155", font(SANS_BOLD, 17 * SCALE))

    # Terminal panes.
    rr(draw, (720, 78, 1118, 230), 8, "#f9fbf7", "#4b5563", 1)
    draw.rectangle((720 * SCALE, 78 * SCALE, 1118 * SCALE, 112 * SCALE), fill="#eef2f0")
    text(draw, (740, 86), "stdin", "#202938", font(SANS_BOLD, 17 * SCALE))
    text(draw, (744, 132), "4", "#1d2530", mono)
    text(draw, (744, 166), "42 7 19 88", "#1d2530", mono)

    rr(draw, (720, 254, 1118, 430), 8, "#f9fbf7", "#4b5563", 1)
    draw.rectangle((720 * SCALE, 254 * SCALE, 1118 * SCALE, 288 * SCALE), fill="#eef2f0")
    text(draw, (740, 262), "stdout", "#202938", font(SANS_BOLD, 17 * SCALE))
    text(draw, (744, 308), "7", "#1d2530", mono)
    text(draw, (744, 354), "compiled in browser", "#1d2530", mono)
    text(draw, (744, 388), "ready offline", "#1d2530", mono)

    # Foreground message panel.
    rr(draw, (424, 108, 1110, 540), 12, "#121722", "#364152", 1)
    rr(draw, (456, 144, 650, 180), 18, "#21382f", "#3f7058", 1)
    text(draw, (478, 150), "TCC Wasm IDE", "#bdf2cd", font(SANS_BOLD, 20 * SCALE))
    text(draw, (456, 210), "Free In-Browser", "#ffffff", font(SANS_BOLD, 64 * SCALE))
    text(draw, (456, 282), "C IDE", "#ffffff", font(SANS_BOLD, 76 * SCALE))
    text(draw, (460, 378), "Offline Use Capable", "#c7f7d4", font(SANS_BOLD, 35 * SCALE))
    text(draw, (460, 432), "Write, compile, and run C locally in your browser.", "#d9e2ec", font(SANS, 28 * SCALE))

    chips = [("No install", 460), ("Student friendly", 604), ("Runs locally", 816)]
    for label, x in chips:
        w = draw.textlength(label, font=font(SANS_BOLD, 18 * SCALE)) / SCALE + 34
        rr(draw, (x, 482, x + w, 520), 19, "#f9fbf7", "#9de4b2", 1)
        text(draw, (x + 17, 489), label, "#0c2115", font(SANS_BOLD, 18 * SCALE))

    img = img.resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS)
    img.save(OUT, "JPEG", quality=92, optimize=True, progressive=True)


if __name__ == "__main__":
    main()
