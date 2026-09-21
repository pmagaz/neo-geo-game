#!/usr/bin/env python3
"""Draw the stage as parallax layers and emit them as Neo Geo sprite tiles.

The Neo Geo has no background layer: everything on screen is either a sprite
or the fix layer, and the fix layer always draws on top. So the stage is made
of sprites with lower numbers than the character, since higher-numbered
sprites are drawn in front.

Three layers, each 320 pixels wide, scrolled at its own rate:

    sky     stars and the moon, never moves
    hills   the ridges, scrolled slowly so they read as distant
    ground  the floor the characters walk on, scrolled with the camera

The layers are stacked rather than overlapped, and that is deliberate. The
characters move through a band of depth on the floor, and the hardware only
draws 96 sprites on any one scanline - so if two parallax layers reached
down into that band, their columns would spend the budget the characters
need. The sky stops where the hills start and the hills stop where the floor
starts, which leaves the whole floor band paid for by one layer.

It also keeps the floor moving at one speed. The hills scroll at a quarter
of the camera's rate; a floor that was partly hills would slide at different
speeds at the back and the front of the same band.

The scrolling layers repeat every 320 pixels, so they have to be seamless:
the right edge must join the left edge exactly. The ridges are therefore
built from sine waves with a whole number of cycles across the width rather
than from a random walk, and anything scattered near an edge is drawn again
on the other side.

All three layers share one 16-colour palette. Index 0 is transparent on this
hardware, so it is never used for a visible pixel.
"""

import argparse
import math
import random

from neogeo_color import to_color_word
from PIL import Image, ImageDraw

W = 320

# Where each layer sits on screen and how tall it is, in whole tiles of 16.
# The hills are transparent above their ridges and opaque below them, so the
# sky only has to reach as far as the highest ridge; each layer is otherwise
# responsible for its own slice of the screen and nothing else.
#
# The floor starts at 128 because the HUD owns lines 0-31 and the character is
# 96 px tall: 32 + 96 is the first line its feet can reach without its head
# crossing into the HUD.
SKY_Y, SKY_H = 0, 128
HILLS_Y, HILLS_H = 64, 64
GROUND_Y, GROUND_H = 128, 96

# Index 0 must stay transparent, so the stage draws with indices 1-15.
PALETTE = [
    (0, 0, 0),          # 0  transparent, never drawn
    (16, 16, 40),       # 1  sky, darkest
    (30, 28, 60),       # 2  sky
    (48, 44, 84),       # 3  sky
    (78, 66, 104),      # 4  sky at the horizon
    (232, 224, 200),    # 5  moon and stars
    (12, 12, 28),       # 6  far ridge
    (26, 26, 52),       # 7  far ridge, lit edge
    (20, 30, 40),       # 8  near ridge
    (34, 24, 20),       # 9  ground, deepest
    (54, 40, 30),       # 10 ground
    (74, 56, 40),       # 11 ground, upper
    (96, 74, 52),       # 12 ground, lit edge
    (18, 14, 12),       # 13 rock shadow
    (44, 54, 38),       # 14 scrub
    (132, 104, 70),     # 15 highlight
]


def new_layer(height, fill=0):
    im = Image.new("P", (W, height), fill)
    im.putpalette([c for rgb in PALETTE for c in rgb])
    return im


def wave(x, terms):
    """A periodic height at x: whole cycles across W, so it always joins up."""
    return sum(a * math.sin(2 * math.pi * f * x / W + p) for f, a, p in terms)


def ridge(im, terms, base_y, color):
    """Fill below a seamless skyline with `color`."""
    d = ImageDraw.Draw(im)
    pts = [(x, base_y + wave(x, terms)) for x in range(W + 1)]
    d.polygon(pts + [(W, im.height), (0, im.height)], fill=color)


def dither_join(im, y, height, upper, lower):
    """Blend two flat bands into each other across `height` rows at `y`.

    Fifteen colours do not stretch to a smooth gradient, so the boundaries
    between flat bands are stippled instead. This is how the era's artists
    faked extra shades, and it reads far better than a hard line.
    """
    for row in range(height):
        yy = y - height // 2 + row
        if not 0 <= yy < im.height:
            continue
        density = row / (height - 1)
        for x in range(W):
            checker = (x + yy) % 2 == 0
            if density > 0.66:
                im.putpixel((x, yy), lower)
            elif density > 0.33:
                im.putpixel((x, yy), lower if checker else upper)
            elif density > 0.1 and checker and (x // 2 + yy // 2) % 2 == 0:
                im.putpixel((x, yy), lower)


def draw_sky(args):
    im = new_layer(SKY_H, 1)
    d = ImageDraw.Draw(im)
    rng = random.Random(args.seed)

    # Banded, getting brighter towards the horizon, with the joins stippled.
    bands = [(0, 0.34, 1), (0.34, 0.62, 2), (0.62, 0.86, 3), (0.86, 1.0, 4)]
    for lo, hi, color in bands:
        d.rectangle([0, int(SKY_H * lo), W, int(SKY_H * hi)], fill=color)

    for i in range(len(bands) - 1):
        dither_join(im, int(SKY_H * bands[i][1]), 12,
                    bands[i][2], bands[i + 1][2])

    # Stars, thinning out towards the brighter horizon.
    for _ in range(110):
        x, y = rng.randrange(W), rng.randrange(int(SKY_H * 0.8))
        if rng.random() < 1.0 - y / (SKY_H * 0.8):
            im.putpixel((x, y), 5)

    # Moon, with a bite taken out of it to make a crescent.
    mx, my, r = 254, 34, 11
    d.ellipse([mx - r, my - r, mx + r, my + r], fill=5)
    d.ellipse([mx - r + 6, my - r - 2, mx + r + 6, my + r - 2], fill=2)
    return im


def draw_hills(args):
    """Ridges drawn on transparent, so the sky shows through above them.

    The baselines are set so the nearest ridge is opaque all the way to the
    bottom edge of the layer. The floor begins there, and a transparent gap
    between the two would show the backdrop colour as a seam across the
    screen.
    """
    im = new_layer(HILLS_H, 0)
    ridge(im, [(1, 6, 0.0), (2, 3, 1.1), (3, 2, 2.3)], 27, 7)
    ridge(im, [(1, 5, 2.0), (3, 3, 0.4), (5, 2, 1.7)], 40, 6)
    ridge(im, [(2, 4, 1.0), (3, 2, 2.9), (7, 1, 0.2)], 51, 8)
    return im


def draw_ground(args):
    """The floor plane, seen at a shallow angle.

    Three regions down the layer. The first `--floor-back` rows are floor
    behind the point a character may stand: without them the back row of
    characters has its feet on the layer's top edge with the hills directly
    behind, and reads as standing on the horizon rather than on the ground.
    Then the walkable band, up to `--floor-depth`, one row per unit of depth.
    Then the apron, which nothing stands on either but which stops the floor
    ending in mid-air at the bottom of the screen.

    The depth is painted in, not projected. Nothing here may scale - the
    layer is drawn once and scrolled, and the characters keep one size at
    every depth by design - so the only cues available are the banding down
    the layer and the texture coarsening towards the viewer.
    """
    im = new_layer(GROUND_H, 0)
    d = ImageDraw.Draw(im)
    rng = random.Random(args.seed + 3)
    depth = args.floor_depth
    back = args.floor_back

    # Where the floor meets the ridges behind it: a dark seam under a lit lip,
    # so the join reads as a step up rather than as a change of colour.
    d.rectangle([0, 0, W, 3], fill=13)
    d.rectangle([0, 4, W, 6], fill=12)

    # The floor behind the walk limit, in the darkest ground colour. Distance
    # reading as shadow is the cue, so there is no drawn line at the limit
    # itself - a hard edge there would look like a wall the characters stand
    # in front of rather than floor they cannot reach.
    d.rectangle([0, 7, W, back], fill=9)

    # Three bands across the walkable depth, darkest at the back. Mid-toned
    # throughout rather than running dark to light, because a character and
    # its shadow have to read against the floor at every depth - a floor that
    # went black at one end would swallow them there.
    span = depth - back
    bands = [(0.00, 0.34, 10), (0.34, 0.68, 11), (0.68, 1.00, 12)]
    for lo, hi, color in bands:
        d.rectangle([0, back + int(span * lo), W, back + int(span * hi)],
                    fill=color)
    dither_join(im, back, 8, 9, bands[0][2])
    for i in range(len(bands) - 1):
        dither_join(im, back + int(span * bands[i][1]), 8,
                    bands[i][2], bands[i + 1][2])

    # Stones and scrub. Anything crossing an edge is drawn on the other side
    # too, so the layer still joins up where it repeats.
    def blot(x, y, w, h, color):
        d.rectangle([x, y, x + w, y + h], fill=color)
        if x + w >= W:
            d.rectangle([x - W, y, x - W + w, y + h], fill=color)

    # Coarser towards the front: one stone covers more pixels when it is
    # nearer, and that change down the layer is most of what makes it read as
    # a floor receding rather than as a striped wall.
    for _ in range(150):
        y = rng.randrange(back + 2, depth)
        near = (y - back) / span
        size = 1 + int(near * 2.4)
        blot(rng.randrange(W), y, rng.randrange(1, size + 1), max(1, size - 1),
             rng.choice([13, 13, 9, 15]))

    # A little texture on the floor behind the walk limit too, finer and
    # sparser. Left plain it reads as a flat band rather than as ground.
    for _ in range(30):
        blot(rng.randrange(W), rng.randrange(8, max(9, back)), 1, 1,
             rng.choice([13, 10]))

    # Scrub along the back edge, where the floor meets the ridges.
    for _ in range(34):
        blot(rng.randrange(W), rng.randrange(7, 12),
             rng.choice([1, 2]), 1, 14)

    # The apron. Darker than the band above it, so the front edge of the
    # walkable floor is visible - a player needs to see where the floor stops
    # before walking into it.
    d.rectangle([0, depth, W, GROUND_H], fill=9)
    d.rectangle([0, depth, W, depth + 1], fill=13)
    for _ in range(44):
        blot(rng.randrange(W), rng.randrange(depth + 3, GROUND_H - 2),
             rng.choice([1, 2, 3]), rng.choice([1, 2]),
             rng.choice([13, 9, 10]))
    return im


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--outdir", default="assets", help="where to write the GIFs")
    p.add_argument("--header", required=True, help="C header to write")
    p.add_argument("--name", default="stage", help="identifier prefix")
    p.add_argument("--floor-depth", type=int, default=72, metavar="ROWS",
                   help="scanlines of depth the floor spans, measured down "
                        "from the top of the ground layer")
    p.add_argument("--floor-back", type=int, default=16, metavar="ROWS",
                   help="floor behind the furthest a character may stand, so "
                        "the back row has ground behind its feet")
    p.add_argument("--seed", type=int, default=7, help="scenery random seed")
    args = p.parse_args()

    # The band has to leave room for an apron in front of it, or the floor
    # ends at the bottom edge of the screen and the front row of characters
    # stands on nothing.
    if not 16 <= args.floor_depth < GROUND_H:
        raise SystemExit(f"error: --floor-depth must be between 16 and "
                         f"{GROUND_H - 1}, the ground layer being "
                         f"{GROUND_H} px tall")
    # 16 rows of walkable band is already very shallow; less than that is
    # not a beat-'em-up floor at all.
    if not 8 <= args.floor_back <= args.floor_depth - 16:
        raise SystemExit(f"error: --floor-back must be between 8 and "
                         f"{args.floor_depth - 16}, leaving at least 16 rows "
                         f"of a {args.floor_depth}-row floor to walk on")

    layers = [
        ("sky", draw_sky(args), SKY_Y, SKY_H),
        ("hills", draw_hills(args), HILLS_Y, HILLS_H),
        ("ground", draw_ground(args), GROUND_Y, GROUND_H),
    ]

    for lname, im, _, _ in layers:
        path = f"{args.outdir}/{args.name}-{lname}.gif"
        # optimize=False matters: Pillow otherwise drops the unused index 0
        # and shifts every colour down a place. Index 0 is transparent on this
        # hardware, so the whole layer would come out wrong.
        im.save(path, transparency=0, optimize=False)
        print(f"{path}: {W}x{im.height} px = {W // 16}x{im.height // 16} tiles")

    write_header(args, layers)


def write_header(args, layers):
    name, up = args.name, args.name.upper()
    words = [0x8000 if i == 0 else to_color_word(*PALETTE[i]) for i in range(16)]

    with open(args.header, "w") as f:
        f.write("/* Generated by tools/make_stage.py - do not edit. */\n")
        f.write(f"#ifndef {up}_H\n#define {up}_H\n\n")
        f.write(f"#define {up}_COLS {W // 16}\n\n")
        f.write("/* The floor plane: screen y of depth 0, and how many\n"
                "   scanlines of depth there are. Feet at depth z land on\n"
                f"   screen line {up}_FLOOR_TOP + z.\n"
                "\n"
                "   FLOOR_BACK is how much of that floor sits behind the\n"
                "   furthest a character may stand, so the back row has\n"
                "   ground behind its feet instead of the horizon. Depth is\n"
                "   therefore clamped to FLOOR_BACK..FLOOR_DEPTH-1, not to\n"
                f"   0..FLOOR_DEPTH-1. */\n")
        f.write(f"#define {up}_FLOOR_TOP {GROUND_Y}\n")
        f.write(f"#define {up}_FLOOR_DEPTH {args.floor_depth}\n")
        f.write(f"#define {up}_FLOOR_BACK {args.floor_back}\n\n")
        for lname, im, y, h in layers:
            ln = f"{up}_{lname.upper()}"
            f.write(f"#define {ln}_Y {y}\n")
            f.write(f"#define {ln}_ROWS {h // 16}\n")
            f.write(f"#define {ln}_TILE_COUNT {(W // 16) * (h // 16)}\n")
        f.write("\n/* 16 colours, index 0 transparent. */\n")
        f.write(f"static const u16 {name}_palette[16] = {{\n")
        for i in range(0, 16, 4):
            f.write("    " + ", ".join(f"0x{w:04x}" for w in words[i:i + 4]) + ",\n")
        f.write("};\n\n#endif\n")


if __name__ == "__main__":
    main()
