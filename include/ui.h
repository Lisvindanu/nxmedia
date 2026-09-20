#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
void ui_theater(const char *title, const char *status);

/** Self-contained screen: paints a whole frame and presents it. */
void ui_message(const char *label, const char *detail, const UiHint *hints, size_t count);

/* Hit tests against the layout, so that the pixel coordinates a touch arrives as
 * stay the concern of the code that decided where things go. */

/** The press value of the footer hint under the point, or 0 for none. */
uint64_t ui_hit_footer(int x, int y);

/** Index into the visible rows under the point, or -1 for none. */
int ui_hit_row(int x, int y);
