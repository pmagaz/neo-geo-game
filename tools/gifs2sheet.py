#!/usr/bin/env python3
"""Build a sprite sheet from animated GIFs, one animation per GIF.

Art often arrives as a separate animation per file rather than as a sheet,
and with far more frames than a 60 Hz game needs - a 44-frame run cycle
played one frame per game frame lasts nearly a second. So each GIF is
sampled down to the number of frames asked for, spread evenly across it.

The result is laid out the way sheet2neo.py expects: one animation per row,
separated by fully transparent gaps, with transparent gaps between frames.

    gifs2sheet.py -o sheet.png \\
        --anim walk=run.gif:8 --anim attack=attack.gif:8

Each --anim is NAME=FILE:FRAMES. Frames are taken evenly across the file;
use :FIRST-LAST:FRAMES to sample from part of it instead, which is how to
skip a neutral pose sitting at the front of a cycle.

--height scales the result, the way prep_sheet.py does for a hand-laid
sheet, so art drawn at whatever size it arrived at comes out the size the
game wants.
"""

import argparse

from PIL import Image, ImageSequence

GAP = 10        # transparent pixels between frames and between rows


def key_background(im, tolerance):
    """Make the flat backdrop transparent.

    Animation exported as GIF usually has no alpha at all - the backdrop is
    just a colour, and every frame is fully opaque. The colour is taken from
    a corner and flooded inwards from the edges, so a patch of the same
    colour inside the character is left alone.
    """
    w, h = im.size
    px = im.load()
    bg = px[0, 0][:3]

    def matches(p):
        # Every channel has to be close, not the total.
        #
        # Summing the differences makes unrelated colours look near: this
        # character's skin is (174,135,102) against a (91,112,117) backdrop,
        # which sums to only 121 - so a summed tolerance of 48 swallowed every
        # face, arm and leg while leaving the clothes behind. Per channel the
        # same pair is 83 apart and never matches.
        return (abs(p[0] - bg[0]) <= tolerance
                and abs(p[1] - bg[1]) <= tolerance
                and abs(p[2] - bg[2]) <= tolerance)

    seen = bytearray(w * h)
    stack = [(x, 0) for x in range(w)] + [(x, h - 1) for x in range(w)]
    stack += [(0, y) for y in range(h)] + [(w - 1, y) for y in range(h)]
    while stack:
        x, y = stack.pop()
        if not (0 <= x < w and 0 <= y < h) or seen[y * w + x]:
            continue
        if not matches(px[x, y]):
            continue
        seen[y * w + x] = 1
        stack.append((x + 1, y))
        stack.append((x - 1, y))
        stack.append((x, y + 1))
        stack.append((x, y - 1))

    for y in range(h):
        base = y * w
        for x in range(w):
            if seen[base + x]:
                px[x, y] = (0, 0, 0, 0)
    return im


def load_frames(path, tolerance):
    """Every frame of a GIF, composited, in RGBA, with the backdrop removed."""
    im = Image.open(path)
    out = []
    for f in ImageSequence.Iterator(im):
        f = f.convert("RGBA")
        if tolerance >= 0:
            f = key_background(f, tolerance)
        out.append(f)
    return out


def pick(frames, count, first=None, last=None):
    """`count` frames spread evenly across the range."""
    lo = 0 if first is None else first
    hi = (len(frames) - 1) if last is None else last
    lo = max(0, min(lo, len(frames) - 1))
    hi = max(lo, min(hi, len(frames) - 1))
    if count == 1:
        return [frames[lo]]
    span = hi - lo
    return [frames[lo + round(i * span / (count - 1))] for i in range(count)]


def scale_frames(rows, height):
    """Scale every frame so the tallest one comes out `height` pixels.

    One factor for all of them, and applied before the sheet is laid out:
    sheet2neo.py fits a single bounding box to every frame of every animation,
    so scaling them individually would make the character change size when
    the animation changed.

    The alpha is thresholded afterwards because LANCZOS feathers the edges it
    shrinks, and sheet2neo.py counts any pixel above zero alpha as part of the
    character. A feathered edge would push the bounding box a pixel or two
    past the target and cost a whole extra tile row - which is 16 pixels of
    sprite height and a row of tiles per frame in the C ROM, for nothing.
    """
    tallest = max(f.height for _, _, _, fs in rows for f in fs)
    scale = height / tallest

    for _, _, _, frames in rows:
        for i, f in enumerate(frames):
            size = (max(1, round(f.width * scale)),
                    max(1, round(f.height * scale)))
            g = f.resize(size, Image.LANCZOS)
            px = g.load()
            for y in range(g.height):
                for x in range(g.width):
                    r, gr, b, a = px[x, y]
                    px[x, y] = (r, gr, b, 255) if a >= 128 else (0, 0, 0, 0)
            frames[i] = g

    return tallest, scale


def parse_anim(spec):
    """NAME=FILE:FRAMES or NAME=FILE:FIRST-LAST:FRAMES"""
    name, _, rest = spec.partition("=")
    if not name or not rest:
        raise SystemExit(f"error: bad --anim {spec!r}")
    parts = rest.split(":")
    if len(parts) == 2:
        return name, parts[0], None, None, int(parts[1])
    if len(parts) == 3:
        first, last = parts[1].split("-")
        return name, parts[0], int(first), int(last), int(parts[2])
    raise SystemExit(f"error: bad --anim {spec!r}, want NAME=FILE[:A-B]:COUNT")


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("-o", "--output", required=True, help="PNG sheet to write")
    p.add_argument("--anim", action="append", default=[], required=True,
                   metavar="NAME=FILE[:A-B]:COUNT", help="repeatable")
    p.add_argument("--bg-tolerance", type=int, default=24,
                   help="how far each channel may differ from the corner "
                        "colour and still count as backdrop; -1 keeps it")
    p.add_argument("--height", type=int, metavar="PX",
                   help="scale so the tallest frame is this many pixels")
    args = p.parse_args()

    rows = []
    for spec in args.anim:
        name, path, first, last, count = parse_anim(spec)
        frames = load_frames(path, args.bg_tolerance)
        chosen = pick(frames, count, first, last)
        # Trim each frame to what it actually draws, so the gaps between them
        # are real gaps: a frame padded with transparent pixels out to the
        # GIF's canvas would run into its neighbour and be read as one frame.
        trimmed = []
        for f in chosen:
            bb = f.getbbox()
            trimmed.append(f.crop(bb) if bb else f)
        rows.append((name, path, len(frames), trimmed))

    if args.height:
        tallest, scale = scale_frames(rows, args.height)
        print(f"  scaled by {scale:.3f}: tallest frame {tallest} -> "
              f"{args.height} px")

    cell_w = max(f.width for _, _, _, fs in rows for f in fs) + GAP
    cell_h = max(f.height for _, _, _, fs in rows for f in fs) + GAP
    width = cell_w * max(len(fs) for _, _, _, fs in rows) + GAP
    height = cell_h * len(rows) + GAP

    sheet = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    for r, (name, path, total, frames) in enumerate(rows):
        for i, f in enumerate(frames):
            # Centre each frame in its cell horizontally and sit it on the
            # cell's bottom, so the feet line up across the animation.
            x = GAP + i * cell_w + (cell_w - GAP - f.width) // 2
            y = GAP + r * cell_h + (cell_h - GAP - f.height)
            sheet.paste(f, (x, y), f)
        print(f"  row {r}: {name:8s} {len(frames)} of {total} frames "
              f"from {path}")

    sheet.save(args.output)
    print(f"{args.output}: {width}x{height}, {len(rows)} rows, "
          f"cell {cell_w - GAP}x{cell_h - GAP}")


if __name__ == "__main__":
    main()
