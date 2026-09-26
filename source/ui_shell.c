#include "ui_internal.h"

#include <stdio.h>

#include <switch.h>

#define CARD_W 828
#define CARD_X ((SCREEN_W - CARD_W) / 2)

/* A hint is a pill: badge, gap, label, breathing room. Sized off a fingertip
 * rather than off the glyph, because the bar doubles as a row of touch targets. */
#define HINT_PILL_H 44
#define HINT_BADGE 28
#define HINT_PAD_L 8
#define HINT_PAD_R 16
#define HINT_GAP 10
#define HINT_SPACE 12

static const int FONT_SIZES[FONT_COUNT] = { 30, 24, 23, 19, 54 };

static SDL_Window *window;
SDL_Renderer *ui_renderer;
TTF_Font *ui_fonts[FONT_COUNT];

/* ------------------------------------------------------------------ init */

static bool open_shared_font(char *err, size_t err_len) {
	PlFontData data;
	if (R_FAILED(plGetSharedFontByType(&data, PlSharedFontType_Standard))) {
		snprintf(err, err_len, "tidak bisa membaca font sistem");
		return false;
	}

	for (int i = 0; i < FONT_COUNT; i++) {
		/* Each size needs its own face, and each needs its own RWops because
		 * TTF_OpenFontRW takes ownership of the one it is handed. */
		SDL_RWops *stream = SDL_RWFromConstMem(data.address, (int)data.size);
		if (!stream) {
			snprintf(err, err_len, "tidak bisa membuka font sistem");
			return false;
		}

		ui_fonts[i] = TTF_OpenFontRW(stream, 1, FONT_SIZES[i]);
		if (!ui_fonts[i]) {
			snprintf(err, err_len, "TTF_OpenFontRW: %.180s", TTF_GetError());
			return false;
		}
	}

	return true;
}

bool ui_init(char *err, size_t err_len) {
	if (R_FAILED(plInitialize(PlServiceType_User))) {
		snprintf(err, err_len, "plInitialize gagal");
		return false;
	}

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
		snprintf(err, err_len, "SDL_Init: %.180s", SDL_GetError());
		return false;
	}

	if (TTF_Init() != 0) {
		snprintf(err, err_len, "TTF_Init: %.180s", TTF_GetError());
		return false;
	}

	window = SDL_CreateWindow("nxmedia", 0, 0, SCREEN_W, SCREEN_H, 0);
	if (!window) {
		snprintf(err, err_len, "SDL_CreateWindow: %.180s", SDL_GetError());
		return false;
	}

	ui_renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!ui_renderer) {
		snprintf(err, err_len, "SDL_CreateRenderer: %.180s", SDL_GetError());
		return false;
	}

	SDL_SetRenderDrawBlendMode(ui_renderer, SDL_BLENDMODE_BLEND);
	return open_shared_font(err, err_len);
}

void ui_exit(void) {
	ui_cache_clear();

	for (int i = 0; i < FONT_COUNT; i++) {
		if (ui_fonts[i]) TTF_CloseFont(ui_fonts[i]);
		ui_fonts[i] = NULL;
	}

	if (ui_renderer) SDL_DestroyRenderer(ui_renderer);
	if (window) SDL_DestroyWindow(window);
	ui_renderer = NULL;
	window = NULL;

	TTF_Quit();
	SDL_Quit();
	plExit();
}

/* ----------------------------------------------------------------- frame */

void ui_draw_shell(void) {
	ui_fill_rect(0, 0, SCREEN_W, SCREEN_H, COLOR_BG);
	ui_fill_rect(0, FOOTER_Y, SCREEN_W, SCREEN_H - FOOTER_Y, COLOR_PANEL);
	ui_fill_rect(0, FOOTER_Y, SCREEN_W, 1, COLOR_RULE);
}

void ui_begin(void) {
	ui_draw_shell();
	ui_fill_rect(CONTENT_X, HEADER_RULE_Y, CONTENT_RIGHT - CONTENT_X, 1, COLOR_RULE);
}

void ui_present(void) {
	SDL_RenderPresent(ui_renderer);
}

void ui_header(const char *eyebrow, const char *title, const char *subtitle) {
	ui_draw_text(CONTENT_X + 2, 44, FONT_SMALL, COLOR_ACCENT, eyebrow, CONTENT_W);
	ui_draw_text(CONTENT_X, 82, FONT_TITLE, COLOR_TEXT, title, CONTENT_W);
	ui_draw_text(CONTENT_X + 1, 116, FONT_SMALL, COLOR_DIM, subtitle, CONTENT_W);
}

/* Dark enough to read white text against a bright picture, light enough that the
 * picture is still there underneath it. */
#define SCRIM_H 116

UiRect ui_theater_scrub(void) {
	/* A band rather than the bar itself: the track is four pixels tall, which no
	 * thumb can be expected to land on. */
	return (UiRect){ CONTENT_X, SCRUB_Y - 18, CONTENT_W, 40 };
}

void ui_theater(const char *title, const char *status, double progress) {
	SDL_Color scrim = { 0x00, 0x00, 0x00, 0xAA };
	ui_fill_rect(0, 0, SCREEN_W, SCRIM_H, scrim);
	ui_fill_rect(0, SCRUB_Y - 24, SCREEN_W, SCREEN_H - SCRUB_Y + 24, scrim);

	ui_draw_text(CONTENT_X, 48, FONT_TITLE, COLOR_TEXT, title, CONTENT_W);
	ui_draw_text(CONTENT_X + 1, 86, FONT_SMALL, COLOR_DIM, status, CONTENT_W);

	/* A live stream has no end to travel towards, so it gets no bar at all rather
	 * than one that never moves. */
	if (progress < 0) return;
	if (progress > 1.0) progress = 1.0;

	int filled = (int)(CONTENT_W * progress);

	ui_fill_round_rect(CONTENT_X, SCRUB_Y, CONTENT_W, SCRUB_H, SCRUB_H / 2, COLOR_RULE);
	if (filled > 0) {
		ui_fill_round_rect(CONTENT_X, SCRUB_Y, filled, SCRUB_H, SCRUB_H / 2, COLOR_ACCENT);
	}

	/* The knob is what says the bar can be moved, not just watched. */
	ui_fill_round_rect(CONTENT_X + filled - SCRUB_KNOB / 2, SCRUB_Y + SCRUB_H / 2 - SCRUB_KNOB / 2,
			SCRUB_KNOB, SCRUB_KNOB, SCRUB_KNOB / 2, COLOR_TEXT);
}

void ui_message(const char *label, const char *detail, const UiHint *hints, size_t count) {
	ui_draw_shell();

	int h = detail && detail[0] ? 200 : 140;
	int y = (SCREEN_H - h) / 2 - 40;
	ui_fill_round_rect(CARD_X - 1, y - 1, CARD_W + 2, h + 2, 17, COLOR_RULE);
	ui_fill_round_rect(CARD_X, y, CARD_W, h, 16, COLOR_PANEL);

	ui_draw_text(CARD_X + 36, y + 52, FONT_TITLE, COLOR_TEXT, label, CARD_W - 72);
	if (detail && detail[0]) {
		ui_draw_text(CARD_X + 36, y + 116, FONT_SMALL, COLOR_DIM, detail, CARD_W - 72);
	}

	/* Called even with nothing to draw, so a screen without a bar retires the hint
	 * boxes rather than leaving the previous screen's still tappable. */
	ui_footer(hints, hints ? count : 0, 0);
	ui_present();
}

/* A fixed chip column is what makes the rows line up as a list rather than as four
 * loose scraps of text. */
#define LEGEND_W 252
#define LEGEND_ROW 42
#define LEGEND_PAD 16
#define LEGEND_CHIP_W 80
#define LEGEND_CHIP_H 28

int ui_legend_height(size_t count) {
	return LEGEND_PAD * 2 + (int)count * LEGEND_ROW;
}

void ui_legend(const UiHint *rows, size_t count, int x, int y) {
	ui_fill_round_rect(x, y, LEGEND_W, ui_legend_height(count), 16, COLOR_PANEL);

	int chip_x = x + LEGEND_PAD;
	int label_x = chip_x + LEGEND_CHIP_W + 12;

	for (size_t i = 0; i < count; i++) {
		int mid = y + LEGEND_PAD + (int)i * LEGEND_ROW + LEGEND_ROW / 2;

		ui_fill_round_rect(chip_x, mid - LEGEND_CHIP_H / 2, LEGEND_CHIP_W, LEGEND_CHIP_H,
				LEGEND_CHIP_H / 2, COLOR_ROW);

		int w = ui_measure_text(FONT_SMALL, rows[i].button);
		ui_draw_text(chip_x + (LEGEND_CHIP_W - w) / 2, mid, FONT_SMALL, COLOR_TEXT,
				rows[i].button, 0);
		ui_draw_text(label_x, mid, FONT_SMALL, COLOR_DIM, rows[i].label,
				x + LEGEND_W - LEGEND_PAD - label_x);
	}
}

#define SETTING_Y 196
#define SETTING_H 76
#define SETTING_GAP 16
#define SETTING_PAD 28
#define SETTING_CURSOR_W 6

void ui_setting_row(int index, const char *label, const char *value, bool selected) {
	int y = SETTING_Y + index * (SETTING_H + SETTING_GAP);
	int mid = y + SETTING_H / 2;

	ui_fill_round_rect(CONTENT_X, y, CONTENT_W, SETTING_H, 16, selected ? COLOR_ROW : COLOR_PANEL);
	if (selected) ui_fill_round_rect(CONTENT_X, y, SETTING_CURSOR_W, SETTING_H, 3, COLOR_ACCENT);

	/* The value carries update failures, which are long enough to run past the label
	 * and off the panel unless they are clipped to whatever the label leaves. */
	int label_w = ui_draw_text(CONTENT_X + SETTING_PAD, mid, FONT_ROW,
			selected ? COLOR_TEXT : COLOR_DIM, label, CONTENT_W / 2);
	ui_draw_text_right(CONTENT_RIGHT - SETTING_PAD, mid, FONT_ROW, COLOR_ACCENT, value,
			CONTENT_W - SETTING_PAD * 2 - label_w - SETTING_GAP);
}

/* Hint boxes fall out of measured text widths, so they are only known once drawn.
 * Recording them here is what lets a tap on the bar act as that button. */
#define HINT_BOX_MAX 8

static struct {
	int x;
	int w;
	uint64_t press;
} hint_box[HINT_BOX_MAX];

static size_t hint_box_count;

static SDL_Color shade(SDL_Color color, int percent) {
	return (SDL_Color){ (Uint8)(color.r * percent / 100), (Uint8)(color.g * percent / 100),
			(Uint8)(color.b * percent / 100), color.a };
}

void ui_footer(const UiHint *hints, size_t count, uint64_t held) {
	int mid = FOOTER_Y + (SCREEN_H - FOOTER_Y) / 2;
	int top = mid - HINT_PILL_H / 2;

	hint_box_count = 0;
	if (count > HINT_BOX_MAX) count = HINT_BOX_MAX;

	/* The bar sits against the right edge, so the whole run has to be measured
	 * before the first pill knows where it starts. */
	int width[HINT_BOX_MAX];
	int total = 0;
	for (size_t i = 0; i < count; i++) {
		width[i] = HINT_PAD_L + HINT_BADGE + HINT_GAP
				+ ui_measure_text(FONT_SMALL, hints[i].label) + HINT_PAD_R;
		total += width[i] + (i ? HINT_SPACE : 0);
	}

	int x = CONTENT_RIGHT - total;

	for (size_t i = 0; i < count; i++) {
		bool primary = hints[i].primary;
		bool down = hints[i].press && (held & hints[i].press);

		SDL_Color pill = primary ? COLOR_ACCENT : COLOR_ROW;
		SDL_Color badge = primary ? COLOR_ON_ACCENT : COLOR_RULE;
		SDL_Color glyph = primary ? COLOR_ACCENT : COLOR_TEXT;
		SDL_Color label = primary ? COLOR_ON_ACCENT : COLOR_DIM;

		if (down) {
			pill = primary ? shade(COLOR_ACCENT, 72) : COLOR_RULE;
			if (!primary) label = COLOR_TEXT;
		}

		ui_fill_round_rect(x, top, width[i], HINT_PILL_H, HINT_PILL_H / 2, pill);
		ui_fill_round_rect(x + HINT_PAD_L, mid - HINT_BADGE / 2, HINT_BADGE, HINT_BADGE,
				HINT_BADGE / 2, badge);

		int glyph_w = ui_measure_text(FONT_SMALL, hints[i].button);
		ui_draw_text(x + HINT_PAD_L + (HINT_BADGE - glyph_w) / 2, mid, FONT_SMALL, glyph,
				hints[i].button, 0);
		ui_draw_text(x + HINT_PAD_L + HINT_BADGE + HINT_GAP, mid, FONT_SMALL, label,
				hints[i].label, 0);

		/* The whole pill is the target: the badge alone is narrower than a fingertip,
		 * and the label reads as part of the same button. */
		hint_box[hint_box_count].x = x;
		hint_box[hint_box_count].w = width[i];
		hint_box[hint_box_count].press = hints[i].press;
		hint_box_count++;

		x += width[i] + HINT_SPACE;
	}
}

/* ----------------------------------------------------------------- touch */

uint64_t ui_hit_footer(int x, int y) {
	if (y < FOOTER_Y) return 0;

	for (size_t i = 0; i < hint_box_count; i++) {
		if (x >= hint_box[i].x && x < hint_box[i].x + hint_box[i].w) return hint_box[i].press;
	}

	return 0;
}
