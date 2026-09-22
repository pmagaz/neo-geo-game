/*
 * The fix layer and the frame clock.
 *
 * The fix layer is a 40x32 grid of 8x8 tiles drawn on top of every sprite. It
 * is the only thing on this hardware that can cover the whole screen without
 * spending sprites, so it carries the text, the screen transitions and any
 * user interface a screen wants to draw.
 */

#ifndef VIDEO_H
#define VIDEO_H

#include <ngdevkit/neogeo.h>

/*
 * How palette bank 1 is shared out. A palette number means the same thing on
 * every screen, so a module that draws into the fix layer can name the colours
 * it wants without knowing what else is on screen.
 */
#define PAL_TEXT 0      /* 1 white, 2 shadow, 3 black - what dissolves paint */
#define PAL_HERO 1
#define PAL_STAGE 2
#define PAL_UI 3        /* same text colours, but 3 is the frame colour */
#define PAL_SHADOW 4    /* one dark colour, stippled into a shadow */
#define PAL_HIT 5       /* every colour white: the flash when a blow lands */

#define FIX_COLS 40
#define FIX_ROWS 32

/*
 * An NTSC screen only shows fix rows 2 to 29. Text outside that is written and
 * never seen, which is easy to do and hard to spot, so screens lay themselves
 * out against these rather than against FIX_ROWS.
 */
#define VIS_ROW 2
#define VIS_ROWS 28

/// Block until the next Vertical Blank: the 60 Hz frame clock.
void wait_vblank(void);

/// Write one fix map cell. `entry` is (palette << 12) | tile.
void fix_put(u16 col, u16 row, u16 entry);

/// Fill a rectangle with one cell value.
void fix_fill(u8 col, u8 row, u8 w, u8 h, u16 entry);

/// Blank a rectangle, revealing the sprites behind it.
void fix_clear(u8 col, u8 row, u8 w, u8 h);

/// Draw a one-tile-thick frame, in colour 3 of `palette`. The inside is
/// left as it is.
void fix_box(u8 col, u8 row, u8 w, u8 h, u8 palette);

/// Lay text out inside a `width` by `rows` area, breaking between words.
///
/// `spacing` is how many rows each line advances by: 1 packs them, 2 leaves a
/// blank row between, which reads much better in a box of running text since
/// the fix font fills its 8x8 cell edge to edge.
///
/// Returns how many lines were drawn. Text that does not fit is dropped, so
/// the caller can tell a page was too long by comparing against its own count.
u8 fix_wrap_text(u8 col, u8 row, u8 width, u8 rows, u8 spacing,
                 u8 palette, const char *s);

/// Cover every cell at once, with no animation.
void cover_screen(void);

/// Break the screen up into blocks until it is covered, over 16 frames.
void dissolve_out(void);

/// Clear the blocks away to reveal the sprites, over 16 frames. This wipes
/// the whole fix layer, so a screen draws its text and boxes afterwards.
void dissolve_in(void);

#endif /* VIDEO_H */
