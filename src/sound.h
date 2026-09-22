/*
 * Asking the Z80 for a sound.
 *
 * The 68000 cannot reach the sound chip. It writes a single byte to REG_SOUND,
 * which fires an NMI on the Z80; src/user_commands.s is the table of what each
 * number means, and these must stay in step with it. Commands 0 to 3 are
 * reserved by nullsound.
 */

#ifndef SOUND_H
#define SOUND_H

#include <ngdevkit/neogeo.h>

#define SND_NONE 0      /* not a command: "play nothing" for callers */
#define SND_RESET 3
#define SND_GONG 4
#define SND_JUMP 5
#define SND_PUNCH 6     /* the swing */
#define SND_TAIKO 7
#define SND_KOTO 8
#define SND_HIT 9       /* the blow landing, which is a different sound */
#define SND_STEP 10

static inline void play_sound(u8 command) {
    *REG_SOUND = command;
}

#endif /* SOUND_H */
