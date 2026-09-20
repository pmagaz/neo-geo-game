/*
 * Player 1's controls, sampled once a frame.
 *
 * Every screen shares this rather than reading the pad itself, so that the
 * edge detection carries across a screen change: holding A to dismiss one
 * screen must not also count as pressing A on the next.
 */

#ifndef INPUT_H
#define INPUT_H

#include <ngdevkit/neogeo.h>

/// Buttons held down, as CNT_* masks.
extern u8 in_pad;

/// Buttons that went down this frame.
extern u8 in_pressed;

/// Start, which lives in a different register to the stick. Held, then edge.
extern u8 in_start;
extern u8 in_start_pressed;

/// Sample the controls. Call exactly once per frame, before reading the above.
void input_poll(void);

#endif /* INPUT_H */
