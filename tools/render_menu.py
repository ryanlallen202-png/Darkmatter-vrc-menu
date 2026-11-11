#!/usr/bin/env python3
"""
Render the Dark Matter Quest menu mockup from the geometry in menu.cpp.

Every number below mirrors the constants in the source:
  kW 900, kH 700, kTabH 46, kTabY = kH/2-148, kContentTop = kTabY-kTabH/2-14
  kRowH 44, kRowW kW-30, kSliderW 640, kCheckSize 34
Palette values are the kBg/kPanel/... constants, converted 0-1 float to 0-255.
"""

from PIL import Image, ImageDraw, ImageFont

W, H = 1800, 1400          # 2x canvas for a crisp render
S = 2                       # canvas px -> image px

kW, kH = 900.0, 720.0
kTabH = 46.0
kTabY = kH / 2 - 190.0
kContentTop = kTabY - kTabH / 2 - 14.0
kRowH = 42.0
kRowW = kW - 30.0
kSliderW = 640.0
kCheckSize = 32.0

TABS = ["Player", "Exploits", "Visual", "Players", "Soundboard", "Text", "Misc", "Credits", "Auth"]

def c(r, g, b, a=255):
    return (int(r * 255), int(g * 255), int(b * 255), a)

kBg         = c(0.055, 0.055, 0.062)
kPanel      = c(0.085, 0.085, 0.095)
kPanelAlt   = c(0.105, 0.105, 0.118)
kTabActive  = c(0.145, 0.145, 0.160)
kTabIdle    = c(0.105, 0.105, 0.118)
kBtn        = c(0.115, 0.115, 0.128)
kCheckOn    = c(0.960, 0.960, 0.970)
kCheckOff   = c(0.055, 0.055, 0.062)
kText       = c(0.960, 0.960, 0.970)
kTextDim    = c(0.620, 0.620, 0.650)
kTitle      = (255, 255, 255, 255)
kSliderBg   = c(0.070, 0.070, 0.080)
kSliderFill = c(0.930, 0.930, 0.950)
kBorder     = c(0.230, 0.230, 0.255)

FB = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
FR = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"

def font(size, bold=True):
    return ImageFont.truetype(FB if bold else FR, int(size * S))

def to_px(x, y):
    """Canvas coords (centre origin, +y up) -> image coords (top-left, +y down)."""
    return (W / 2 + x * S, H / 2 - y * S)

img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
d = ImageDraw.Draw(img, "RGBA")

def rrect(cx, cy, w, h, fill, radius=10, outline=None, width=1):
    x0, y0 = to_px(cx - w / 2, cy + h / 2)
    x1, y1 = to_px(cx + w / 2, cy - h / 2)
    d.rounded_rectangle([x0, y0, x1, y1], radius=radius * S,
                        fill=fill, outline=outline, width=width * S)

def text(cx, cy, s, fnt, fill, anchor="mm"):
    x, y = to_px(cx, cy)
    d.text((x, y), s, font=fnt, fill=fill, anchor=anchor)

# ---------------------------------------------------------------- window
rrect(0, 0, kW, kH, kBg, radius=14, outline=kBorder, width=1)

# ---------------------------------------------------------------- header
text(-kW / 2 + 20 + 160, kH / 2 - 34, "Dark Matter  <3", font(30), kTitle, "mm")
text(-kW / 2 + 118, kH / 2 - 78, "FPS: 60.0", font(20), kText)

# dock button
rrect(-kW / 2 + 180, kH / 2 - 130, 330, 48, kBtn, radius=10, outline=kBorder, width=1)
text(-kW / 2 + 180, kH / 2 - 130, "Dock in Front of Face", font(20), kText)

# ---------------------------------------------------------------- tabs
tabW = kW / len(TABS)
for i, name in enumerate(TABS):
    x = -kW / 2 + tabW * (i + 0.5)
    active = (i == 0)
    rrect(x, kTabY, tabW - 6, kTabH, kTabActive if active else kTabIdle,
          radius=6, outline=kBorder if not active else None, width=1)
    text(x, kTabY, name, font(15), kText)

# ---------------------------------------------------------------- rows
rows = [
    ("header", "▾    Movement", None, None),
    ("slider", "Fly Speed", 15.000, 40.0),
    ("slider", "Flap Multiplier", 5.500, 20.0),
    ("toggle", "JFly", True, None),
    ("toggle", "Velocity Fly", False, None),
    ("toggle", "Platforms", False, None),
    ("toggle", "Noclip", False, None),
    ("toggle", "Punch Mod", False, None),
    ("toggle", "Low Gravity", False, None),
    ("toggle", "No Gravity", False, None),
    ("toggle", "Speed Hack", False, None),
]

y = kContentTop - kRowH / 2
for kind, label, val, vmax in rows:
    if kind == "header":
        rrect(0, y, kRowW, kRowH - 4, kPanelAlt, radius=8)
        text(-kRowW / 2 + 30, y, "▼", font(20), kTitle, "lm")
        text(-kRowW / 2 + 66, y + 1, label.replace("▾    ", ""), font(24), kTitle, "lm")
    elif kind == "slider":
        rrect(0, y, kRowW, kRowH - 4, kPanel, radius=8)
        trackX = -kRowW / 2 + 28
        rrect(trackX + kSliderW / 2, y, kSliderW, 26, kSliderBg, radius=13)
        frac = val / vmax
        fillW = max(4, kSliderW * frac)
        rrect(trackX + fillW / 2, y, fillW, 26, kSliderFill, radius=13)
        # knob: white pill at the fill edge
        rrect(trackX + fillW, y, 22, 40, (255, 255, 255, 255), radius=10)
        text(trackX + kSliderW / 2, y, f"{val:.3f}", font(26), kText)
        text(kRowW / 2 - 150, y, label, font(22), kText)
    else:
        rrect(0, y, kRowW, kRowH - 4, kPanel, radius=8)
        bx = -kRowW / 2 + 28
        rrect(bx, y, kCheckSize, kCheckSize, (245, 245, 250, 255) if val else kCheckOff,
              radius=6, outline=None if val else kBorder, width=2)
        if val:
            text(bx, y - 2, "✓", font(22), (20, 20, 26, 255))
        text(-kRowW / 2 + 66, y, label, font(22), kText, "lm")
    y -= kRowH

# ---------------------------------------------------------------- cursor
cx, cy = to_px(-kRowW / 2 + 232, kContentTop - kRowH * 2.42)
d.ellipse([cx - 9, cy - 9, cx + 9, cy + 9], fill=(255, 255, 255, 255))
d.ellipse([cx - 13, cy - 13, cx + 13, cy + 13], outline=(255, 255, 255, 120), width=2)

# ---------------------------------------------------------------- vignette bg
bg = Image.new("RGBA", (W, H), (14, 12, 18, 255))
# soft radial lift so the panel reads as floating in a dark room
glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
gd = ImageDraw.Draw(glow)
for r in range(700, 0, -14):
    a = int(26 * (1 - r / 700))
    gd.ellipse([W / 2 - r * 1.25, H / 2 - r, W / 2 + r * 1.25, H / 2 + r],
               fill=(86, 92, 128, a))
bg.alpha_composite(glow)
bg.alpha_composite(img)

out = "/home/user/Darkmatter-vrc-menu/docs/menu-mockup.png"
import os
os.makedirs(os.path.dirname(out), exist_ok=True)
bg.convert("RGB").save(out, quality=96)
print("wrote", out, bg.size)
