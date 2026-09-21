/*
 * The floor plane the characters walk on, and the fixed point their positions
 * are kept in.
 *
 * A character has three coordinates rather than two. `x` runs along the stage
 * and scrolls with the camera. `z` is depth on the floor: 0 at the back,
 * FLOOR_Z_MAX at the front, one unit per scanline. `air` is height above the
 * floor, and is zero unless the character is jumping or has been knocked
 * down.
 *
 * Depth never scales a sprite. The hardware can shrink one, but a beat-'em-up
 * floor read through size changes looks like a zoom rather than a floor, so
 * depth is expressed entirely through screen y and through drawing order.
 * That is also why the sorting has to be right on every single frame: it is
 * the only thing creating the illusion.
 *
 * Positions are 8.8 fixed point because the speeds are not whole pixels. A
 * character walks 2 px a frame along x and 65% of that through z, which is
 * 1.3 - and in whole pixels that can only be 1 or 2. The first makes the
 * floor feel like a corridor and the second like ice.
 *
 * `x` is 32-bit because a stage is wider than the +-127 px an 8.8 s16 can
 * hold. `z` and `air` are 16-bit, a depth of 71 being 18176 in 8.8.
 */

#ifndef FLOOR_H
#define FLOOR_H

#include <ngdevkit/neogeo.h>
#include "stage.h"

#define FX_BITS 8
#define FX_ONE (1 << FX_BITS)

/// A whole number of pixels as fixed point.
#define FX(whole) ((whole) * FX_ONE)

/// Whole pixels from fixed point. An arithmetic shift keeps its sign, so this
/// rounds consistently towards the left of the stage and the back of the
/// floor rather than towards zero from either side.
#define FX_PX(v) ((s16)((v) >> FX_BITS))

/*
 * The floor, from the generated stage header so that the art and the movement
 * cannot disagree about where it is.
 *
 * The walkable band is not the whole floor. FLOOR_BACK rows of it sit behind
 * the furthest a character may stand, because a character whose feet are on
 * the floor's very top edge has the hills immediately behind them and reads
 * as standing on the horizon rather than on the ground. Depth is clamped to
 * FLOOR_Z_MIN..FLOOR_Z_MAX, and the rows above that are floor the characters
 * can be seen against but never reach.
 *
 * Per-room limits narrow this further, for a bridge or a corridor; they never
 * widen it past these.
 */
#define FLOOR_TOP STAGE_FLOOR_TOP
#define FLOOR_DEPTH STAGE_FLOOR_DEPTH
#define FLOOR_Z_MIN STAGE_FLOOR_BACK
#define FLOOR_Z_MAX (FLOOR_DEPTH - 1)
#define FLOOR_Z_MID ((FLOOR_Z_MIN + FLOOR_Z_MAX) / 2)

/*
 * Screen y of the top edge of a sprite whose feet are at (z, air).
 *
 * The feet are the origin because sheet2neo.py sits every frame on the bottom
 * edge of its cell, so the bottom of the sprite is the bottom of the
 * character however tall the art happens to be.
 *
 * `height` is passed in rather than read from a character's own header: the
 * one thing that must not be written as a literal here is 96. It is only 96
 * because the art is currently scaled to six tiles, and a literal would float
 * every character above its own shadow the next time a sheet is regenerated
 * at a different size.
 */
static inline s16 floor_screen_top(s16 z, s16 air, u16 height) {
    return (s16)(FLOOR_TOP + FX_PX(z) - FX_PX(air) - (s16)height);
}

#endif /* FLOOR_H */
