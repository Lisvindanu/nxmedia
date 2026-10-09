#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pane_media.h"

/*
 * The Music section is two places to find a song: the card, and the server.
 *
 * They are kept behind one tile rather than two because "I want to listen to
 * something" is one intention -- where the file happens to live is a detail, and
 * making the user pick the right tile first is making them answer a question they
 * did not ask.
 */

void music_pane_open(void);
void music_pane_exit(void);

/** Handles this frame's presses. False means B went unused: back to the home screen. */
bool music_pane_input(uint64_t down);

void music_pane_touch(int x, int y);

/** True while the music section has something moving that needs the next frame. */
bool music_pane_animating(void);

void music_pane_draw(uint64_t held);
