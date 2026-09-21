/*
 * A character on a parallax stage that scrolls forever, with a title screen
 * and a dialogue screen in front of it.
 *
 * On the Neo Geo a sprite is a vertical strip of tiles, and there is no
 * background layer at all: the stage is sprites too, numbered below the
 * character because higher-numbered sprites are drawn in front.
 *
 * The screen-wide machinery lives in src/: video.c owns the fix layer, the
 * frame clock and the transitions, dialogue.c is the portrait-and-text screen,
 * input.c samples the pad, and sound.h is the list of noises the Z80 can make.
 */

#include <ngdevkit/neogeo.h>
#include <ngdevkit/ng-fix.h>
#include "hero.h"
#include "stage.h"
#include "assets/sfx.h"
#include "dialogue.h"
#include "floor.h"
#include "input.h"
#include "sound.h"
#include "video.h"

/* C ROM layout. The BIOS eye-catcher owns tiles 0-255, then each sheet is
   loaded after the one before it, in the order the makefile lists them. */
#define HERO_TILE 256
#define SKY_TILE (HERO_TILE + HERO_TILE_COUNT)
#define HILLS_TILE (SKY_TILE + STAGE_SKY_TILE_COUNT)
#define GROUND_TILE (HILLS_TILE + STAGE_HILLS_TILE_COUNT)

/*
 * Sprite numbering decides drawing order: higher numbers draw in front, and
 * sprite 0 is never drawn. The scrolling layers get one sprite more than the
 * screen is wide, so a column is always available to cover the seam as the
 * others slide left.
 */
#define SKY_SPRITE 1
#define HILLS_SPRITE (SKY_SPRITE + STAGE_COLS)
#define GROUND_SPRITE (HILLS_SPRITE + STAGE_COLS + 1)
#define FIRST_SPRITE (GROUND_SPRITE + STAGE_COLS + 1)

/*
 * Each character owns a block of consecutive sprites, and it has to be
 * consecutive: the columns of a character are a sticky chain, where every
 * column after the first inherits the leader's position and sits 16 px to its
 * right. So a character cannot be given sprites scattered through the list.
 *
 * Three sprites are reserved in front of each body for the shadow that
 * arrives with the depth sorting. They sit at zero height until then. The
 * reservation costs nothing - 315 of the hardware's 381 sprites are unused -
 * and it saves renumbering every block afterwards.
 *
 * The shadow comes before the body so that it draws behind it, drawing order
 * being sprite order.
 */
#define MAX_ENTITIES 8
#define SHADOW_SPRITES 3
#define BODY_SPRITES HERO_TILES_W
#define ENT_SPRITES (SHADOW_SPRITES + BODY_SPRITES)

#define ENT_SHADOW(slot) (FIRST_SPRITE + (slot) * ENT_SPRITES)
#define ENT_BODY(slot) (ENT_SHADOW(slot) + SHADOW_SPRITES)

#define SCREEN_W 320
#define SCREEN_H 224
#define CHAR_W (HERO_TILES_W * 16)
#define CHAR_H (HERO_TILES_H * 16)

/*
 * Movement, in 8.8 pixels a frame.
 *
 * Depth moves at 65% of horizontal. The floor is seen at a shallow angle, so
 * a scanline of depth stands for more ground than a pixel of width; matching
 * the two speeds would make walking towards the screen feel like sprinting.
 *
 * The diagonals are that pair scaled so their magnitude is exactly the
 * horizontal speed: 1.676 and 1.090 give 1.999. Without it, holding two
 * directions is 19% faster than holding one, which every player finds inside
 * a minute and which turns every fight into a diagonal shuffle.
 */
#define SPEED_X FX(2)       /* 512, 2.000 px */
#define SPEED_Z 333         /*      1.301 px, 65% of x */
#define SPEED_DIAG_X 429    /*      1.676 px */
#define SPEED_DIAG_Z 279    /*      1.090 px */

#define JUMP_SPEED FX(9)
#define GRAVITY FX(1)

/*
 * The stage's extent in whole pixels. Four screens, which is how far the
 * world used to run before it wrapped - but bounded, because a camera that
 * clamps to the stage and a fight room that seals its exits both need the
 * stage to have ends. Per-room bounds replace this.
 */
#define STAGE_WIDTH (4 * SCREEN_W)
#define STAGE_X_MIN FX(0)
#define STAGE_X_MAX FX(STAGE_WIDTH - CHAR_W)

/*
 * The camera only follows once the character leaves a dead zone in the middle
 * of the screen, so small steps do not drag the whole stage around. The zone
 * runs from 40% to 60% of the width, measured at the character's centre.
 */
#define CAM_RIGHT ((SCREEN_W * 60 / 100) - CHAR_W / 2)
#define CAM_LEFT ((SCREEN_W * 40 / 100) - CHAR_W / 2)

/* Game frames each animation frame is held for. */
#define WALK_RATE 4
#define ATTACK_RATE 3

/// The art faces right, so walking left is the mirrored one. Depth adds no
/// facings: nothing ever faces towards the screen or away from it.
#define FACING_RIGHT 0
#define FACING_LEFT 1

enum state {
    ST_IDLE,
    ST_WALK,
    ST_JUMP,
    ST_ATTACK,
};

struct entity {
    s32 x;          /* along the stage, 8.8 */
    s16 z;          /* depth on the floor, 8.8, clamped to the band */
    s16 air;        /* height above the floor, 8.8; zero is standing */
    s16 vair;       /* vertical speed, 8.8 */
    u8 active;
    u8 facing;
    u8 state;
    u8 frame;
    u8 tick;
};

static struct entity ents[MAX_ENTITIES];
static struct entity *player = &ents[0];
static s16 camera_x = 0;
/* The tile column each scrolling layer is currently showing, so its tile maps
   are only rewritten when the scroll crosses a whole tile. */
static s16 hills_tile_scroll = -1;
static s16 ground_tile_scroll = -1;


/* The four palettes video.h names. */
static const u16 text_palette[16] = {
    0x8000, 0x0fff, 0x0666, 0x8000, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
};      /*                  ^ colour 3: black, what the transition paints */

/* The same text colours, but colour 3 is the frame colour the dialogue boxes
   are drawn in rather than black. */
static const u16 ui_palette[16] = {
    0x8000, 0x0fff, 0x0666, 0x0eb4, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
};      /*                  ^ colour 3: warm gold */


/*
 * Dim one colour to num/den of its brightness.
 *
 * A palette entry packs six bits per channel awkwardly: the top four bits of
 * each are together in the low half of the word, each channel's next bit is in
 * bits 14-12, and the lowest bit of all three is shared in bit 15 and stored
 * inverted. So the channels have to be unpacked, scaled and packed again.
 */
static u16 dim_color(u16 c, u16 num, u16 den) {
    u16 dark = ((c >> 15) & 1) ^ 1;
    u16 r = (u16)((((c >> 8) & 0xf) << 2) | (((c >> 14) & 1) << 1) | dark);
    u16 g = (u16)((((c >> 4) & 0xf) << 2) | (((c >> 13) & 1) << 1) | dark);
    u16 b = (u16)(((c & 0xf) << 2) | (((c >> 12) & 1) << 1) | dark);

    r = (u16)((u32)r * num / den);
    g = (u16)((u32)g * num / den);
    b = (u16)((u32)b * num / den);

    u16 nd = (((r & 1) + (g & 1) + (b & 1)) >= 2) ? 1 : 0;
    return (u16)(((nd ^ 1) << 15)
                 | (((r >> 1) & 1) << 14) | (((g >> 1) & 1) << 13)
                 | (((b >> 1) & 1) << 12)
                 | (((r >> 2) & 0xf) << 8) | (((g >> 2) & 0xf) << 4)
                 | ((b >> 2) & 0xf));
}


/// Write all three palettes at `level`/FADE_STEPS of their brightness.
#define FADE_STEPS 9

static void set_brightness(u16 level) {
    for (u16 i = 0; i < 16; i++) {
        MMAP_PALBANK1[PAL_TEXT * 16 + i] =
            dim_color(text_palette[i], level, FADE_STEPS);
        MMAP_PALBANK1[PAL_HERO * 16 + i] =
            dim_color(hero_palette[i], level, FADE_STEPS);
        MMAP_PALBANK1[PAL_STAGE * 16 + i] =
            dim_color(stage_palette[i], level, FADE_STEPS);
        MMAP_PALBANK1[PAL_UI * 16 + i] =
            dim_color(ui_palette[i], level, FADE_STEPS);
    }
}


/*
 * Fade the screen, about 150 ms each way.
 *
 * This replaces an earlier attempt that scaled every sprite with the
 * hardware's shrink. That grows the scene out of a point, but the hardware can
 * only shrink and never grow past full size, so a full-screen image scaled
 * down always leaves the screen's edges empty around it. A fade has nothing to
 * leave empty, and it dims the text too, which no sprite effect can.
 */
static void fade_to_black(void) {
    for (s16 l = FADE_STEPS; l >= 0; l--) {
        set_brightness((u16)l);
        wait_vblank();
    }
}

static void fade_from_black(void) {
    for (u16 l = 0; l <= FADE_STEPS; l++) {
        set_brightness(l);
        wait_vblank();
    }
}


static void init_palette(void) {
    set_brightness(FADE_STEPS);
    /* The backdrop shows wherever no sprite is drawn: the last colour of the
       bank. Black, so a screen with the stage hidden is plain black. */
    MMAP_PALBANK1[4095] = 0x8000;
}


/*
 * The sky never moves, so it is a plain sticky chain: only the leftmost
 * column carries a position and the rest follow it. Written once.
 */
static void init_sky(void) {
    for (u16 col = 0; col < STAGE_COLS; col++) {
        *REG_VRAMMOD = 1;
        *REG_VRAMADDR = ADDR_SCB1 + (SKY_SPRITE + col) * 64;
        for (u16 row = 0; row < STAGE_SKY_ROWS; row++) {
            *REG_VRAMRW = SKY_TILE + row * STAGE_COLS + col;
            *REG_VRAMRW = 2 << 8;               /* palette 2 */
        }

        *REG_VRAMMOD = 0;
        *REG_VRAMADDR = ADDR_SCB2 + SKY_SPRITE + col;
        *REG_VRAMRW = 0xfff;                    /* no shrinking */

        *REG_VRAMADDR = ADDR_SCB3 + SKY_SPRITE + col;
        if (col == 0) {
            *REG_VRAMRW = (((496 - STAGE_SKY_Y) & 0x1ff) << 7) | STAGE_SKY_ROWS;
        } else {
            *REG_VRAMRW = 1 << 6;               /* sticky: follow the previous */
        }
    }

    *REG_VRAMADDR = ADDR_SCB4 + SKY_SPRITE;
    *REG_VRAMRW = 0;
}


/*
 * A scrolling layer cannot use the sticky chain, because each column needs
 * its own X as the layer slides. Shrink and vertical position never change,
 * so they are set once here; only X is touched per frame.
 */
static void init_scrolling_layer(u16 first, u16 rows, s16 y) {
    for (u16 i = 0; i <= STAGE_COLS; i++) {
        *REG_VRAMMOD = 0;
        *REG_VRAMADDR = ADDR_SCB2 + first + i;
        *REG_VRAMRW = 0xfff;

        *REG_VRAMADDR = ADDR_SCB3 + first + i;
        *REG_VRAMRW = (((496 - y) & 0x1ff) << 7) | rows;
    }
}


/*
 * Point each column of a layer at a source column of the artwork.
 *
 * Only needed when the scroll crosses a whole tile: in between, the layer is
 * moved by changing X alone.
 */
static void layer_set_tiles(u16 first, u16 tile_base, u16 rows, u16 tile_scroll) {
    for (u16 i = 0; i <= STAGE_COLS; i++) {
        u16 src = i + tile_scroll;
        if (src >= STAGE_COLS) {
            src -= STAGE_COLS;      /* i and tile_scroll are both < COLS+1 */
        }

        *REG_VRAMMOD = 1;
        *REG_VRAMADDR = ADDR_SCB1 + (first + i) * 64;
        for (u16 row = 0; row < rows; row++) {
            *REG_VRAMRW = tile_base + row * STAGE_COLS + src;
            *REG_VRAMRW = 2 << 8;
        }
    }
}


/*
 * Slide a layer by `frac` pixels, 0-15.
 *
 * The leftmost column ends up at a negative X, which is exactly what is
 * wanted: X is nine bits and wraps at 512, and pixels at X >= 320 are off
 * screen, so a column placed just below 512 has its tail appear at the left
 * edge. That is what covers the seam.
 */
static void layer_set_x(u16 first, s16 frac) {
    for (u16 i = 0; i <= STAGE_COLS; i++) {
        *REG_VRAMMOD = 0;
        *REG_VRAMADDR = ADDR_SCB4 + first + i;
        *REG_VRAMRW = ((((s16)(i * 16)) - frac) & 0x1ff) << 7;
    }
}


static void scroll_layer(u16 first, u16 tile_base, u16 rows,
                         s16 scroll, s16 *last_tile_scroll) {
    /* Keep the scroll inside one repeat of the artwork, staying positive so
       the tile index and the pixel offset are both easy to reason about. */
    s16 s = scroll % (STAGE_COLS * 16);
    if (s < 0) {
        s += STAGE_COLS * 16;
    }

    s16 tile_scroll = s >> 4;
    if (tile_scroll != *last_tile_scroll) {
        layer_set_tiles(first, tile_base, rows, (u16)tile_scroll);
        *last_tile_scroll = tile_scroll;
    }
    layer_set_x(first, s & 15);
}


/*
 * Load one animation frame into a character's sprite block.
 *
 * Mirroring is not only a per-tile flag: the columns have to be emitted in
 * reverse order too, or the character is assembled back to front.
 */
static void set_body_frame(u16 base, u16 anim_row, u8 f, u8 dir) {
    u16 attr = (1 << 8) | (dir == FACING_LEFT ? 1 : 0);   /* palette 1, H-flip */

    for (u16 col = 0; col < BODY_SPRITES; col++) {
        u16 src = (dir == FACING_LEFT) ? (BODY_SPRITES - 1 - col) : col;

        *REG_VRAMMOD = 1;
        *REG_VRAMADDR = ADDR_SCB1 + (base + col) * 64;
        for (u16 row = 0; row < HERO_TILES_H; row++) {
            *REG_VRAMRW = HERO_TILE + (anim_row + row) * HERO_SHEET_W
                          + f * HERO_TILES_W + src;
            *REG_VRAMRW = attr;
        }
    }
}


/// Position a character's block. Only the leader is written; the rest of the
/// chain is sticky and follows it.
static void place_body(u16 base, s16 x, s16 y) {
    *REG_VRAMMOD = ADDR_SCB4 - ADDR_SCB3;   /* so SCB4 follows SCB3 */
    *REG_VRAMADDR = ADDR_SCB3 + base;
    *REG_VRAMRW = (((496 - y) & 0x1ff) << 7) | HERO_TILES_H;
    *REG_VRAMRW = (x & 0x1ff) << 7;
}


/// A sprite with a height of zero is not drawn, and a sticky follower
/// inherits the leader's height, so zeroing the leader hides the block.
static void hide_body(u16 base) {
    *REG_VRAMMOD = 0;
    *REG_VRAMADDR = ADDR_SCB3 + base;
    *REG_VRAMRW = 0;
}


/*
 * Lay out every character's sprite block once: shrink off, and the chain
 * stitched together. Only the tiles and the leader's position change per
 * frame after this.
 */
static void init_entities(void) {
    for (u16 slot = 0; slot < MAX_ENTITIES; slot++) {
        u16 base = ENT_BODY(slot);

        for (u16 col = 0; col < BODY_SPRITES; col++) {
            *REG_VRAMMOD = 0;
            *REG_VRAMADDR = ADDR_SCB2 + base + col;
            *REG_VRAMRW = 0xfff;            /* no shrinking */

            if (col > 0) {
                *REG_VRAMADDR = ADDR_SCB3 + base + col;
                *REG_VRAMRW = 1 << 6;       /* sticky: follow the previous one */
            }
        }
        hide_body(base);

        /* The shadow block is reserved but has no art yet. */
        *REG_VRAMMOD = 0;
        for (u16 col = 0; col < SHADOW_SPRITES; col++) {
            *REG_VRAMADDR = ADDR_SCB3 + ENT_SHADOW(slot) + col;
            *REG_VRAMRW = 0;
        }
    }
}


/// Advance a character's frame, stopping on the last one. Returns 1 once the
/// last frame has been held for its full time, not as soon as it is reached -
/// otherwise the final frame of an attack flickers past in one game frame and
/// the move is shorter than the animation suggests.
static u8 advance_once(struct entity *e, u8 frames, u8 rate) {
    if (++e->tick >= rate) {
        e->tick = 0;
        if (e->frame + 1 >= frames) {
            return 1;
        }
        e->frame++;
    }
    return 0;
}

/// Advance a character's frame, looping back to the start.
static void advance_loop(struct entity *e, u8 frames, u8 rate) {
    if (++e->tick >= rate) {
        e->tick = 0;
        e->frame = (u8)((e->frame + 1) % frames);
    }
}


static void set_state(struct entity *e, enum state s) {
    if (e->state != s) {
        e->state = (u8)s;
        e->frame = 0;
        e->tick = 0;
    }
}


/// Keep a character on the floor and inside the stage. Per-room depth limits,
/// for a bridge or a corridor that narrows the floor, replace the band here.
static void clamp_to_floor(struct entity *e) {
    if (e->z < FX(0)) {
        e->z = FX(0);
    } else if (e->z > FX(FLOOR_Z_MAX)) {
        e->z = FX(FLOOR_Z_MAX);
    }

    if (e->x < STAGE_X_MIN) {
        e->x = STAGE_X_MIN;
    } else if (e->x > STAGE_X_MAX) {
        e->x = STAGE_X_MAX;
    }
}


/*
 * Walk by the eight-direction speed table. Returns 1 if anything moved.
 *
 * Up is away from the viewer, because depth 0 is the back of the floor.
 * Facing is only touched by the horizontal part: walking straight towards the
 * screen must not turn the character round, and there is no art for facing
 * that way in any case.
 */
static u8 walk(struct entity *e, u8 pad) {
    s16 dx = 0;
    s16 dz = 0;

    if (pad & CNT_LEFT) {
        dx = -1;
    } else if (pad & CNT_RIGHT) {
        dx = 1;
    }
    if (pad & CNT_UP) {
        dz = -1;
    } else if (pad & CNT_DOWN) {
        dz = 1;
    }

    if (!dx && !dz) {
        return 0;
    }

    if (dx && dz) {
        e->x += dx * SPEED_DIAG_X;
        e->z += dz * SPEED_DIAG_Z;
    } else {
        e->x += dx * SPEED_X;
        e->z += dz * SPEED_Z;
    }

    if (dx) {
        e->facing = (dx < 0) ? FACING_LEFT : FACING_RIGHT;
    }
    return 1;
}


static void update_player(struct entity *e) {
    u8 pad = in_pad;
    u8 pressed = in_pressed;

    /* Attacking and jumping run to completion; they are not interrupted. */
    if (e->state == ST_ATTACK) {
        if (advance_once(e, HERO_ATTACK_FRAMES, ATTACK_RATE)) {
            set_state(e, ST_IDLE);
        }
    } else if (e->state == ST_JUMP) {
        /* Steering in mid-air is allowed, which is what makes a jump feel
           controllable rather than committed. Depth included: a jump that
           could not change depth would be useless for crossing a fight. */
        walk(e, pad);

        e->air += e->vair;
        e->vair -= GRAVITY;
        if (e->air <= 0) {
            e->air = 0;
            e->vair = 0;
            set_state(e, ST_IDLE);
        } else {
            /* Map the arc onto the animation: rising uses the early frames,
               falling the later ones. */
            e->frame = (e->vair > 0) ? 2 : 3;
        }
    } else if (pressed & CNT_A) {
        set_state(e, ST_ATTACK);
        play_sound(SND_PUNCH);
    } else if (pressed & CNT_B) {
        /* Jump is a button now. Up and down steer through the floor's depth,
           so the stick has no spare direction to put it on. */
        set_state(e, ST_JUMP);
        e->vair = JUMP_SPEED;
        e->frame = 1;
        play_sound(SND_JUMP);
    } else if (walk(e, pad)) {
        set_state(e, ST_WALK);
        advance_loop(e, HERO_WALK_FRAMES, WALK_RATE);
    } else {
        set_state(e, ST_IDLE);
        e->frame = 0;
    }

    clamp_to_floor(e);
}


/*
 * Follow the character once it leaves the dead zone, then clamp to the stage.
 *
 * The world used to wrap here instead, which kept the coordinates small and
 * let the stage scroll forever. It cannot stay: a camera that stops at the
 * end of a stage, and a fight room that seals its exits, both need the stage
 * to have ends to stop at.
 */
static void update_camera(const struct entity *e) {
    s16 world_x = FX_PX(e->x);
    s16 screen_x = (s16)(world_x - camera_x);

    if (screen_x > CAM_RIGHT) {
        camera_x = (s16)(world_x - CAM_RIGHT);
    } else if (screen_x < CAM_LEFT) {
        camera_x = (s16)(world_x - CAM_LEFT);
    }

    if (camera_x < 0) {
        camera_x = 0;
    } else if (camera_x > STAGE_WIDTH - SCREEN_W) {
        camera_x = STAGE_WIDTH - SCREEN_W;
    }
}


static u16 anim_row(const struct entity *e) {
    switch (e->state) {
    case ST_ATTACK: return HERO_ATTACK_ROW;
    case ST_JUMP:   return HERO_JUMP_ROW;
    default:        return HERO_WALK_ROW;   /* idle rests on walk[0] */
    }
}


/*
 * Write the characters to the sprite list.
 *
 * Every VRAM write in the frame happens here, and none of the movement above
 * touches the hardware. That split is what the sorting pass slots into: it
 * reorders which block each character is emitted into, and nothing in the
 * logic has to know.
 *
 * No sorting yet, so each character stays in its own block and a nearer one
 * does not yet draw in front of one further back.
 */
static void draw_entities(void) {
    for (u16 slot = 0; slot < MAX_ENTITIES; slot++) {
        struct entity *e = &ents[slot];

        if (!e->active) {
            hide_body(ENT_BODY(slot));
            continue;
        }

        set_body_frame(ENT_BODY(slot), anim_row(e), e->frame, e->facing);
        place_body(ENT_BODY(slot),
                   (s16)(FX_PX(e->x) - camera_x),
                   floor_screen_top(e->z, e->air, CHAR_H));
    }
}


static void hide_entities(void) {
    for (u16 slot = 0; slot < MAX_ENTITIES; slot++) {
        hide_body(ENT_BODY(slot));
    }
}


static struct entity *spawn(s32 x, s16 z, u8 facing) {
    for (u16 slot = 0; slot < MAX_ENTITIES; slot++) {
        struct entity *e = &ents[slot];
        if (!e->active) {
            e->x = x;
            e->z = z;
            e->air = 0;
            e->vair = 0;
            e->active = 1;
            e->facing = facing;
            e->state = ST_IDLE;
            e->frame = 0;
            e->tick = 0;
            return e;
        }
    }
    return 0;
}


static void reset_entities(void) {
    for (u16 slot = 0; slot < MAX_ENTITIES; slot++) {
        ents[slot].active = 0;
    }
    camera_x = 0;

    player = spawn(FX(SCREEN_W / 2 - CHAR_W / 2), FX(FLOOR_DEPTH / 2),
                   FACING_RIGHT);

    /* A training dummy, with no behaviour at all. It is here because depth
       cannot be seen with one character on an empty floor: there has to be
       something for it to be in front of and behind. */
    spawn(FX(SCREEN_W / 2 + 96), FX(FLOOR_DEPTH / 2), FACING_LEFT);
}


/*
 * The title screen.
 *
 * The stage stays on screen behind it - it costs nothing, since those sprites
 * are already set up, and an empty parallax backdrop makes a better title card
 * than a black screen. Only the character is hidden.
 */

#define MENU_START 0
#define MENU_QUIT 1
#define MENU_ITEMS 2

static const u8 menu_row[MENU_ITEMS] = { 17, 19 };
static const char *menu_label[MENU_ITEMS] = { "START", "QUIT" };


/*
 * Switch the stage off. A sprite whose height is zero is not drawn, so the
 * screen falls back to the backdrop colour - black. The title screen wants a
 * clean background rather than the game showing through behind it.
 *
 * The sky is a sticky chain and its followers inherit the leader's height, so
 * only the leader needs changing; the scrolling layers each carry their own.
 */
static void show_stage(u8 visible) {
    *REG_VRAMMOD = 0;

    *REG_VRAMADDR = ADDR_SCB3 + SKY_SPRITE;
    *REG_VRAMRW = (((496 - STAGE_SKY_Y) & 0x1ff) << 7)
                  | (visible ? STAGE_SKY_ROWS : 0);

    for (u16 i = 0; i <= STAGE_COLS; i++) {
        *REG_VRAMADDR = ADDR_SCB3 + HILLS_SPRITE + i;
        *REG_VRAMRW = (((496 - STAGE_HILLS_Y) & 0x1ff) << 7)
                      | (visible ? STAGE_HILLS_ROWS : 0);
        *REG_VRAMADDR = ADDR_SCB3 + GROUND_SPRITE + i;
        *REG_VRAMRW = (((496 - STAGE_GROUND_Y) & 0x1ff) << 7)
                      | (visible ? STAGE_GROUND_ROWS : 0);
    }
}


static void draw_menu(u8 selected) {
    for (u8 i = 0; i < MENU_ITEMS; i++) {
        /* The cursor is part of the string so that clearing it needs no
           separate erase: the same width is always written. */
        char line[16];
        const char *label = menu_label[i];
        u8 n = 0;
        line[n++] = (i == selected) ? '>' : ' ';
        line[n++] = ' ';
        while (*label) { line[n++] = *label++; }
        line[n++] = ' ';
        line[n++] = (i == selected) ? '<' : ' ';
        line[n] = '\0';
        ng_center_text(menu_row[i], 0, line);
    }
}


/// Runs the title screen until the player picks something. Returns the choice.
static u8 title_screen(void) {
    u8 selected = MENU_START;

    ng_cls();
    cover_screen();
    hide_entities();
    show_stage(0);
    dissolve_in();

    /* Text goes on after the dissolve: it lives in the same fix layer the
       dissolve paints over, so anything drawn first would be wiped. */
    ng_center_text(8, 0, "T H E   W A N D E R E R");
    ng_center_text(11, 0, "A NEO GEO GAME");
    draw_menu(selected);
    ng_center_text(25, 0, "W S TO CHOOSE   ENTER OR J TO PICK");

    for (;;) {
        wait_vblank();
        input_poll();

        if (in_pressed & (CNT_UP | CNT_DOWN)) {
            selected = (selected + 1) % MENU_ITEMS;   /* only two entries */
            draw_menu(selected);
            play_sound(SND_JUMP);       /* doubles as the cursor blip */
        }

        if ((in_pressed & (CNT_A | CNT_B | CNT_C | CNT_D)) || in_start_pressed) {
            return selected;
        }
    }
}


/*
 * What the character is told before the stage starts.
 *
 * Placeholder text: the screen it is shown on is the point, and swapping in
 * the real script is a matter of editing these strings. Uppercase because that
 * is all the fix font draws well.
 */
static const char *const intro_pages[] = {
    "LOREM IPSUM DOLOR SIT AMET, CONSECTETUR ADIPISCING ELIT.",
    "SED DO EIUSMOD TEMPOR INCIDIDUNT UT LABORE ET DOLORE MAGNA ALIQUA.",
    "UT ENIM AD MINIM VENIAM, QUIS NOSTRUD EXERCITATION ULLAMCO LABORIS.",
};

static const struct dialogue intro_dialogue = {
    intro_pages,
    sizeof(intro_pages) / sizeof(intro_pages[0]),
    SND_TAIKO,
    SFX_TAIKO_FRAMES,           /* looped: the screen lasts as long as the
                                   player takes to read it */
};


int main(void) {
    ng_cls();
    init_palette();
    init_sky();
    init_scrolling_layer(HILLS_SPRITE, STAGE_HILLS_ROWS, STAGE_HILLS_Y);
    init_scrolling_layer(GROUND_SPRITE, STAGE_GROUND_ROWS, STAGE_GROUND_Y);
    init_entities();

    /* Lay the scrolling layers out once before anything is drawn. Their
       columns only get an X when they are scrolled, and until then they would
       all sit stacked at the left edge. */
    scroll_layer(HILLS_SPRITE, HILLS_TILE, STAGE_HILLS_ROWS, 0, &hills_tile_scroll);
    scroll_layer(GROUND_SPRITE, GROUND_TILE, STAGE_GROUND_ROWS, 0, &ground_tile_scroll);

    /* Put the sound driver in a known state before asking it for anything. */
    play_sound(SND_RESET);

    /* Start covered, so the first screen reveals like every other one. */
    cover_screen();

    for (;;) {
        if (title_screen() == MENU_QUIT) {
            dissolve_out();

            /* A cartridge has nowhere to quit to, so this is as far as it
               goes: say goodbye, then offer the title screen again. */
            ng_cls();
            cover_screen();
            hide_entities();
            show_stage(0);
            dissolve_in();
            ng_center_text(13, 0, "THANKS FOR PLAYING");
            for (u16 i = 0; i < 120; i++) {
                wait_vblank();
            }
            dissolve_out();
            continue;
        }

        dissolve_out();
        play_sound(SND_GONG);

        /* The dialogue is read against black, so nothing is left on screen
           behind it. It dissolves in itself and leaves the screen covered. */
        hide_entities();
        show_stage(0);
        dialogue_run(&intro_dialogue);

        ng_cls();
        cover_screen();
        show_stage(1);

        /* Put the characters where they will actually stand before anything
           is shown. Without this they sit at x=0 for the whole fade and then
           jump into place on the first frame of play. */
        reset_entities();
        draw_entities();

        dissolve_in();
        play_sound(SND_KOTO);
        ng_center_text(2, 0, "WASD MOVE   K JUMP   J HIT");

        for (;;) {
            wait_vblank();
            input_poll();
            update_player(player);
            update_camera(player);
            draw_entities();

            /* The far hills move at a quarter of the floor's speed, which is
               what makes them read as distant. */
            scroll_layer(HILLS_SPRITE, HILLS_TILE, STAGE_HILLS_ROWS,
                         camera_x >> 2, &hills_tile_scroll);
            scroll_layer(GROUND_SPRITE, GROUND_TILE, STAGE_GROUND_ROWS,
                         camera_x, &ground_tile_scroll);
        }
    }
    return 0;
}
