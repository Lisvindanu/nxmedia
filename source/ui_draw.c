#include "ui_internal.h"

#include <math.h>

/*
 * Derived from contrast ratios rather than chosen by eye, because the first attempt
 * was chosen by eye and every surface landed between 1.09:1 and 1.34:1 -- visible
 * on a desk monitor, and nothing at all on a television.
 *
 * The thing to understand is that adjacent surfaces in a dark theme cannot reach
 * WCAG 1.4.11's 3:1: from a dark field the only way to 3:1 is a mid grey, which is
 * no longer a dark theme. So the fills are only asked to be perceptible, and the
 * 3:1 is carried by what WCAG actually asks it of -- the *boundary*. Panels are
 * outlined in RULE (2.33:1) and the selected row is ringed in ACCENT (6.17:1).
 *
 * Measured against BG: PANEL 1.33, ROW 1.16, RULE 2.33. Every text colour clears
 * 4.5:1 on all three surfaces, which is why DIM had to come up from #8690A4.
 */
const SDL_Color COLOR_BG = { 0x22, 0x26, 0x2E, 0xFF };
const SDL_Color COLOR_PANEL = { 0x33, 0x3A, 0x47, 0xFF };
const SDL_Color COLOR_RULE = { 0x55, 0x5E, 0x72, 0xFF };
const SDL_Color COLOR_ROW = { 0x2B, 0x31, 0x3C, 0xFF };
const SDL_Color COLOR_ACCENT = { 0x00, 0xC3, 0xE3, 0xFF };
const SDL_Color COLOR_ON_ACCENT = { 0x07, 0x11, 0x16, 0xFF };
const SDL_Color COLOR_TEXT = { 0xEC, 0xEF, 0xF4, 0xFF };
const SDL_Color COLOR_DIM = { 0x9A, 0xA4, 0xB8, 0xFF };
const SDL_Color COLOR_OCEAN = { 0x0E, 0x1B, 0x2E, 0xFF };
const SDL_Color COLOR_LAND = { 0x2C, 0x3B, 0x4E, 0xFF };
const SDL_Color COLOR_LIVE = { 0x3D, 0xDC, 0x84, 0xFF };

/*
 * A panel and the hairline that makes its edge findable.
 *
 * The fill alone sits at 1.33:1 against the background, which is perceptible up
 * close and gone at three metres. The outline is 2.33:1, so the shape stays legible
 * whatever the television does to the midtones.
 */
void ui_panel(int x, int y, int w, int h, int radius) {
	ui_fill_round_rect(x - 1, y - 1, w + 2, h + 2, radius + 1, COLOR_RULE);
	ui_fill_round_rect(x, y, w, h, radius, COLOR_PANEL);
}

void ui_fill_rect(int x, int y, int w, int h, SDL_Color color) {
	SDL_SetRenderDrawColor(ui_renderer, color.r, color.g, color.b, color.a);
	SDL_Rect rect = { x, y, w, h };
	SDL_RenderFillRect(ui_renderer, &rect);
}

void ui_fill_round_rect(int x, int y, int w, int h, int radius, SDL_Color color) {
	if (radius * 2 > h) radius = h / 2;
	if (radius * 2 > w) radius = w / 2;
	if (radius <= 0) {
		ui_fill_rect(x, y, w, h, color);
		return;
	}

	SDL_SetRenderDrawColor(ui_renderer, color.r, color.g, color.b, color.a);
	SDL_Rect middle = { x, y + radius, w, h - 2 * radius };
	SDL_RenderFillRect(ui_renderer, &middle);

	for (int dy = 0; dy < radius; dy++) {
		int offset = radius - dy;
		int reach = (int)(sqrt((double)(radius * radius - offset * offset)) + 0.5);
		int inset = radius - reach;
		SDL_Rect top = { x + inset, y + dy, w - 2 * inset, 1 };
		SDL_Rect bottom = { x + inset, y + h - 1 - dy, w - 2 * inset, 1 };
		SDL_RenderFillRect(ui_renderer, &top);
		SDL_RenderFillRect(ui_renderer, &bottom);
	}
}
