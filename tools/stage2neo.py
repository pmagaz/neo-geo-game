#!/usr/bin/env python3
"""Cut a painted stage into its three layers and emit them as Neo Geo tiles.

The companion to make_stage.py, which draws a stage in code. This one takes a
stage that was painted or generated as a single image and turns it into the
same three GIFs and the same header, so the game cannot tell which it got.

The source is one image holding three panels stacked top to bottom, separated
by bands of solid magenta running the full width:

    sky        opaque, never scrolls
    magenta    gutter
    hills      transparent above the rooflines, opaque along its bottom edge
    magenta    gutter
    ground     opaque, the floor the characters walk on

Magenta does double duty - it separates the panels and it is the keying colour
inside the hills panel - so the gutter above the hills and the sky showing
through above its rooflines merge into one band. That is harmless: everything
in it is transparent either way, and the hills panel is taken to start at the
first row that is not entirely magenta, which is the tip of the tallest roof.

The panels are found rather than measured out, because no image generator
respects exact pixel offsets. What it does have to get right is the order and
the full-width gutters.

The hard part is the palette. All three layers share ONE fifteen-colour
palette on this hardware, so they are quantised together rather than
separately: three independent quantisations produce three sets of fifteen
colours that have to be crushed into fifteen afterwards, and everything comes
out muddy.
"""

import argparse
import math
import sys

from neogeo_color import snap, to_color_word
from PIL import Image

# Where each layer belongs on screen, and how deep the floor is. Imported so
# that a converted stage and a drawn one cannot disagree about the geometry.
from make_stage import (W, SKY_Y, SKY_H, HILLS_Y, HILLS_H, GROUND_Y, GROUND_H,
                        FLOOR_DEPTH, FLOOR_BACK)

LAYERS = [
    # name, on-screen y, height, is the magenta meant to be there
    ("sky", SKY_Y, SKY_H, False),
    ("hills", HILLS_Y, HILLS_H, True),
    ("ground", GROUND_Y, GROUND_H, False),
]


def is_magenta(p, tolerance):
    """Near enough to #ff00ff to be the keying colour.

    Generous on red and blue and strict on green, because that is the axis
    that separates magenta from everything a night scene actually contains.
    Compression and resampling drag the corners around a long way.
    """
    r, g, b = p[:3]
    return r >= 255 - tolerance and b >= 255 - tolerance and g <= tolerance


def find_panels(im, tolerance):
    """The three panels, as (top, bottom) row pairs of the source image."""
    w, h = im.size
    px = im.load()

    full = []
    for y in range(h):
        n = sum(1 for x in range(w) if is_magenta(px[x, y], tolerance))
        full.append(n > w * 0.97)

    runs, start = [], None
    for y, f in enumerate(full):
        if f and start is None:
            start = y
        elif not f and start is not None:
            runs.append((start, y - 1))
            start = None
    if start is not None:
        runs.append((start, h - 1))

    # Only the gutters span the full width for any length; a roofline or a
    # star breaks a row long before this.
    runs = [r for r in runs if r[1] - r[0] + 1 >= 4]
    if len(runs) != 2:
        raise SystemExit(
            f"error: found {len(runs)} full-width magenta bands, expected 2.\n"
            f"       bands at {runs}\n"
            "       The source needs exactly two magenta gutters: one between\n"
            "       the sky and the rooftops, one between the rooftops and the\n"
            "       ground. Try --magenta-tolerance if the colour is off.")

    (g1a, g1b), (g2a, g2b) = runs
    return [(0, g1a - 1), (g1b + 1, g2a - 1), (g2b + 1, h - 1)]


def grow_mask(mask, pixels):
    """Eat `pixels` further into the art all round the keyed-out areas.

    A hard magenta edge never survives compression or resampling intact: it
    leaves a halo of colours that are part magenta and part artwork, too far
    from #ff00ff to be keyed and too purple to belong in the scene. Left
    alone they are the most saturated thing in a night scene, so the
    quantiser spends real palette entries on them.

    Growing the transparent side by a pixel or two removes the halo. It costs
    a sliver of the artwork, but the source is several times the final size,
    so a couple of pixels here is a fraction of one on the cartridge.
    """
    if pixels <= 0:
        return mask

    w, h = mask.size
    src = mask.load()
    out = Image.new("L", mask.size, 255)
    dst = out.load()

    for y in range(h):
        for x in range(w):
            if not src[x, y]:
                for dy in range(-pixels, pixels + 1):
                    for dx in range(-pixels, pixels + 1):
                        nx, ny = x + dx, y + dy
                        if 0 <= nx < w and 0 <= ny < h:
                            dst[nx, ny] = 0
    return out


def cut_layer(im, top, bottom, height, tolerance, grow):
    """One panel, resampled to 320 x height, as RGB plus a transparency mask.

    LANCZOS rather than nearest: the source is a large image of pixel-art-
    looking blocks whose grid does not line up with ours, and nearest picks
    one arbitrary pixel out of each block, which shimmers along every edge.
    The colours are snapped and quantised afterwards anyway, so the softness
    LANCZOS introduces does not survive to the cartridge.

    The mask is taken before resampling. Resampling the magenta would blend
    it with the art at every edge and leave a fringe of colours that are
    neither, which then eat palette entries.
    """
    panel = im.crop((0, top, im.width, bottom + 1))

    mask = Image.new("L", panel.size, 255)
    mpx, ppx = mask.load(), panel.load()
    for y in range(panel.height):
        for x in range(panel.width):
            if is_magenta(ppx[x, y], tolerance):
                mpx[x, y] = 0

    mask = grow_mask(mask, grow)

    rgb = panel.resize((W, height), Image.LANCZOS)
    # NEAREST for the mask, so its edges stay hard and no pixel ends up half
    # transparent - which this hardware cannot draw in any case.
    mask = mask.resize((W, height), Image.NEAREST)
    return rgb, mask


def build_palette(layers, colors, weight):
    """One palette for every layer at once.

    Only opaque pixels are offered to the quantiser, and they are snapped to
    the hardware's colour space first, so the fifteen colours chosen are ones
    the console can actually display.

    Each distinct colour is then offered `count ** weight` times rather than
    once per pixel. Straight pixel counts let the largest flat area decide
    almost everything: on a night scene the sky is most of the image, so a
    median cut spends four of its fifteen entries splitting one dark blue into
    four indistinguishable dark blues, and throws away the lantern flames
    entirely because they are only a few hundred pixels. Taking a fractional
    power keeps the big areas important without letting them crowd out every
    small thing that gives the scene its colour.
    """
    counts = {}
    for rgb, mask in layers:
        rpx, mpx = rgb.load(), mask.load()
        for y in range(rgb.height):
            for x in range(rgb.width):
                if mpx[x, y]:
                    c = snap(rpx[x, y][:3])
                    counts[c] = counts.get(c, 0) + 1

    if not counts:
        raise SystemExit("error: every pixel was keyed out as magenta")

    pixels = []
    for c, n in counts.items():
        pixels += [c] * max(1, int(n ** weight))

    # A roughly square probe image; quantisers behave oddly on a single row.
    side = int(math.ceil(math.sqrt(len(pixels))))
    probe = Image.new("RGB", (side, side), pixels[0])
    probe.putdata(pixels + [pixels[0]] * (side * side - len(pixels)))

    quant = probe.quantize(colors=colors, method=Image.MEDIANCUT)
    pal = quant.getpalette()[: colors * 3]
    pal += [0] * (colors * 3 - len(pal))
    return [tuple(pal[i * 3: i * 3 + 3]) for i in range(colors)]


def index_layer(rgb, mask, palette):
    """Map a layer onto the shared palette. Index 0 is transparent."""
    out = Image.new("P", rgb.size, 0)
    out.putpalette([0, 0, 0] + [c for rgb_ in palette for c in rgb_]
                   + [0, 0, 0] * (15 - len(palette)))

    cache = {}
    rpx, mpx, opx = rgb.load(), mask.load(), out.load()
    for y in range(rgb.height):
        for x in range(rgb.width):
            if not mpx[x, y]:
                continue                        # stays 0, transparent
            c = snap(rpx[x, y][:3])
            i = cache.get(c)
            if i is None:
                i = min(range(len(palette)),
                        key=lambda k: sum((a - b) ** 2
                                          for a, b in zip(c, palette[k])))
                cache[c] = i
            opx[x, y] = i + 1                   # 0 is reserved
    return out


def seam_error(layer):
    """How badly the left and right edges disagree, 0 to 1.

    A scrolling layer repeats every 320 pixels, so a mismatch here is a seam
    crossing the screen every few seconds. Generated art almost never joins
    up, and it is much cheaper to hear about it now than to notice it later.
    """
    px = layer.load()
    differing = sum(1 for y in range(layer.height)
                    if px[0, y] != px[layer.width - 1, y])
    return differing / layer.height


def write_header(path, name, floor_depth, floor_back, palette):
    up = name.upper()
    words = [0x8000] + [to_color_word(*c) for c in palette]
    words += [0x8000] * (16 - len(words))

    with open(path, "w") as f:
        f.write("/* Generated by tools/stage2neo.py - do not edit. */\n")
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
                "   0..FLOOR_DEPTH-1. */\n")
        f.write(f"#define {up}_FLOOR_TOP {GROUND_Y}\n")
        f.write(f"#define {up}_FLOOR_DEPTH {floor_depth}\n")
        f.write(f"#define {up}_FLOOR_BACK {floor_back}\n\n")
        for lname, y, h, _ in LAYERS:
            ln = f"{up}_{lname.upper()}"
            f.write(f"#define {ln}_Y {y}\n")
            f.write(f"#define {ln}_ROWS {h // 16}\n")
            f.write(f"#define {ln}_TILE_COUNT {(W // 16) * (h // 16)}\n")
        f.write("\n/* 16 colours, index 0 transparent. */\n")
        f.write(f"static const u16 {name}_palette[16] = {{\n")
        for i in range(0, 16, 4):
            f.write("    " + ", ".join(f"0x{w:04x}" for w in words[i:i + 4]) + ",\n")
        f.write("};\n\n#endif\n")


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("source", help="the painted stage, three panels in one image")
    p.add_argument("--outdir", default="assets/images/stages")
    p.add_argument("--header", required=True, help="C header to write")
    p.add_argument("--name", default="stage", help="identifier prefix")
    p.add_argument("--colors", type=int, default=15,
                   help="colours besides transparent, shared by all three "
                        "layers (max 15)")
    p.add_argument("--magenta-tolerance", type=int, default=70,
                   help="how far a pixel may stray from #ff00ff and still be "
                        "keyed out")
    p.add_argument("--key-grow", type=int, default=2, metavar="PX",
                   help="how far to eat into the art around keyed-out areas, "
                        "to remove the halo a compressed magenta edge leaves")
    p.add_argument("--weight", type=float, default=0.25, metavar="POWER",
                   help="how much a colour's pixel count counts towards "
                        "choosing the palette: 1 is by area, 0 gives every "
                        "distinct colour equal say")
    p.add_argument("--floor-depth", type=int, default=FLOOR_DEPTH)
    p.add_argument("--floor-back", type=int, default=FLOOR_BACK)
    args = p.parse_args()

    if not 1 <= args.colors <= 15:
        raise SystemExit("error: --colors must be between 1 and 15")

    im = Image.open(args.source).convert("RGB")
    panels = find_panels(im, args.magenta_tolerance)
    print(f"{args.source}: {im.width}x{im.height}")

    cut = []
    for (top, bottom), (lname, _, h, _) in zip(panels, LAYERS):
        print(f"  {lname:6s} source rows {top:4d}-{bottom:<4d} "
              f"({bottom - top + 1:4d} px) -> {W}x{h}")
        cut.append(cut_layer(im, top, bottom, h, args.magenta_tolerance,
                             args.key_grow))

    palette = build_palette(cut, args.colors, args.weight)
    print(f"  quantised all three layers together to {len(palette)} colours")

    warnings = 0
    for (rgb, mask), (lname, _, h, may_key) in zip(cut, LAYERS):
        layer = index_layer(rgb, mask, palette)

        # optimize=False, or Pillow drops the unused index 0 and shifts every
        # colour down one - and index 0 is transparent on this hardware.
        path = f"{args.outdir}/{args.name}-{lname}.gif"
        layer.save(path, transparency=0, optimize=False)

        mpx = mask.load()
        keyed = sum(1 for y in range(h) for x in range(W) if not mpx[x, y])
        note = ""
        if keyed and not may_key:
            note = f"  ** {keyed} transparent px: this layer must be opaque"
            warnings += 1
        elif may_key:
            bottom_gap = sum(1 for x in range(W) if not mpx[x, h - 1])
            if bottom_gap:
                note = (f"  ** {bottom_gap} transparent px along the bottom "
                        "edge: a gap will show between this and the ground")
                warnings += 1

        seam = seam_error(layer)
        if seam > 0.25 and lname != "sky":
            note += f"  ** left and right edges differ on {seam:.0%} of rows"
            warnings += 1

        print(f"  {path}{note}")

    write_header(args.header, args.name, args.floor_depth, args.floor_back,
                 palette)

    if warnings:
        print(f"\n{warnings} thing(s) worth looking at above. The stage will "
              "still build.", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
