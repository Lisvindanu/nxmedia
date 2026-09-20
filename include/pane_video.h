#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * The Video section is two things behind one tile: channels off the internet and
 * files on the card. They share nothing but the word "video", so the section opens
 * on a choice rather than guessing which one was meant.
 */

void video_pane_open(void);
void video_pane_exit(void);

/** Handles this frame's presses. False means B went unused: go back to the home screen. */
bool video_pane_input(uint64_t down);

void video_pane_touch(int x, int y);

/** Paints between ui_begin() and ui_present(), the hint bar included. */
void video_pane_draw(uint64_t held);
