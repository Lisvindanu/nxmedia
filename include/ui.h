#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mediavault.h"

/** One entry of the button-hint bar, drawn as a glyph badge inside a pill. */
typedef struct {
	const char *button;
	const char *label;
	/** The pad button a tap on this hint stands in for. */
	uint64_t press;
	/** Carries the accent: the one thing this screen is asking to be done. */
	bool primary;
} UiHint;

/** A rectangle in screen pixels, so callers can draw into space the UI laid out. */
typedef struct {
	int x;
	int y;
	int w;
	int h;
} UiRect;

/** One launcher tile on the home screen. */
typedef struct {
	const char *title;
	const char *blurb;
	/** False dims the tile and marks it "segera": there is nothing behind it yet. */
	bool ready;
} UiTile;

/** One line of a scrolling list: a folder, a file, a channel. */
typedef struct {
	const char *name;
	/** Right-aligned trailing column -- a size, a bitrate, a channel number. */
	const char *detail;
	/** Short word in a chip on the left saying what kind of row this is. */
	const char *kind;
	/** Tints the chip to separate the kinds at a glance. */
	bool accent;
} UiRow;

bool ui_init(char *err, size_t err_len);
void ui_exit(void);

/** Paints the background, sidebar panel and footer bar for a new frame. */
void ui_begin(void);
void ui_present(void);

/**
 * Full-bleed launcher: no sidebar, a bento of tiles with Radio as the hero. Paints
 * everything but the hint bar, so the caller still ends the frame itself.
 */
void ui_home(const UiTile *tiles, size_t count, size_t active);

/** Where the hero tile leaves room for a live globe. Valid before ui_home runs. */
UiRect ui_home_globe_slot(void);

/** Home tile under the point, or -1 for none. */
int ui_hit_home(int x, int y);

/** Eyebrow, title, subtitle. The eyebrow is what says which section this is. */
void ui_header(const char *eyebrow, const char *title, const char *subtitle);

/** `held` is the live button mask, so a button under a thumb reads as pressed. */
void ui_footer(const UiHint *hints, size_t count, uint64_t held);

/**
 * Standing control legend, one chip and its meaning per row. For the inputs a hint
 * bar cannot carry -- sticks, triggers, the touch screen -- which are exactly the
 * ones nobody discovers by guessing.
 */
void ui_legend(const UiHint *rows, size_t count, int x, int y);

/** Height ui_legend will occupy, so the caller can centre it before drawing. */
int ui_legend_height(size_t count);

/**
 * One settings line: what the option is on the left, what it is set to on the right.
 * Rows stack downwards by index, and the selected one carries the cursor.
 */
void ui_setting_row(int index, const char *label, const char *value, bool selected);

/** How many rows fit. Callers scroll their own data, so they need to know the window. */
size_t ui_rows_visible(void);

/**
 * Draws one screenful of a list. `rows` holds only what is on screen and `selected`
 * indexes into it, so scrolling stays the caller's business; `total` and `scroll`
 * are only what the scroll indicator needs to size itself.
 */
void ui_rows(const UiRow *rows, size_t count, size_t selected, size_t total, size_t scroll);

/** Stands in for a list with nothing in it, so the screen is never simply blank. */
void ui_empty(const char *label);

/* Results are cards in a grid, four across, and this is how many rows of them
 * are on screen -- so the pane can page by UI_RESULT_ROWS * 4. */
#define UI_RESULT_ROWS 2
#define UI_RESULT_COLS 4
#define UI_RESULT_CELLS (UI_RESULT_ROWS * UI_RESULT_COLS)
void ui_results(const MediaListing *listing, size_t selected, size_t scroll);

/** The row of source names above the grid: trending, history, favourites. */
void ui_result_tabs(const char *const *labels, size_t count, size_t active);

/** Visible result under the point, or -1. Rows here are not the shared size. */
int ui_hit_result(int x, int y);

/* Thumbnails are fetched and decoded off the main thread, so the cache behind the
 * results list has a life of its own. It needs http_init to have run. */
void ui_thumbs_init(void);
void ui_thumbs_exit(void);

/* ------------------------------------------------------------------- music */

/** One line of a track list: what a song is picked by, which is its name. */
typedef struct {
	/**
	 * Video id, for the row's cover. NULL draws the generated placeholder instead.
	 *
	 * A row with a picture on it is the difference between a track list and a list
	 * of filenames; every music app of the last decade leads its rows with the
	 * cover, and a number is only ever ordering nobody reads.
	 */
	const char *id;
	const char *title;
	const char *artist;
	/** Pre-formatted duration, right-aligned. Empty for something with no length. */
	const char *length;
} UiTrack;

/**
 * Everything the two music surfaces draw. One struct for both, because the strip
 * is the same information as the full screen with less room to say it in.
 */
typedef struct {
	/** Video id, for the album art. NULL falls back to the generated placeholder. */
	const char *id;
	const char *title;
	const char *artist;
	/** A third line: album, or where this is playing from. */
	const char *detail;
	/** Playing, paused, connecting -- the word, already translated. */
	const char *state;
	/** What follows this one, or NULL when nothing does. */
	const char *next;
	/** Shown by the strip when nothing is playing at all. */
	const char *idle_label;
	double position;
	/** Seconds, or <= 0 for a source with no end. No bar is drawn for those. */
	double duration;
	/** How much is buffered ahead, 0 to 1, or negative when unknown. */
	double buffered;
	bool paused;
	bool shuffle;
	bool repeat;
	/* Already translated, because the chip is drawn here but worded by the caller. */
	const char *shuffle_label;
	const char *repeat_label;
} UiNowPlaying;

size_t ui_tracks_visible(void);

/** One screenful of tracks. `scroll` only numbers the rows and sizes the indicator. */
void ui_tracks(const UiTrack *tracks, size_t count, size_t selected, size_t scroll,
		size_t total);

/** Visible track row under the point, or -1. */
int ui_hit_track(int x, int y);

/**
 * What is playing, as one piece of album art in the header's corner.
 *
 * Draws nothing when nothing is playing. A strip across the bottom was the obvious
 * alternative and it cost 84px of list to repeat what the full screen says better.
 */
void ui_now_corner(const UiNowPlaying *now);

/** Whether the point is on that art, which is how a tap opens the full screen. */
bool ui_hit_now_corner(int x, int y);

/** The whole content band given over to what is playing. */
void ui_now_playing(const UiNowPlaying *now);

/** Where a finger has to land to move the playhead on the full screen. */
UiRect ui_music_scrub(void);

/** Transport control under the point: 0 back, 1 play/pause, 2 forward, or -1. */
int ui_hit_transport(int x, int y);

/** State chip under the point: 0 shuffle, 1 repeat, or -1. */
int ui_hit_music_chip(int x, int y);

/**
 * Square album art for a video id, cropped from the cached thumbnail.
 *
 * With nothing cached yet it paints a deterministic gradient from the id and the
 * title's first character, rather than a grey hole that looks like a bug.
 */
void ui_art(const char *id, const char *title, UiRect box);

/** Byte count in the largest unit that leaves a number worth reading. */
void ui_format_size(int64_t bytes, char *out, size_t len);

/** Clock time. Hours appear only once there are any, so a song is not 0:03:42. */
void ui_format_time(double seconds, char *out, size_t len);

/** Where a video fills the screen: all of it, letterboxed to the picture's own shape. */
UiRect ui_stage(void);

/**
 * Title and playback state laid over a picture, on bands dark enough to read them
 * against. Drawn before the hint bar, which sits on the lower band.
 */
/**
 * The transport overlay. `progress` runs 0 to 1, or is negative for a source with
 * no end -- a live channel gets no bar rather than one that never fills.
 */
void ui_theater(const char *title, const char *status, double progress);

/** Where a finger has to land to move the scrub bar. */
UiRect ui_theater_scrub(void);

/** Self-contained screen: paints a whole frame and presents it. */
void ui_message(const char *label, const char *detail, const UiHint *hints, size_t count);

/* Hit tests against the layout, so that the pixel coordinates a touch arrives as
 * stay the concern of the code that decided where things go. */

/** The press value of the footer hint under the point, or 0 for none. */
uint64_t ui_hit_footer(int x, int y);

/** Index into the visible rows under the point, or -1 for none. */
int ui_hit_row(int x, int y);
