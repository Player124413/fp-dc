#!/usr/bin/env python3
"""What the phone actually shows: the old upscale against the fixed one, at 2652x1200.

The GLES upscale took the texture coordinates from the window instead of from the view rect, so on a
2.2:1 phone the 426x240 picture was zoomed 1.245x and its sides were cut off (the "seam" through the
palm tree, the content sliding under the edge of the black band). This renders both cases from one
synthetic game frame, so the difference can be looked at without a device:

    python3 tools/preview/present_sim.py      ->  tools/preview/out/present_old_vs_new.png
"""
import numpy as np
from PIL import Image

VW, VH = 426, 240
WW, WH = 2652, 1200          # the phone the bug was reported on


def frame():
    """A stand-in game frame: sky, ground, palms, HUD, and a circle that shows any stretch."""
    y, x = np.mgrid[0:VH, 0:VW]
    f = np.zeros((VH, VW, 3), np.float64)
    t = y / float(VH)
    f[..., 0] = 120 + 90 * (1 - t)                  # sky, lighter at the horizon
    f[..., 1] = 170 + 70 * (1 - t)
    f[..., 2] = 235
    ground = y > 190
    f[ground] = (58, 132, 64)
    f[ground & (y > 205)] = (44, 108, 52)
    for cx, top in ((60, 40), (300, 20), (392, 60)):   # palms: trunk plus four fronds
        f[top:214, cx - 3:cx + 4] = (116, 84, 48)
        for dx, dy in ((-1, -1), (1, -1), (-1, 0), (1, 0)):
            for k in range(34):
                px, py = cx + dx * k // 2, top + 6 + dy * k // 3
                if 0 <= px < VW and 0 <= py < VH:
                    f[py:py + 3, px:px + 3] = (36, 148, 60)
    f[4:22, 8:120] = (216, 40, 48)                  # HUD: the health bar
    f[6:20, 10:10 + 100] = (250, 208, 40)
    f[6:20, VW - 92:VW - 8] = (24, 32, 56)          # a timer in the corner
    cx, cy, r = 213, 120, 40                         # a circle: it must stay round
    d = np.hypot(x - cx, y - cy)
    f[(d > r - 3) & (d < r)] = (255, 255, 255)
    for gx in range(0, VW, 16):                      # 1px grid: the nearest-neighbour pattern
        f[:, gx] = (f[:, gx] * 0.75)
    return f


def sample(f, u, v):
    """nearest-neighbour, like the GL_NEAREST upscale"""
    x = np.clip((u * VW).astype(int), 0, VW - 1)
    y = np.clip((v * VH).astype(int), 0, VH - 1)
    return f[y, x]


def blur(img, px=5):
    """the 9-tap smear the backdrop pass does, in source texels"""
    out = img * 0.28
    for dx, dy, w in ((5, 0, 0.14), (-5, 0, 0.14), (0, 5, 0.14), (0, -5, 0.14),
                      (6, 6, 0.08), (-6, 6, 0.08), (6, -6, 0.08), (-6, -6, 0.08)):
        out += np.roll(np.roll(img, dy, axis=0), dx, axis=1) * w
    return out


def present(f, old):
    """letterboxed presentation of the frame on the phone"""
    y, x = np.mgrid[0:WH, 0:WW]
    s = min(WW / VW, WH / VH)                        # 5.0 on this phone
    w, h = VW * s, VH * s
    x0, y0 = (WW - w) * 0.5, (WH - h) * 0.5
    if old:
        # the bug: the quad covers the view rect, but the uv is taken from the window
        out = np.zeros((WH, WW, 3))
        inside = (x >= x0) & (x < x0 + w) & (y >= y0) & (y < y0 + h)
        u = np.clip(x / WW, 0, 1)
        v = np.clip(y / WH, 0, 1)
        out[inside] = sample(f, u, v)[inside]        # u only spans 0.098..0.902: zoomed and cropped
    else:
        out = sample(blur(f), x / WW, y / WH) * 0.34  # the backdrop: smeared, stretched, dimmed
        inside = (x >= x0) & (x < x0 + w) & (y >= y0) & (y < y0 + h)
        u = np.clip((x - x0) / w, 0, 1)
        v = np.clip((y - y0) / h, 0, 1)
        out[inside] = sample(f, u, v)[inside]         # the whole view, undistorted
    return Image.fromarray(np.clip(out, 0, 255).astype(np.uint8))


def main():
    f = frame()
    Image.fromarray(f.astype(np.uint8)).save("tools/preview/out/present_frame.png")
    a, b = present(f, True), present(f, False)
    gap = 24
    sheet = Image.new("RGB", (WW, WH * 2 + gap), (16, 16, 18))
    sheet.paste(a, (0, 0))
    sheet.paste(b, (0, WH + gap))
    sheet = sheet.resize((WW // 2, (WH * 2 + gap) // 2), Image.LANCZOS)
    sheet.save("tools/preview/out/present_old_vs_new.png")
    print("wrote tools/preview/out/present_old_vs_new.png (old on top, fixed below)")


if __name__ == "__main__":
    main()
