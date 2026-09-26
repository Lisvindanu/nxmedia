#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * YouTube by way of the MediaVault server: what is popular now, whatever you
 * search for, played as sound or kept on the card as video.
 */

/** Fetches what is trending the first time the pane is entered, then keeps it. */
void youtube_pane_open(void);

void youtube_pane_exit(void);

/** Handles this frame's presses. False means B went unused: leave the section. */
bool youtube_pane_input(uint64_t down);

void youtube_pane_touch(int x, int y);

/** Paints between ui_begin() and ui_present(), the hint bar included. */
void youtube_pane_draw(uint64_t held);
