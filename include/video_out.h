#pragma once

#include <stdbool.h>

#include <SDL2/SDL.h>

#include "ui.h"

struct AVFrame;

/*
 * A short queue of decoded frames and the texture they are shown through.
 *
 * The decoder thread fills the queue, the render loop takes whichever frame is due
 * against the audio clock, and frames that fell behind are dropped rather than shown
 * late. Everything is converted to YUV420P on the way in, so the renderer does the
 * colour conversion on the GPU and no software scaler runs per frame.
 */

/* Deep enough to swallow a whole interleave chunk, because a muxer is free to write
 * a second of video before the first audio packet and the decoder cannot start until
 * it has reached that audio. At 1080p about 75 MB, which on a console with gigabytes
 * free is not worth economising on. */
#define VIDEO_QUEUE_MAX 24

bool video_out_init(void);
void video_out_exit(void);

/**
 * Blocks while the queue is full; false means an interrupt landed mid-wait.
 *
 * With `may_drop` the frame is given up instead of waiting. Nothing takes frames out
 * of the queue until the clock is running, so a push before then must never block.
 */
bool video_out_push(const struct AVFrame *frame, double pts, bool may_drop);

/** Drops every queued frame but keeps the picture up. For a seek. */
void video_out_flush(void);

/** Drops the queue and the picture with it, so the next source starts on a blank. */
void video_out_clear(void);

/** Releases a blocked video_out_push so a stopping worker is not stuck waiting. */
void video_out_interrupt(bool on);

/**
 * Shows the frame due at `clock`, scaled to fit `box` with its aspect kept. The last
 * frame shown stays up when nothing new is due, so a paused picture does not blink.
 * False before the first frame has arrived.
 */
bool video_out_draw(double clock, UiRect box);
