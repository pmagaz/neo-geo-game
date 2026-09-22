# Art asset specification

What the game needs from a piece of art, why, and how to ask an image
generator for it.

Every number here is what the code uses today. The sizes the game works in are
generated into `assets/images/sprites/hero.h` and
`assets/images/stages/stage.h`, and the build reads them from there — so those
two headers are the truth and this document is a copy of it. It has gone stale
once already, describing a stage layout two rewrites out of date, so **if a
figure here matters to you, check it against the header** before drawing to
it. If you change a size, change it in the makefile.

---

## 1. What the hardware fixes

These are not preferences. The console cannot do otherwise.

| | |
|---|---|
| Screen | 320 × 224 pixels |
| Safe area | Avoid the leftmost and rightmost 8 pixels, and the top and bottom 16 lines — they fall outside a real TV's picture |
| Sprite tile | 16 × 16 pixels |
| Text tile | 8 × 8 pixels |
| Colours per palette | **15, plus transparent** |
| Palettes on screen | 256, but one sprite uses exactly one |
| Sprites per scanline | **96** |
| Sprites per frame | 381 |

Two consequences worth understanding before drawing anything:

**A sprite is a vertical strip, not a rectangle.** It is 16 pixels wide and up
to 32 tiles tall. A character 64 pixels wide is therefore four sprites side by
side, and it costs four of the 96 sprites available on any line it touches.
Width is expensive; height is nearly free.

**Transparency is a colour.** Index 0 of every palette is transparent and
cannot be drawn with, which is why the limit is 15 colours and not 16.

---

## 2. Sizes the game uses now

### The character

| | |
|---|---|
| On screen | **56 × 96** px of art, in a 4 × 6 tile cell |
| Animations | walk 8 frames, attack 8, jump 4, hurt 2 |
| Sprites used | **7** — four for the body, three for its shadow |
| Feet stand on | screen line 128 + depth, so anywhere from 144 to 199 |

**96 pixels tall is not a preference.** The floor starts at line 128 and the
HUD owns lines 0 – 31, so 128 − 32 is the tallest a character can be and still
clear the HUD while standing at the back of the floor. It is also exactly six
tiles, so no transparent row is carried around costing sprite budget.

The source sheet does **not** have to be this size. Both `tools/gifs2sheet.py`
and `tools/prep_sheet.py` take `--height`, and scale whatever you supply so
the tallest frame becomes that many pixels — every frame by the same factor,
so the animation keeps its proportions and the character does not change size
between animations.

Drawing larger than the target and letting the build scale down is fine and
usually looks better than drawing at 96 px directly. Two to three times the
final size is a good range; beyond that, detail is lost in the reduction
anyway.

There is no hit-reaction art yet: the hurt animation borrows two frames of the
attack. A real two- or three-frame recoil is the most useful thing that could
be added to this character.

### The stage

Three layers, each **exactly 320 pixels wide** — one screen — stacked rather
than overlapped, and scrolled at different rates.

| Layer | Image size | Covers screen lines | Scrolls | Must tile | Transparency |
|---|---|---|---|---|---|
| Sky | **320 × 128** | 0 – 127 | never | no | none: opaque everywhere |
| Hills | **320 × 64** | 64 – 127 | ¼ speed | **yes** | above the rooflines |
| Ground | **320 × 96** | 128 – 223 | full speed | **yes** | none: opaque everywhere |

They are stacked and not overlapped for two reasons, and both are worth
knowing before drawing anything. The hardware draws only 96 sprites on a
scanline, and a layer costs 20 or 21 of them on every line it covers — so two
layers reaching into the band where the fighting happens would spend the
budget the characters need. And the floor has to move at one speed: the hills
scroll at a quarter of the camera's rate, so a floor that was partly hills
would slide at two different rates between its back and its front.

```
  line   0 ┌──────────────────────────────────────┐
           │ HUD - lives, score, timer            │  drawn on the text layer,
        31 │ (reserved; nothing is drawn here)    │  over every sprite
           │                                      │
           │  SKY            320 x 128            │  never moves
        63 │                                      │
        64 ├──────────────────────────────────────┤
           │  HILLS          320 x 64             │  quarter speed
           │  transparent above the rooflines,    │  tiles horizontally
       127 │  fully opaque along its bottom edge  │
       128 ├──────────────────────────────────────┤
           │  GROUND         320 x 96             │  full speed
           │   128-143  behind the walk limit     │  tiles horizontally
           │   144-199  the walkable band         │
           │   200-223  the apron                 │
       223 └──────────────────────────────────────┘
```

### The three parts of the ground layer

The ground is the layer that needs the most explaining, because it is not a
wall seen from the side — it is **a floor seen at a shallow angle**, receding
away from the viewer. Its 96 pixels are three bands with different jobs:

| Rows in the image | Screen lines | What it is |
|---|---|---|
| 0 – 15 | 128 – 143 | Floor **behind** the furthest a character can stand. It exists so the back row has ground behind its feet rather than the horizon. |
| 16 – 71 | 144 – 199 | **The walkable band.** Every character stands somewhere in these 56 lines. One line of the image is one unit of depth. |
| 72 – 95 | 200 – 223 | **The apron**, in front of the walk limit. Nothing stands on it; it stops the floor ending in mid-air at the bottom of the screen. |

Two things follow from that middle band. It should read as **ground going away
from you**, which is done with banding and with texture getting coarser
towards the bottom, since nothing can be scaled to fake perspective. And it
should stay **fairly plain and mid-toned** — every character and every shadow
in the game is drawn on top of it, and a busy or very dark floor swallows
them.

### The one palette

**All three layers share a single 15-colour palette.** Not 15 each — 15 for
the whole stage, plus transparent. This is the constraint that most often
ruins generated stage art: three images drawn independently arrive with three
unrelated colour schemes, and forcing them into 15 shared colours afterwards
leaves all three muddy.

So ask for a **deliberately limited, shared palette across all three layers**,
and prefer art that already looks like it was painted with a dozen colours.
Night scenes are forgiving here; bright daylight with a blue sky, green trees
and warm stone is not.

### Room left

| | |
|---|---|
| Sprite ROM used | 1,390 tiles of 16,384 — about 8% |
| Busiest scanline | 67 sprites of 96, with six characters fighting |
| Spare | room for about **ten characters** on the floor at once |

Enemies are limited by that 96-per-line budget, not by memory. In the walkable
band the ground layer costs 21 sprites, each character costs 7 (four for the
body, three for its shadow), and the impact sparks cost one each.

---

## 3. What a source sheet must look like

The build finds animations by itself: `tools/sheet2neo.py` looks for **fully
transparent gaps**. Rows of animation are separated by transparent rows, and
the frames within a row by transparent columns. Animations are then selected
by number, like `--anim walk:1 --anim jump:4:4-8`.

So the layout rules are:

1. **One animation per row**, in a consistent order.
2. **A clear transparent gap between rows** — a few pixels is enough, but it
   must contain no pixels at all, not even faint ones.
3. **A clear transparent gap between frames** in a row.
4. **Real transparency**, not a painted checkerboard and not white. See the
   pitfalls below; this is the one that has cost the most time.
5. **Margin below the last row.** A row touching the bottom edge of the image
   gets cut off, and the frames are unusable.
6. **A consistent baseline.** The build fits one bounding box to every frame
   of every animation so the character does not change size when the
   animation changes, and aligns them by their feet. Frames drawn at wildly
   different scales will shrink everything else to fit.
7. **Side view, facing right.** Only one direction is needed: the hardware
   mirrors sprites for free. Drawing both directions wastes half the ROM.

Run `python3 tools/sheet2neo.py <sheet> --list` to see what the build found
before wiring anything up. If the frame counts do not match what you drew, the
gaps are not clean.

---

## 4. Asking an image generator for it

Image models are good at the drawing and bad at the layout. Expect to accept
the art and fix the layout, and write the prompt to minimise the fixing.

### A prompt that works

> A pixel art sprite sheet of **[character]**, side view, facing right.
>
> Four rows of animation, in this order: row 1 walk cycle, 8 frames; row 2
> attack, 8 frames; row 3 jump, 4 frames; row 4 being hit, 3 frames —
> recoiling backwards, head turned away, off balance.
>
> Every frame the same size, evenly spaced, with clear empty space between
> each frame and between each row. Leave empty space below the last row.
>
> All frames share one ground line — the character's feet at the same height
> in every frame of a row.
>
> Limited palette, about 12 to 15 flat colours, no gradients, no
> anti-aliasing, hard pixel edges, no outline glow, no drop shadow.
>
> Plain solid magenta background (#FF00FF), nothing else in the image, no
> text, no labels, no frame numbers, no borders.

Magenta rather than "transparent" is deliberate: models rarely produce a real
alpha channel, and a saturated colour that appears nowhere in the art is
trivial to key out cleanly. White is a poor choice — it collides with
highlights, eyes and metal.

### For a stage

A stage is three layers that have to look like one place, so the thing to get
right first is that they share an art direction and a palette. Two ways to ask
for it, and the first is usually the better one.

**Every prompt below should open with a context paragraph** — where this is,
what time of day, what mood — because that is what makes the three layers
agree with each other. The measurements alone produce three correct rectangles
that do not belong in the same scene.

#### One image, three panels

One request, one image, all three layers stacked in it with magenta gutters
between them. The panels are cut apart afterwards. This keeps the palette and
the lighting consistent for free, because the model drew them together.

The image is **320 × 320**, laid out top to bottom:

| Rows | Contents |
|---|---|
| 0 – 127 | the sky panel, 320 × 128 |
| 128 – 143 | solid magenta gutter, 16 px |
| 144 – 207 | the hills panel, 320 × 64 |
| 208 – 223 | solid magenta gutter, 16 px |
| 224 – 319 | the ground panel, 320 × 96 |

#### One image per layer

Three requests. Use this when a panel came back wrong and only that one needs
redoing, or when the model will not hold a layout. Paste the same context
paragraph into each of the three, then the block for that layer.

Either way, **expect to fix the layout by hand.** Image models are good at the
drawing and bad at exact pixel dimensions; assume you will crop and resample
to the exact sizes, and assume the seam needs repairing. `tools/make_stage.py`
draws the current stage in code precisely because that guarantees a seamless
join: its ridges are sine waves with a whole number of cycles across the
width, so they cannot help but meet.

---

### A worked example: a Kyoto street in the ninja era

#### The context paragraph — goes at the top of every request

> A night scene on a narrow street in Kyoto in the late feudal period, the era
> of ninja and wandering swordsmen. Wooden machiya townhouses with deep tiled
> eaves crowd the street, paper lanterns glowing dull orange outside their
> doors, a temple gate further off. The mood is quiet, cold and blue, lit by a
> low moon — not a festival, not a battle. This is background art for a 16-bit
> arcade beat-'em-up: flat pixel art, hard edges, no gradients, no
> anti-aliasing, and a single deliberately limited palette of about 15 colours
> shared across the whole scene, mostly deep blues and browns with a few warm
> lantern accents.

#### As one image, three panels

> [context paragraph]
>
> Produce a single image, exactly 320 pixels wide and 320 pixels tall,
> containing three separate horizontal panels of the same scene, divided by
> solid magenta (#FF00FF) gutters exactly 16 pixels tall. No borders, no
> labels, no text anywhere.
>
> **Rows 0 to 127 — the sky panel, 320 × 128.** The night sky over the city:
> a low crescent moon, scattered stars, thin cloud. Fully opaque, no magenta.
> It never moves, so it needs no seam.
>
> **Rows 144 to 207 — the rooftops panel, 320 × 64.** The middle distance:
> tiled roofs of machiya houses, a temple gate, a pagoda silhouette, drying
> poles and a few lanterns, all seen at a distance and darker than the
> foreground. Everything **above** the rooflines must be solid magenta
> (#FF00FF) so the sky shows through. The bottom edge of this panel must be
> fully opaque across its whole width, with no magenta reaching it. The left
> and right edges must match exactly so it repeats seamlessly.
>
> **Rows 224 to 319 — the street panel, 320 × 96.** The street surface itself,
> seen at a shallow angle from slightly above, receding away from the viewer —
> a flat floor, not a wall. Packed earth and worn stone paving, a gutter, the
> stone bases of the buildings along the very top. Plain and mid-toned in the
> middle: characters are drawn on top of it and must stay readable. Detail
> should get coarser and larger towards the bottom of the panel to suggest
> nearness. Fully opaque, no magenta. The left and right edges must match
> exactly so it repeats seamlessly.

#### As three separate requests

> [context paragraph]
>
> A pixel art **night sky over Kyoto**, exactly 320 pixels wide and 128 pixels
> tall. A low crescent moon, scattered stars, thin cloud. Fully opaque — every
> pixel is sky. No horizon line, no buildings, no ground: this is only the sky
> above the rooftops. No characters, no text.

> [context paragraph]
>
> A seamless horizontally tiling pixel art band of **Kyoto rooftops seen at a
> distance**, exactly 320 pixels wide and 64 pixels tall. Tiled machiya roofs,
> a temple gate, a pagoda silhouette, a few dim paper lanterns. Darker and
> less detailed than the foreground, since this is the middle distance.
>
> Everything above the rooflines must be solid magenta (#FF00FF) so it can be
> made transparent. The bottom edge must be completely opaque across the full
> width — no magenta may touch it, or a gap opens between this and the street.
> The left and right edges must match exactly so the band repeats without a
> visible seam. No characters, no text.

> [context paragraph]
>
> A seamless horizontally tiling pixel art **street surface**, exactly 320
> pixels wide and 96 pixels tall, seen at a shallow angle from slightly above
> and receding away from the viewer. This is a floor, not a wall.
>
> Read it as three bands down its height. The top 16 pixels are the far edge
> of the street where it meets the buildings: the stone bases of the houses, a
> drain, a step. The middle 56 pixels are the part characters walk on — packed
> earth and worn stone paving, plain and mid-toned, no large or bright
> features, because characters and their shadows are drawn on top and must
> stay readable. The bottom 24 pixels are the nearest part of the street,
> which may be darker and more detailed.
>
> Suggest depth by making the paving texture coarser and larger towards the
> bottom, not by drawing converging lines or a vanishing point. Fully opaque
> everywhere. The left and right edges must match exactly so it repeats
> without a visible seam. No characters, no text.

#### Wiring the result in

The build currently **draws** the stage rather than converting it, so there is
no tool yet that takes three finished layer images and produces the tile sheets
and `stage.h`. That converter is the missing piece between generated art and
the cartridge; it would be the stage's equivalent of `tools/sheet2neo.py`, and
its main job is the hard part described above — quantising all three layers
together against one shared 15-colour palette.

---

## 5. Pitfalls, all of which have already happened here

**A checkerboard background is not transparency.** A generated sheet arrived
as a JPEG with the grey transparency checkerboard painted into it as ordinary
pixels. `tools/prep_sheet.py` exists to flood that away from the edges inwards
— flooding rather than deleting every grey pixel, so grey *inside* the
character survives. Ask for flat magenta and this step is reliable; accept a
checkerboard and it is guesswork.

**JPEG is the wrong format.** Its compression smears colour across hard pixel
edges, which both makes the background harder to key and wastes palette
entries on artefacts. Ask for PNG.

**Thousands of colours become fifteen.** One sheet arrived with 33,798 distinct
colours and came out muted, because the palette had to be spread across skin,
cloth and metal at once. Art drawn with a deliberately small palette survives
the conversion far better than art that is quantised down to one.

**A row touching the bottom edge is lost.** One sheet's idle poses were cut off
by the image boundary, so that animation had to be dropped. Ask for margin.

**Frames that merge are counted as one.** Where a sword or a cape crossed into
the neighbouring frame, the build saw 7 frames instead of 8. Gaps must be
truly empty.

---

## 6. Adding a new asset

For a character sheet, point the makefile at it and name its animations:

```make
SHEET=assets/images/sprites/your-sheet.png
    --anim walk:0 --anim attack:1 --anim jump:2 --anim hurt:3
```

`--anim NAME:ROW` takes a whole row; `--anim NAME:ROW:FIRST-LAST` takes part
of one, which is how the current character's hurt animation borrows two frames
out of the middle of its attack. The character's size is `--height`, on either
`gifs2sheet` or `prep_sheet` depending on which path the sheet comes through.

Both tools write a C header next to their output with the palette, the frame
size and where each animation starts, and the game reads those rather than
hard-coded numbers — so a new sheet of a different size needs no code change.

---

## 7. Sound

Everything the game plays is an ADPCM-A sample in the 512 KB V ROM. Drop an
`.mp3` in `assets/sound/`, add its name to `SFX` in the makefile and to
`assets/sound/samples-map.yaml`, then give it a command number in
`src/user_commands.s` and a matching `SND_*` in `src/sound.h`.

| Constraint | Value | Why |
|---|---|---|
| Sample rate | 18.5 kHz, fixed | The YM2610's ADPCM-A channels have no rate control |
| Channels | Mono | Panning is per channel, in the descriptor |
| Cost | ~9 KB per second | 4 bits a sample; the V ROM holds about 56 seconds |
| Channels available | 6 | Sounds on the same channel cut each other off |
| Filename | lowercase | The build derives the WAV and the C name from it |

The makefile resamples and trims each file, generously at the tail so a decay
is not clipped. It also writes `build/assets/sfx.h` with each sample's length
in video frames.

**Channel choice matters.** Effects that can overlap need separate channels: a
punch landed in mid-air must not cut the jump short. The two music tracks
deliberately share channel 4, so starting one stops the other.

**Looping is a retrigger.** The YM2610 loops ADPCM-B in hardware but not
ADPCM-A, so a track that has to keep going is started again as it ends. Use
the generated `SFX_<NAME>_FRAMES` as the period rather than a number you
worked out once — retrimming the audio changes it.

**Long music is expensive.** An eight second loop is a sixth of the sample ROM.
Prefer a short phrase that loops over a long one that does not.
