#include "ui_internal.h"

#include "i18n.h"

/*
 * The launcher. Not a four-up grid of equal cards: Radio is the whole reason the
 * app exists, so it takes a hero tile with a live globe in it and the rest sit
 * around it as satellites. The bento is irregular on purpose -- equal tiles would
 * say all four sections are equally finished, and three of them are not.
 */

#define HOME_X CONTENT_X
#define HOME_RIGHT CONTENT_RIGHT
#define TILE_TOP 168
#define TILE_BOTTOM 604
#define TILE_GAP 16
#define TILE_RADIUS 20

#define HERO_W 668
#define SIDE_X (HOME_X + HERO_W + TILE_GAP)
#define SIDE_W (HOME_RIGHT - SIDE_X)
/* Three rows down the right column, not two: YouTube earns a full-width band of
 * its own between them, because it is the one tile that brings something new in
 * from outside rather than listing what is already on the card. */
#define SIDE_H ((TILE_BOTTOM - TILE_TOP - 2 * TILE_GAP) / 3)
#define SIDE_MID_Y (TILE_TOP + SIDE_H + TILE_GAP)
#define SIDE_LOW_Y (TILE_TOP + 2 * (SIDE_H + TILE_GAP))
#define SIDE_HALF_W ((SIDE_W - TILE_GAP) / 2)

/* Room kept clear below the globe so the hero's own words never sit on top of it. */
#define HERO_TEXT_H 128
#define HERO_INSET 24

static const UiRect TILE[] = {
	{ HOME_X, TILE_TOP, HERO_W, TILE_BOTTOM - TILE_TOP },
	{ SIDE_X, TILE_TOP, SIDE_W, SIDE_H },
	{ SIDE_X, SIDE_MID_Y, SIDE_W, SIDE_H },
	{ SIDE_X, SIDE_LOW_Y, SIDE_HALF_W, SIDE_H },
	{ SIDE_X + SIDE_HALF_W + TILE_GAP, SIDE_LOW_Y, SIDE_HALF_W, SIDE_H },
};

#define TILE_COUNT (int)(sizeof(TILE) / sizeof(TILE[0]))

UiRect ui_home_globe_slot(void) {
	UiRect hero = TILE[0];
	return (UiRect){
		hero.x + HERO_INSET,
		hero.y + HERO_INSET,
		hero.w - 2 * HERO_INSET,
		hero.h - HERO_INSET - HERO_TEXT_H,
	};
}

static void draw_soon_badge(UiRect r) {
	const char *text = T(STR_SOON);
	int w = ui_measure_text(FONT_SMALL, text);
	ui_fill_round_rect(r.x + r.w - w - 44, r.y + 20, w + 24, 28, 14, COLOR_RULE);
	ui_draw_text(r.x + r.w - w - 32, r.y + 34, FONT_SMALL, COLOR_DIM, text, 0);
}

static void draw_tile(UiRect r, const UiTile *item, bool active, bool hero) {
	/* The ring is drawn as a slightly larger tile behind, which is the only way to
	 * get an outline out of a fill-only primitive. */
	if (active) ui_fill_round_rect(r.x - 3, r.y - 3, r.w + 6, r.h + 6, TILE_RADIUS + 3, COLOR_ACCENT);
	ui_fill_round_rect(r.x, r.y, r.w, r.h, TILE_RADIUS, active ? COLOR_ROW : COLOR_PANEL);

	if (!item->ready) draw_soon_badge(r);

	int title_font = hero ? FONT_TITLE : FONT_SIDE;
	int title_y = r.y + r.h - (hero ? 74 : 62);
	int blurb_y = r.y + r.h - (hero ? 36 : 30);
	int wrap = r.w - 2 * HERO_INSET;

	ui_draw_text(r.x + HERO_INSET, title_y, title_font,
			item->ready ? COLOR_TEXT : COLOR_DIM, item->title, wrap);
	ui_draw_text(r.x + HERO_INSET + 1, blurb_y, FONT_SMALL, COLOR_DIM, item->blurb, wrap);
}

void ui_home(const UiTile *tiles, size_t count, size_t active) {
	ui_draw_shell();

	ui_draw_text(HOME_X + 2, 58, FONT_SMALL, COLOR_ACCENT, T(STR_HOME_EYEBROW), 600);
	ui_draw_text(HOME_X, 106, FONT_DISPLAY, COLOR_TEXT, "nxmedia", 700);

	if (count > (size_t)TILE_COUNT) count = TILE_COUNT;
	for (size_t i = 0; i < count; i++) {
		draw_tile(TILE[i], &tiles[i], i == active, i == 0);
	}
}

int ui_hit_home(int x, int y) {
	for (int i = 0; i < TILE_COUNT; i++) {
		if (x >= TILE[i].x && x < TILE[i].x + TILE[i].w
				&& y >= TILE[i].y && y < TILE[i].y + TILE[i].h) {
			return i;
		}
	}

	return -1;
}
