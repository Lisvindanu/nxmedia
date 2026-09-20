#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Indonesian free-to-air channels, taken from the iptv-org playlist and cached on
 * the card so a console with no network still has a list to show.
 */

/** Fetches the playlist the first time the pane is entered, then keeps it. */
void iptv_pane_open(void);

void iptv_pane_exit(void);

/** Handles this frame's presses. False means B went unused: leave the section. */
bool iptv_pane_input(uint64_t down);

void iptv_pane_touch(int x, int y);

/** Paints between ui_begin() and ui_present(), the hint bar included. */
void iptv_pane_draw(uint64_t held);
