#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "library.h"

/*
 * Walks the SD card for something to play. Music and video each get their own
 * instance, so browsing for a song does not move the cursor in the film folder and
 * neither list is cluttered with files the other one wanted.
 *
 * Video hands off to the stage once it starts; audio keeps the list up, so a song
 * carries on playing while the next one is being looked for.
 */

typedef struct {
	LibraryKind kind;
	char dir[LIBRARY_PATH_MAX];
	LibraryListing listing;
	size_t selected;
	size_t scroll;
	/** What is playing, for the header to keep naming after the list has moved on. */
	char title[128];
	/** Whatever went wrong last, shown in place of the hint line. */
	char note[160];
} MediaPane;

/** Starts at the root the first time and where the user left off after that. */
void media_pane_open(MediaPane *pane, LibraryKind kind);

/** Releases the listing. Playback is the player's to stop, not the pane's. */
void media_pane_exit(MediaPane *pane);

/** Handles this frame's presses. False means B went unused: leave the section. */
bool media_pane_input(MediaPane *pane, uint64_t down);

/** A tap inside the list. Ignored elsewhere on the screen. */
void media_pane_touch(MediaPane *pane, int x, int y);

/** Paints between ui_begin() and ui_present(), the hint bar included. */
void media_pane_draw(MediaPane *pane, const char *eyebrow, uint64_t held);
