#include "ui_internal.h"

#include <stdio.h>

#define SCROLLBAR_X 1252
#define SCROLLBAR_W 5

#define ROW_GAP 6
#define ROW_PAD 20
#define CHIP_W 74
#define CHIP_H 26
#define CHIP_GAP 16

size_t ui_rows_visible(void) {
	return LIST_ROWS;
}

UiRect ui_stage(void) {
	return (UiRect){ 0, 0, SCREEN_W, SCREEN_H };
}

void ui_empty(const char *label) {
	ui_draw_text(CONTENT_X, LIST_TOP + 60, FONT_ROW, COLOR_DIM, label, CONTENT_W);
}

void ui_format_size(int64_t bytes, char *out, size_t len) {
	double value = (double)bytes;

	if (bytes < 1024) snprintf(out, len, "%lld B", (long long)bytes);
	else if (bytes < 1024 * 1024) snprintf(out, len, "%.0f KB", value / 1024.0);
	else if (bytes < 1024LL * 1024 * 1024) snprintf(out, len, "%.1f MB", value / (1024.0 * 1024.0));
	else snprintf(out, len, "%.2f GB", value / (1024.0 * 1024.0 * 1024.0));
}

void ui_format_time(double seconds, char *out, size_t len) {
	if (seconds < 0) seconds = 0;
	int whole = (int)seconds;

	if (whole >= 3600) {
		snprintf(out, len, "%d:%02d:%02d", whole / 3600, (whole / 60) % 60, whole % 60);
	} else {
		snprintf(out, len, "%d:%02d", whole / 60, whole % 60);
	}
}

static void draw_scrollbar(size_t total, size_t scroll) {
	if (total <= LIST_ROWS) return;

	int track_h = LIST_ROWS * LIST_ROW_H;
	ui_fill_round_rect(SCROLLBAR_X, LIST_TOP, SCROLLBAR_W, track_h, 2, COLOR_ROW);

	int thumb_h = (int)((double)track_h * LIST_ROWS / (double)total);
	if (thumb_h < 30) thumb_h = 30;

	double span = (double)(total - LIST_ROWS);
	int offset = (int)((double)(track_h - thumb_h) * (double)scroll / span);
	ui_fill_round_rect(SCROLLBAR_X, LIST_TOP + offset, SCROLLBAR_W, thumb_h, 2, COLOR_ACCENT);
}

static void draw_row(size_t row, const UiRow *item, bool selected) {
	int y = LIST_TOP + (int)row * LIST_ROW_H;
	int mid = y + LIST_ROW_H / 2;
	int h = LIST_ROW_H - ROW_GAP;

	if (selected) {
		ui_fill_round_rect(CONTENT_X, y + ROW_GAP / 2, CONTENT_W, h, 10, COLOR_ROW);
		ui_fill_round_rect(CONTENT_X, y + ROW_GAP / 2, 4, h, 2, COLOR_ACCENT);
	}

	SDL_Color chip = item->accent ? COLOR_ACCENT : COLOR_RULE;
	SDL_Color chip_text = item->accent ? COLOR_ON_ACCENT : COLOR_DIM;

	int chip_x = CONTENT_X + ROW_PAD;
	ui_fill_round_rect(chip_x, mid - CHIP_H / 2, CHIP_W, CHIP_H, CHIP_H / 2, chip);
	int chip_w = ui_measure_text(FONT_SMALL, item->kind);
	ui_draw_text(chip_x + (CHIP_W - chip_w) / 2, mid, FONT_SMALL, chip_text, item->kind, 0);

	int detail_w = item->detail ? ui_measure_text(FONT_SMALL, item->detail) : 0;
	int name_x = chip_x + CHIP_W + CHIP_GAP;
	int name_clip = CONTENT_RIGHT - ROW_PAD - detail_w - CHIP_GAP - name_x;

	ui_draw_text(name_x, mid, FONT_ROW, selected ? COLOR_TEXT : COLOR_DIM, item->name, name_clip);
	if (item->detail) {
		ui_draw_text_right(CONTENT_RIGHT - ROW_PAD, mid, FONT_SMALL, COLOR_DIM, item->detail, 0);
	}
}

void ui_rows(const UiRow *rows, size_t count, size_t selected, size_t total, size_t scroll) {
	if (count > LIST_ROWS) count = LIST_ROWS;

	for (size_t row = 0; row < count; row++) {
		draw_row(row, &rows[row], row == selected);
	}

	draw_scrollbar(total, scroll);
}

int ui_hit_row(int x, int y) {
	if (x < CONTENT_X || x >= CONTENT_RIGHT) return -1;
	if (y < LIST_TOP || y >= LIST_TOP + LIST_ROWS * LIST_ROW_H) return -1;

	return (y - LIST_TOP) / LIST_ROW_H;
}
