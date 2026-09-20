#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "ui.h"

/*
 * Plays one thing at a time: a radio station, an IPTV channel, a file on the card.
 *
 * A worker thread demuxes and decodes with ffmpeg. Sound goes to a ring buffer the
 * SDL audio callback drains; pictures go to a short frame queue the render loop
 * drains, timed against the sound that is actually leaving the speakers. Playback
 * therefore survives the screen going dark, because nothing here depends on the
 * render loop running.
 *
 * Video is decoded on the Tegra's NVDEC block -- a Cortex-A57 cannot software-decode
 * 1080p -- and its VIC block converts frames on the way out, so neither the decode
 * nor the colour conversion costs CPU.
 *
 * https:// sources need sslInitialize() to have run -- see main.c.
 */

typedef enum {
	PlayerState_Idle,
	PlayerState_Connecting,
	PlayerState_Playing,
	/** The source ran out. Normal for a file, the end of the road for a stream. */
	PlayerState_Ended,
	PlayerState_Error,
} PlayerState;

bool player_init(char *err, size_t err_len);
void player_exit(void);

/**
 * Headers an IPTV host demands before it will serve a stream. Either field may be
 * NULL, and so may the struct itself: a file on the card and a radio station both
 * open fine with whatever ffmpeg sends by default.
 */
typedef struct {
	char *referer;
	char *user_agent;
} PlayerHeaders;

/**
 * Replaces whatever is playing. Returns once the worker starts, not once connected.
 *
 * `headers` is copied, so the caller keeps ownership of the strings it points at.
 */
bool player_play(const char *url, const PlayerHeaders *headers, char *err, size_t err_len);
void player_stop(void);

PlayerState player_state(void);
const char *player_error(void);

void player_set_volume(int percent);
int player_volume(void);

/** Whether the source that is open carries pictures as well as sound. */
bool player_has_video(void);

/** Shows the frame due now, fitted into `box`. False before the first one arrives. */
bool player_draw_video(UiRect box);

/** Seconds played, and the total. Both -1 when unknown: a live stream has no end. */
double player_position(void);
double player_duration(void);

void player_set_paused(bool paused);
bool player_is_paused(void);

/** Whether the source can be jumped around in at all. A live stream cannot. */
bool player_can_seek(void);

/** Jumps to `seconds`, clamped to the source. Does nothing when seeking is refused. */
void player_seek(double seconds);
