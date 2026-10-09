#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * The picture, full width, with the transport controls under it.
 *
 * It is modal over whatever pane started it rather than a pane of its own, because
 * a channel and a file on the card are watched exactly the same way once they are
 * open, and neither list has anything to offer while the picture is up.
 */

/** Puts the picture up for something that has already been handed to the player. */
void stage_begin(const char *title);

/**
 * Same, but tied to a history entry, so where this is left is remembered and
 * where it was left is returned to.
 *
 * `resume_from` of zero starts at the beginning. The jump cannot be made here:
 * the worker has only just started and the source has no duration to seek within
 * yet, so it is deferred until playback actually reports itself running.
 */
void stage_begin_tracked(const char *title, const char *id, int resume_from);

/** Stops playback and gives the screen back to the pane underneath. */
void stage_end(void);

/**
 * Whether the picture owns the screen. A source that runs out takes the stage down
 * with it; one that fails keeps it, so the reason stays on screen instead of the
 * list reappearing with nothing said.
 */
bool stage_active(void);

/** Whether the next frame has to be drawn without waiting for a button. */
bool stage_animating(void);

void stage_input(uint64_t down);

/**
 * Offers a touch to the scrub bar. True means it moved the playhead; false means
 * the finger was elsewhere and the touch is the caller's to deal with.
 */
bool stage_scrub(int x, int y);

/** Brings the title and hint bar back over a picture that has hidden them. */
void stage_wake(void);

/** Paints between ui_begin() and ui_present(), the hint bar included. */
void stage_draw(uint64_t held);
