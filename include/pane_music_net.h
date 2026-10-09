#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Searching the server for something to listen to.
 *
 * Deliberately not the YouTube pane with its buttons swapped. That pane is built
 * for choosing a video by its picture and keeping a shelf of what you watched;
 * this one is built for finding a song by its name and then getting out of the way
 * while it plays. Sharing the screen made the Saved and Favourites shelves appear
 * in two places at once, which is a shelf in neither.
 *
 * So the split is by what you came to do: listen here, discover and keep over
 * there. What was saved is still reachable from the card half of this section --
 * it is a file on the card, which is exactly where that belongs.
 */

void music_net_open(void);
void music_net_exit(void);

/** Handles this frame's presses. False means B went unused: leave the section. */
bool music_net_input(uint64_t down);

void music_net_touch(int x, int y);

/** True while something has to be redrawn without waiting for a button. */
bool music_net_animating(void);

void music_net_draw(uint64_t held);
