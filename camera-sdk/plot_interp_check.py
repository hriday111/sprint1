#!/usr/bin/env python3
"""Plot calibrate2 interp_intensity.csv — check if interpolated ROI means stay flat.

Usage:
  python3 plot_interp_check.py [csv=./tmp/interp_intensity.csv] [out=./tmp/interp_intensity.png]
"""

from __future__ import annotations

import csv
import math
import sys
from pathlib import Path


def load_csv(path: Path):
    with path.open(newline="") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        raise SystemExit(f"empty CSV: {path}")
    pos = [int(r["pos"]) for r in rows]
    interp_cols = [c for c in rows[0] if c.endswith("_interp")]
    fixed_cols = [c for c in rows[0] if c.endswith("_fixed")]
    series = {}
    for c in interp_cols + fixed_cols:
        series[c] = [float(r[c]) for r in rows]
    return pos, series, interp_cols, fixed_cols


def linear_fit(xs, ys):
    n = len(xs)
    mx = sum(xs) / n
    my = sum(ys) / n
    num = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    den = sum((x - mx) ** 2 for x in xs) or 1.0
    slope = num / den
    intercept = my - slope * mx
    return slope, intercept, my


def render_png(path: Path, pos, series, interp_cols, fixed_cols, out: Path):
    try:
        from PIL import Image, ImageDraw, ImageFont
    except ImportError as e:
        raise SystemExit(f"Pillow required for PNG output: {e}") from e

    W, H = 960, 540
    pad_l, pad_r, pad_t, pad_b = 70, 30, 50, 60
    img = Image.new("RGB", (W, H), (250, 250, 248))
    draw = ImageDraw.Draw(img)
    try:
        font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 14)
        font_sm = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 11)
    except OSError:
        font = ImageFont.load_default()
        font_sm = font

    plot_cols = interp_cols + fixed_cols
    all_y = [v for c in plot_cols for v in series[c]]
    ymin, ymax = min(all_y), max(all_y)
    if abs(ymax - ymin) < 1e-6:
        ymin -= 1
        ymax += 1
    # pad 5%
    span = ymax - ymin
    ymin -= 0.05 * span
    ymax += 0.05 * span
    xmin, xmax = min(pos), max(pos)
    if xmin == xmax:
        xmin -= 1
        xmax += 1

    def sx(x):
        return pad_l + (x - xmin) / (xmax - xmin) * (W - pad_l - pad_r)

    def sy(y):
        return pad_t + (ymax - y) / (ymax - ymin) * (H - pad_t - pad_b)

    # axes
    draw.rectangle([pad_l, pad_t, W - pad_r, H - pad_b], outline=(40, 40, 40), width=1)
    draw.text((W // 2 - 80, 12), "ROI mean vs servo position", fill=(20, 20, 20), font=font)
    draw.text((W // 2 - 40, H - 28), "servo pos", fill=(40, 40, 40), font=font_sm)
    draw.text((12, H // 2 - 40), "mean", fill=(40, 40, 40), font=font_sm)

    colors_interp = [(20, 120, 60), (30, 90, 160), (160, 90, 20)]
    colors_fixed = [(160, 60, 60), (180, 100, 100), (200, 140, 80)]

    legend_y = pad_t + 8
    stats_lines = []

    for i, c in enumerate(interp_cols):
        color = colors_interp[i % len(colors_interp)]
        ys = series[c]
        pts = [(sx(x), sy(y)) for x, y in zip(pos, ys)]
        if len(pts) >= 2:
            draw.line(pts, fill=color, width=2)
        for p in pts:
            draw.ellipse([p[0] - 2, p[1] - 2, p[0] + 2, p[1] + 2], fill=color)
        slope, _, mean = linear_fit(pos, ys)
        stats_lines.append(f"{c}: mean={mean:.2f}  slope={slope:.4f}/step")
        draw.text((pad_l + 8, legend_y), c, fill=color, font=font_sm)
        legend_y += 16

    for i, c in enumerate(fixed_cols):
        color = colors_fixed[i % len(colors_fixed)]
        ys = series[c]
        pts = [(sx(x), sy(y)) for x, y in zip(pos, ys)]
        if len(pts) >= 2:
            # dashed-ish by segments
            for a, b in zip(pts, pts[1:]):
                draw.line([a, b], fill=color, width=1)
        slope, _, mean = linear_fit(pos, ys)
        stats_lines.append(f"{c}: mean={mean:.2f}  slope={slope:.4f}/step")
        draw.text((pad_l + 8, legend_y), c + " (dashed)", fill=color, font=font_sm)
        legend_y += 16

    # tick labels
    for x in (xmin, (xmin + xmax) / 2, xmax):
        draw.text((sx(x) - 10, H - pad_b + 8), f"{x:.0f}", fill=(40, 40, 40), font=font_sm)
    for y in (ymin, (ymin + ymax) / 2, ymax):
        draw.text((8, sy(y) - 6), f"{y:.1f}", fill=(40, 40, 40), font=font_sm)

    # stats box
    box_y = H - pad_b - 18 * len(stats_lines) - 8
    for j, line in enumerate(stats_lines):
        draw.text((W - pad_r - 340, box_y + j * 16), line, fill=(30, 30, 30), font=font_sm)

    out.parent.mkdir(parents=True, exist_ok=True)
    img.save(out)
    print(f"wrote {out}")
    print("Flat slope ≈ good tracking. Compare *_interp (solid) vs *_fixed (from pos0).")
    for line in stats_lines:
        print(" ", line)


def render_svg(pos, series, interp_cols, fixed_cols, out: Path):
    W, H = 960, 540
    pad_l, pad_r, pad_t, pad_b = 70, 30, 50, 60
    plot_cols = interp_cols + fixed_cols
    all_y = [v for c in plot_cols for v in series[c]]
    ymin, ymax = min(all_y), max(all_y)
    if abs(ymax - ymin) < 1e-6:
        ymin -= 1.0
        ymax += 1.0
    span = ymax - ymin
    ymin -= 0.05 * span
    ymax += 0.05 * span
    xmin, xmax = float(min(pos)), float(max(pos))
    if xmin == xmax:
        xmin -= 1
        xmax += 1

    def sx(x):
        return pad_l + (x - xmin) / (xmax - xmin) * (W - pad_l - pad_r)

    def sy(y):
        return pad_t + (ymax - y) / (ymax - ymin) * (H - pad_t - pad_b)

    colors_interp = ["#14783c", "#1e5aa0", "#a05a14"]
    colors_fixed = ["#a03c3c", "#b46464", "#c88c50"]

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}">',
        f'<rect width="100%" height="100%" fill="#fafaf8"/>',
        f'<text x="{W/2}" y="24" text-anchor="middle" font-size="16" '
        f'font-family="sans-serif">ROI mean vs servo position</text>',
        f'<rect x="{pad_l}" y="{pad_t}" width="{W-pad_l-pad_r}" '
        f'height="{H-pad_t-pad_b}" fill="none" stroke="#282828"/>',
    ]
    legend_y = pad_t + 14
    for i, c in enumerate(interp_cols):
        color = colors_interp[i % len(colors_interp)]
        pts = " ".join(f"{sx(x):.1f},{sy(y):.1f}" for x, y in zip(pos, series[c]))
        parts.append(f'<polyline fill="none" stroke="{color}" stroke-width="2" points="{pts}"/>')
        slope, _, mean = linear_fit(pos, series[c])
        parts.append(
            f'<text x="{pad_l+8}" y="{legend_y}" font-size="11" fill="{color}" '
            f'font-family="sans-serif">{c} mean={mean:.2f} slope={slope:.4f}</text>'
        )
        legend_y += 14
    for i, c in enumerate(fixed_cols):
        color = colors_fixed[i % len(colors_fixed)]
        pts = " ".join(f"{sx(x):.1f},{sy(y):.1f}" for x, y in zip(pos, series[c]))
        parts.append(
            f'<polyline fill="none" stroke="{color}" stroke-width="1.5" '
            f'stroke-dasharray="4 3" points="{pts}"/>'
        )
        slope, _, mean = linear_fit(pos, series[c])
        parts.append(
            f'<text x="{pad_l+8}" y="{legend_y}" font-size="11" fill="{color}" '
            f'font-family="sans-serif">{c} mean={mean:.2f} slope={slope:.4f}</text>'
        )
        legend_y += 14
    parts.append("</svg>")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(parts))
    print(f"wrote {out}")


def main():
    csv_path = Path(sys.argv[1] if len(sys.argv) > 1 else "./tmp/interp_intensity.csv")
    out = Path(sys.argv[2] if len(sys.argv) > 2 else "./tmp/interp_intensity.png")
    pos, series, interp_cols, fixed_cols = load_csv(csv_path)
    if out.suffix.lower() == ".svg":
        render_svg(pos, series, interp_cols, fixed_cols, out)
    else:
        try:
            render_png(csv_path, pos, series, interp_cols, fixed_cols, out)
        except SystemExit:
            svg = out.with_suffix(".svg")
            render_svg(pos, series, interp_cols, fixed_cols, svg)


if __name__ == "__main__":
    main()
