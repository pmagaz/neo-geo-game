/*
 * A dialogue screen: a portrait on the left, a page of text on the right,
 * advanced a page at a time with the A button.
 *
 * These sit between the game's screens - before a stage, after a boss - so the
 * screen itself is generic and the script is data:
 *
 *     static const char *const intro_pages[] = { "...", "...", "..." };
 *     static const struct dialogue intro = {
 *         intro_pages, 3, SND_TAIKO, SFX_TAIKO_FRAMES,
 *     };
 *     dialogue_run(&intro);
 *
 * The caller sets the sprites up first - usually by hiding them, so the
 * dialogue is read against black. dialogue_run dissolves in, runs until the
 * last page is dismissed, and leaves the screen covered for whatever comes
 * next.
 */

#ifndef DIALOGUE_H
#define DIALOGUE_H

#include <ngdevkit/neogeo.h>

struct dialogue {
    /// One string per page. Each is wrapped to the text box; line breaks in
    /// the source are not needed and spaces are collapsed at the wrap.
    const char *const *pages;
    u8 page_count;
    /// Sound command fired once the screen appears, or SND_NONE.
    u8 music;
    /// Frames after which to fire `music` again, so a track plays for as long
    /// as the screen is up; 0 plays it once. The YM2610 only loops ADPCM-B in
    /// hardware, and these samples are ADPCM-A, so a loop is a retrigger -
    /// pass the sample's own length, which the makefile generates into
    /// build/assets/sfx.h as SFX_<NAME>_FRAMES.
    u16 music_loop;
};

void dialogue_run(const struct dialogue *d);

#endif /* DIALOGUE_H */
