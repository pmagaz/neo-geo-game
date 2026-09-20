#include <ngdevkit/neogeo.h>
#include <ngdevkit/ng-fix.h>
#include "dialogue.h"
#include "input.h"
#include "sound.h"
#include "video.h"

/*
 * Two boxes side by side, one per half of the screen. Each fills 80% of its
 * half, which leaves the margins: two columns either side of each box, and
 * three rows above and below.
 *
 *      col 0        20                39
 *          +---------+-----------------+
 *   row 2  |  .----. |  .-----------.  |
 *          |  |face| |  |   text    |  |
 *          |  '----' |  '-----------'  |
 *          |         |  PRESS A ...    |
 *
 * The portrait is an empty frame for now: the character art for it does not
 * exist yet, and a sprite drawn inside the frame is the only part of this
 * screen that has to change when it does.
 */
#define HALF_COLS (FIX_COLS / 2)
#define BOX_COLS (HALF_COLS * 8 / 10)                   /* 16 */
#define BOX_ROWS (VIS_ROWS * 8 / 10)                    /* 22 */
#define BOX_ROW (VIS_ROW + (VIS_ROWS - BOX_ROWS) / 2)   /* 5 */
#define FACE_COL ((HALF_COLS - BOX_COLS) / 2)           /* 2 */
#define TEXT_COL (HALF_COLS + FACE_COL)                 /* 22 */

/* One column of padding inside the frame, so the text does not touch it. */
#define TXT_COL (TEXT_COL + 2)
#define TXT_ROW (BOX_ROW + 2)
#define TXT_W (BOX_COLS - 4)
#define TXT_H (BOX_ROWS - 4)

/* Every other row, which leaves a blank line between lines of text. That caps
   a page at (TXT_H + 1) / 2 lines of TXT_W characters - around ninety
   characters, so pages are short. Longer ones are silently cut off. */
#define TXT_SPACING 2

/* Under the text box, in the right-hand half. */
#define PROMPT "PRESS A TO CONTINUE"
#define PROMPT_COL HALF_COLS
#define PROMPT_ROW (BOX_ROW + BOX_ROWS + 1)


/* Frames since the music was last started, for the retrigger. */
static u16 music_age;


static void start_music(const struct dialogue *d) {
    if (d->music != SND_NONE) {
        play_sound(d->music);
        music_age = 0;
    }
}


/// Restart the track as it runs out, so it lasts as long as the screen does.
static void music_tick(const struct dialogue *d) {
    if (d->music == SND_NONE || d->music_loop == 0) {
        return;
    }
    if (++music_age >= d->music_loop) {
        start_music(d);
    }
}


/// Hold here until A is pressed, one poll per frame.
static void wait_for_a(const struct dialogue *d) {
    for (;;) {
        wait_vblank();
        input_poll();
        music_tick(d);
        if (in_pressed & CNT_A) {
            return;
        }
    }
}


void dialogue_run(const struct dialogue *d) {
    ng_cls();
    cover_screen();
    dissolve_in();

    start_music(d);

    fix_box(FACE_COL, BOX_ROW, BOX_COLS, BOX_ROWS, PAL_UI);
    fix_box(TEXT_COL, BOX_ROW, BOX_COLS, BOX_ROWS, PAL_UI);

    for (u8 page = 0; page < d->page_count; page++) {
        /* Only the text area is rewritten between pages, so the frames stay
           put and nothing flickers. */
        fix_clear(TXT_COL, TXT_ROW, TXT_W, TXT_H);
        fix_wrap_text(TXT_COL, TXT_ROW, TXT_W, TXT_H, TXT_SPACING,
                      PAL_TEXT, d->pages[page]);
        ng_text(PROMPT_COL, PROMPT_ROW, PAL_TEXT, PROMPT);

        wait_for_a(d);
    }

    dissolve_out();
}
