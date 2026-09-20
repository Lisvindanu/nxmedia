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

#define HEADER_RULE_Y 136
#define FOOTER_Y 636

/* The list fills the gap between the header rule and the hint bar. Eight rows is
 * what a 52px row leaves room for without crowding either edge. */
#define LIST_TOP 164
#define LIST_ROW_H 52
#define LIST_ROWS 8

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
void ui_fill_round_rect(int x, int y, int w, int h, int radius, SDL_Color color);

/* -------------------------------------------------------------------- frame */

/** Paints the background, sidebar panel and footer panel every screen sits on. */
void ui_draw_shell(void);
