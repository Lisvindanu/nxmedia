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

/** Brings the title and hint bar back over a picture that has hidden them. */
void stage_wake(void);

/** Paints between ui_begin() and ui_present(), the hint bar included. */
void stage_draw(uint64_t held);
