#include <ngdevkit/neogeo.h>
#include <ngdevkit/ng-fix.h>
#include "video.h"

#define FIX_MAP 0x7000
#define SOLID_TILE 1280                 /* straight after ngdevkit's font */
#define EMPTY_TILE 255                  /* transparent */


/* The cartridge runtime calls this back on every Vertical Blank interrupt,
   which is our frame clock: 59.6 Hz on AES, 59.2 Hz on MVS. */
static volatile u8 vblank = 0;

void rom_callback_VBlank(void) {
    vblank = 1;
}

void wait_vblank(void) {
    while (!vblank);
    vblank = 0;
}


/*
 * The fix map is stored column-major: the 32 rows of a column are consecutive,
 * and the next column starts 32 words later.
 */
void fix_put(u16 col, u16 row, u16 entry) {
    *REG_VRAMADDR = FIX_MAP + col * 32 + row;
    *REG_VRAMRW = entry;
}


void fix_fill(u8 col, u8 row, u8 w, u8 h, u16 entry) {
    /* Rows within a column are consecutive, so a column of the rectangle is
       one address and h writes. */
    *REG_VRAMMOD = 1;
    for (u8 i = 0; i < w; i++) {
        *REG_VRAMADDR = FIX_MAP + (col + i) * 32 + row;
        for (u8 j = 0; j < h; j++) {
            *REG_VRAMRW = entry;
        }
    }
}


void fix_clear(u8 col, u8 row, u8 w, u8 h) {
    fix_fill(col, row, w, h, EMPTY_TILE);
}


void fix_box(u8 col, u8 row, u8 w, u8 h, u8 palette) {
    u16 entry = (u16)(palette << 12) | SOLID_TILE;

    fix_fill(col, row, w, 1, entry);                /* top */
    fix_fill(col, (u8)(row + h - 1), w, 1, entry);  /* bottom */
    fix_fill(col, (u8)(row + 1), 1, (u8)(h - 2), entry);
    fix_fill((u8)(col + w - 1), (u8)(row + 1), 1, (u8)(h - 2), entry);
}


u8 fix_wrap_text(u8 col, u8 row, u8 width, u8 rows, u8 spacing,
                 u8 palette, const char *s) {
    char line[FIX_COLS + 1];
    u8 used = 0;                /* rows consumed */
    u8 lines = 0;

    if (width > FIX_COLS) {
        width = FIX_COLS;
    }
    if (spacing < 1) {
        spacing = 1;
    }

    while (*s && used < rows) {
        while (*s == ' ') {
            s++;               /* a line never starts on a space */
        }
        if (!*s) {
            break;
        }

        /* Fill the line, remembering the last point it could be cut at
           without splitting a word. */
        const char *p = s;
        const char *word_end = 0;
        u8 n = 0;
        u8 word_len = 0;
        while (*p && n < width) {
            line[n++] = *p++;
            if (*p == ' ' || *p == '\0') {
                word_end = p;
                word_len = n;
            }
        }
        /* Stopped mid-word: back up to the last whole one. A single word
           longer than the line has no such point, so it is cut. */
        if (*p && *p != ' ' && word_end) {
            n = word_len;
            p = word_end;
        }
        line[n] = '\0';

        ng_text(col, (u8)(row + used), palette, line);
        used = (u8)(used + spacing);
        lines++;
        s = p;
    }
    return lines;
}


/*
 * Screen transition: a block dissolve.
 *
 * Filling the fix layer's cells with a solid tile hides the screen and
 * clearing them reveals it again a block at a time. 8x8 is as fine as this
 * gets - the fix grid is the hardware's, and nothing smaller exists.
 *
 * Each cell is given a pseudo-random step at which it flips, so the screen
 * breaks up in a scatter rather than a sweep. One step per frame, so the whole
 * thing takes DISSOLVE_STEPS frames, and each frame only touches the cells
 * belonging to that step.
 */
#define DISSOLVE_STEPS 16

/// Which step a cell flips on. Deliberately scrambled, not a sweep.
static u8 cell_step(u16 col, u16 row) {
    u16 h = (u16)(col * 37u + row * 101u + ((col ^ row) << 3));
    h ^= (u16)(h >> 5);
    return (u8)(h % DISSOLVE_STEPS);
}


void cover_screen(void) {
    fix_fill(0, 0, FIX_COLS, FIX_ROWS, (PAL_TEXT << 12) | SOLID_TILE);
}


/// Flip every cell to `entry`, a step per frame, scattered.
static void dissolve(u16 entry) {
    for (u8 step = 0; step < DISSOLVE_STEPS; step++) {
        *REG_VRAMMOD = 0;
        for (u16 col = 0; col < FIX_COLS; col++) {
            for (u16 row = 0; row < FIX_ROWS; row++) {
                if (cell_step(col, row) == step) {
                    fix_put(col, row, entry);
                }
            }
        }
        wait_vblank();
    }
}


void dissolve_out(void) {
    dissolve((PAL_TEXT << 12) | SOLID_TILE);
}


void dissolve_in(void) {
    dissolve((PAL_TEXT << 12) | EMPTY_TILE);
}
