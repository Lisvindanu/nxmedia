#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "ui.h"

/*
 * Shared by the ui_*.c files and nothing else. The screen is a fixed 1280x720, so
 * the layout is expressed as plain coordinates rather than a layout pass: there is
 * only ever one size to lay out for.
 */

#define SCREEN_W 1280
#define SCREEN_H 720

/* Every screen is full-bleed. Sections are reached from the launcher and left with
 * B, so a standing nav rail would only repeat the home screen. */
#define CONTENT_X 64
#define CONTENT_RIGHT 1216
#define CONTENT_W (CONTENT_RIGHT - CONTENT_X)

/*
 * 96, down from 136.
 *
 * Nintendo's own header is 88 and RetroArch's Horizon-imitating one is 87; ours was
 * 48px taller for no reason anybody could name, and that 48px came straight out of
 * the list. Three lines no longer fit above the rule, so the subtitle moved to the
 * left of the hint bar -- which is where nx-hbmenu has always put its context line,
 * and which was empty space we were already reserving.
 */
#define HEADER_RULE_Y 96
#define FOOTER_Y 636

/* The scrub bar sits above the hint bar, inside the same darkened band. */
#define SCRUB_Y 610
#define SCRUB_H 4
#define SCRUB_KNOB 14

/*
 * The list fills the gap between the header rule and the hint bar.
 *
 * 84px rows rather than 52. At 52 a row was 5.57mm on the 6.2" screen -- under every
 * published touch floor, and under what a thumb can hit without aiming; 84 is
 * exactly 9.00mm, which is where the research clusters. Six of them land on 628,
 * eight pixels clear of the first hint pill, so nothing is cropped at either end.
 *
 * Fewer, larger rows is the deliberate trade. A list is read before it is tapped.
 */
#define LIST_TOP (HEADER_RULE_Y + 28)
#define LIST_ROW_H 84
#define LIST_ROWS 6

enum {
	FONT_TITLE,
	FONT_ROW,
	FONT_SIDE,
	FONT_SMALL,
	FONT_DISPLAY,
	FONT_COUNT,
};

extern SDL_Renderer *ui_renderer;
extern TTF_Font *ui_fonts[FONT_COUNT];

extern const SDL_Color COLOR_BG;
extern const SDL_Color COLOR_PANEL;
extern const SDL_Color COLOR_RULE;
extern const SDL_Color COLOR_ROW;
extern const SDL_Color COLOR_ACCENT;
extern const SDL_Color COLOR_ON_ACCENT;
extern const SDL_Color COLOR_TEXT;
extern const SDL_Color COLOR_DIM;
extern const SDL_Color COLOR_OCEAN;
extern const SDL_Color COLOR_LAND;
extern const SDL_Color COLOR_LIVE;

/* --------------------------------------------------------------------- text */

/** Draws text with its vertical centre at mid_y; returns the width drawn. */
int ui_draw_text(int x, int mid_y, int font, SDL_Color color, const char *text, int clip_w);

/** Draws text ending at right, for a column that lines up on its right edge. */
void ui_draw_text_right(int right, int mid_y, int font, SDL_Color color, const char *text,
		int clip_w);

/** Width the string would occupy, used to reserve room before drawing it. */
int ui_measure_text(int font, const char *text);

/** Drops every cached glyph texture. Called on shutdown and when the table fills. */
void ui_cache_clear(void);

/* ------------------------------------------------------------------- shapes */

void ui_fill_rect(int x, int y, int w, int h, SDL_Color color);

/** A COLOR_PANEL surface with a COLOR_RULE hairline, so its edge survives a TV. */
void ui_panel(int x, int y, int w, int h, int radius);
void ui_fill_round_rect(int x, int y, int w, int h, int radius, SDL_Color color);

/* -------------------------------------------------------------------- frame */

/** Paints the background, sidebar panel and footer panel every screen sits on. */
void ui_draw_shell(void);

/* ------------------------------------------------------------------- music */

/** A hairline frame around album art, so it has an edge against any background. */
void ui_art_frame(UiRect box);

/** A colour at this hue, for the deterministic art placeholder. s and v are 0..1. */
SDL_Color ui_hue_colour(int hue, double saturation, double value);

/**
 * The average colour of this id's cover, if it has already been fetched.
 *
 * Lives here rather than in ui.h because only the drawing code can use it -- a pane
 * has no business knowing what colour anything is. False means there is nothing to
 * tint from yet, so draw what would have been drawn anyway rather than wait.
 */
bool ui_art_tint(const char *id, SDL_Color *out);
