#include "globe.h"

#include <math.h>
#include <stdio.h>

#include "ui_internal.h"
#include "world.h"

/* Outlines are drawn as line strips, so a ring is one SDL call if it fits. Rings
 * longer than this are split, which no country at 110m resolution reaches. */
#define STRIP_MAX 1024

/* The globe is free to grow past its panel when zoomed, so everything it draws is
 * confined to the rectangle the caller placed it in. */
#define VIEW_TOP (HEADER_RULE_Y + 1)
#define VIEW_BOTTOM FOOTER_Y

void globe_place(Globe *globe, int x, int y, int w, int h, int radius) {
	globe->centre_x = x + w / 2;
	globe->centre_y = y + h / 2;
	globe->radius = radius;
	globe->view_x = x;
	globe->view_y = y;
	globe->view_w = w;
	globe->view_h = h;
}

void globe_init(Globe *globe) {
	globe->lat = -2.0f;
	globe->lon = 121.6f;
	globe->zoom = 1.0f;
	globe_place(globe, CONTENT_X, VIEW_TOP, CONTENT_W, VIEW_BOTTOM - VIEW_TOP, 246);
}

static SDL_Rect view_of(const Globe *globe) {
	return (SDL_Rect){ globe->view_x, globe->view_y, globe->view_w, globe->view_h };
}

static void fill_disc(const Camera *cam, SDL_Rect view, SDL_Color color) {
	int radius = (int)(cam->radius + 0.5f);
	int cx = (int)(cam->cx + 0.5f);
	int cy = (int)(cam->cy + 0.5f);

	SDL_SetRenderDrawColor(ui_renderer, color.r, color.g, color.b, color.a);

	/* Zoomed in, most of the disc is off-panel. Walking only the rows that can be
	 * seen keeps this from becoming thousands of discarded draw calls. */
	int first = view.y - cy;
	int last = view.y + view.h - 1 - cy;
	if (first < -radius) first = -radius;
	if (last > radius) last = radius;

	for (int dy = first; dy <= last; dy++) {
		int half = (int)(sqrtf((float)radius * radius - (float)dy * dy) + 0.5f);
		SDL_Rect span = { cx - half, cy + dy, half * 2, 1 };
		SDL_RenderFillRect(ui_renderer, &span);
	}
}

static void stroke_ring(const Camera *cam, size_t ring, SDL_Color color) {
	const WorldRing *info = world_ring(ring);
	SDL_Point strip[STRIP_MAX];
	int used = 0;

	SDL_SetRenderDrawColor(ui_renderer, color.r, color.g, color.b, color.a);

	for (uint32_t i = 0; i <= info->count; i++) {
		float lon, lat, sx, sy;
		world_ring_point(ring, i % info->count, &lon, &lat);

		if (!camera_project(cam, lon, lat, &sx, &sy) || used == STRIP_MAX) {
			/* A vertex that rolled over the horizon breaks the strip: joining
			 * across it would draw a chord straight through the globe. */
			if (used > 1) SDL_RenderDrawLines(ui_renderer, strip, used);
			used = 0;
			continue;
		}

		strip[used].x = (int)(sx + 0.5f);
		strip[used].y = (int)(sy + 0.5f);
		used++;
	}

	if (used > 1) SDL_RenderDrawLines(ui_renderer, strip, used);
}

void globe_draw(const Globe *globe, size_t highlight) {
	Camera cam = camera_of(globe);
	SDL_Rect view = view_of(globe);

	SDL_RenderSetClipRect(ui_renderer, &view);
	fill_disc(&cam, view, COLOR_OCEAN);

	for (size_t c = 0; c < world_country_count(); c++) {
		const WorldCountry *country = world_country(c);
		SDL_Color ink = c == highlight ? COLOR_ACCENT : COLOR_LAND;

		for (uint32_t k = 0; k < country->ring_count; k++) {
			stroke_ring(&cam, country->first_ring + k, ink);
		}
	}

	SDL_RenderSetClipRect(ui_renderer, NULL);

	if (globe->zoom > 1.01f) {
		char label[16];
		snprintf(label, sizeof(label), "%.1fx", (double)globe->zoom);

		int w = ui_measure_text(FONT_SMALL, label);
		int right = view.x + view.w;
		ui_fill_round_rect(right - w - 40, view.y + 14, w + 24, 30, 15, COLOR_ROW);
		ui_draw_text(right - w - 28, view.y + 29, FONT_SMALL, COLOR_DIM, label, 0);
	}
}

void globe_mark(const Globe *globe, float lat, float lon, bool active) {
	Camera cam = camera_of(globe);

	float sx, sy;
	if (!camera_project(&cam, lon, lat, &sx, &sy)) return;

	int x = (int)(sx + 0.5f);
	int y = (int)(sy + 0.5f);
	int size = active ? 9 : 5;

	SDL_Rect view = view_of(globe);
	SDL_RenderSetClipRect(ui_renderer, &view);
	ui_fill_round_rect(x - size / 2, y - size / 2, size, size, size / 2,
			active ? COLOR_LIVE : COLOR_ACCENT);
	SDL_RenderSetClipRect(ui_renderer, NULL);
}
