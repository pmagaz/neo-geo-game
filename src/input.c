#include <ngdevkit/neogeo.h>
#include "input.h"

u8 in_pad = 0;
u8 in_pressed = 0;
u8 in_start = 0;
u8 in_start_pressed = 0;


/*
 * Both registers are active low - a bit reads 0 while its switch is held - so
 * they are inverted to get the "1 means pressed" convention of the CNT_* masks.
 *
 * The BIOS keeps its own copy of the pad state, but nullbios does not maintain
 * it, so this reads the hardware.
 */
void input_poll(void) {
    u8 pad = (u8)~(*REG_P1CNT);
    in_pressed = (u8)(pad & ~in_pad);
    in_pad = pad;

    u8 start = (u8)((u8)~(*REG_STATUS_B) & CNT_START1);
    in_start_pressed = (u8)(start & ~in_start);
    in_start = start;
}
