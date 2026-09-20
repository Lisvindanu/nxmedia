#include "ui_internal.h"

#include <math.h>

const SDL_Color COLOR_BG = { 0x0B, 0x0E, 0x14, 0xFF };
const SDL_Color COLOR_PANEL = { 0x14, 0x18, 0x21, 0xFF };
const SDL_Color COLOR_RULE = { 0x23, 0x2A, 0x36, 0xFF };
const SDL_Color COLOR_ROW = { 0x1A, 0x1F, 0x29, 0xFF };
const SDL_Color COLOR_ACCENT = { 0x00, 0xC3, 0xE3, 0xFF };
const SDL_Color COLOR_ON_ACCENT = { 0x07, 0x11, 0x16, 0xFF };
const SDL_Color COLOR_TEXT = { 0xEC, 0xEF, 0xF4, 0xFF };
const SDL_Color COLOR_DIM = { 0x86, 0x90, 0xA4, 0xFF };
const SDL_Color COLOR_OCEAN = { 0x0E, 0x1B, 0x2E, 0xFF };
const SDL_Color COLOR_LAND = { 0x2C, 0x3B, 0x4E, 0xFF };
const SDL_Color COLOR_LIVE = { 0x3D, 0xDC, 0x84, 0xFF };

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
