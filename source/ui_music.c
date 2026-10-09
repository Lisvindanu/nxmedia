#include "ui_internal.h"

#include <math.h>
#include <stdio.h>

/*
 * The music surfaces: a track list, a strip that says what is playing while you
 * browse, and a full Now Playing screen.
 *
 * Kept apart from the result grid on purpose. A video is chosen by its picture, so
 * it wants a grid of pictures; a song is chosen by its name, so it wants a list --
 * and what a song needs that a video does not is somewhere for the thing currently
 * playing to live while you look for the next one.
 *
 * The proportions come from Kodi's Estuary music screen, which is authored at
 * 1920x1080, divided by 1.5 into this app's fixed 1280x720.
 */

/* --- geometry ------------------------------------------------------------ */

/* The same rows the rest of the app uses, now that those are a size a thumb can
 * hit. Carrying a second number here would be two things to keep in agreement. */
#define TRACK_TOP LIST_TOP
#define TRACK_ROW_H LIST_ROW_H
#define TRACK_ROWS LIST_ROWS
#define TRACK_GAP 8
#define TRACK_PAD 20
/* Square, and as tall as the row leaves room for once its gap is taken out. */
#define TRACK_ART (LIST_ROW_H - TRACK_GAP - 16)

/*
 * What is playing, while the list is up, is one piece of album art in the corner of
 * the header -- no title, no transport, no bar.
 *
 * A strip across the bottom cost 84px of list and repeated what the full screen
 * already says better. Sony's Music app does exactly this: a single thumbnail with
 * a ring, and one press to go to it.
 */
/* 64 rather than Sony's 85: their header has the room and our 96px one does not. */
#define CORNER_ART 64
#define CORNER_X (CONTENT_RIGHT - CORNER_ART)
#define CORNER_Y 16

/*
 * The hero. Sony gives album art about three quarters of the screen's height and
 * ElevenMPV-A about seven tenths; at 512 this is 71%, and it is why the screen
 * reads as a music player rather than as a list with a picture on it.
 *
 * There is no header band here on purpose. The art is the screen, so the 136px the
 * rest of the app spends on an eyebrow and a title would be taken from the one
 * element that is doing the work.
 */
#define ART_SIZE 512
#define ART_X CONTENT_X
#define ART_Y 100
#define COL_X (ART_X + ART_SIZE + 36)
#define COL_W (CONTENT_RIGHT - COL_X)

/*
 * The metadata stack, in Sony's order: artist, then title, then album.
 *
 * The title is sandwiched rather than on top. The eye lands on the largest thing
 * and the two dim lines frame it, which is why a magazine puts the kicker above the
 * headline. Hierarchy is carried by size and by opacity at once -- the small lines
 * are dim, not merely smaller.
 */
#define STACK_ARTIST_Y 236
#define STACK_TITLE_Y 288
#define STACK_ALBUM_Y 332

/*
 * The transport is the full width of the metadata column and alone on its line.
 *
 * Nothing sits to its left or right, so left and right on the pad always mean
 * "seek" rather than "move to the control beside it" -- entering and leaving a
 * one-dimensional control should happen perpendicular to the axis it runs along.
 */
#define SCRUB_FULL_Y 400
#define SCRUB_FULL_H 12
#define SCRUB_KNOB_R 11
/* The drawn control keeps its size; only the area that answers a touch grows. */
#define SCRUB_TOUCH_H 100

/*
 * Three transport controls at a 181px pitch, centred in the metadata column.
 *
 * The pitch is Sony's: my decode of their Vita music player and an independent
 * measurement of their own screenshot agreed on a 136px pitch at 960x544, which is
 * 181 here. Shuffle and repeat sit below as labelled chips rather than as icons --
 * a word that lights up says its state without anybody having to learn a glyph.
 */
/*
 * Everything below the art is laid out upward from the hint bar, not downward from
 * the art, because the first attempt put the chips at 638 -- two pixels under
 * FOOTER_Y, so they were drawn on top of the button hints, and the "next up" line
 * landed inside the pills themselves.
 *
 * The last thing on this screen now ends at 626, ten pixels clear of the band.
 */
#define TRANSPORT_Y 500
#define TRANSPORT_PITCH 181
#define PLAY_R 38
#define CHIP_Y 580
#define CHIP_H 34
#define NEXT_Y 616

/* Sony draws a 10px bar inside a 100px touch band; the drawn control keeps its own
 * size and only the area that answers a touch grows. */
#define TOUCH_MIN 80

#define FOCUS_RING 3

/* --- shared helpers ------------------------------------------------------ */

void ui_art_frame(UiRect box) {
	SDL_Color edge = { 0xFF, 0xFF, 0xFF, 0x1A };

	ui_fill_rect(box.x, box.y, box.w, 1, edge);
	ui_fill_rect(box.x, box.y + box.h - 1, box.w, 1, edge);
	ui_fill_rect(box.x, box.y, 1, box.h, edge);
	ui_fill_rect(box.x + box.w - 1, box.y, 1, box.h, edge);
}

SDL_Color ui_hue_colour(int hue, double saturation, double value) {
	double c = value * saturation;
	double h = (double)(hue % 360) / 60.0;
	double x = c * (1.0 - fabs(fmod(h, 2.0) - 1.0));
	double m = value - c;

	double r = m, g = m, b = m;
	if (h < 1) { r += c; g += x; }
	else if (h < 2) { r += x; g += c; }
	else if (h < 3) { g += c; b += x; }
	else if (h < 4) { g += x; b += c; }
	else if (h < 5) { r += x; b += c; }
	else { r += c; b += x; }

	return (SDL_Color){ (Uint8)(r * 255), (Uint8)(g * 255), (Uint8)(b * 255), 0xFF };
}

/*
 * A left- or right-pointing triangle, built from one-pixel strips.
 *
 * There is no polygon primitive, and a texture for five shapes would be five more
 * things to load and free. The strips are a pixel wide so the hypotenuse steps once
 * per row, which at this size is not visible.
 */
static void triangle(int cx, int cy, int half, bool right, SDL_Color colour) {
	for (int i = 0; i < half; i++) {
		int h = 2 * (half - i);
		int x = right ? cx - half + i : cx + half - i - 1;
		ui_fill_rect(x, cy - h / 2, 1, h, colour);
	}
}

/*
 * The whole screen, tinted by the cover.
 *
 * Only the hue survives: saturation and value are forced, so a pale cover and a
 * dark one give equally readable backgrounds. That is the trap in this effect --
 * tint faithfully and a bright cover turns white text invisible, which is why the
 * usual advice is to composite a heavy black scrim over it. Fixing the value
 * instead means no scrim is needed and the colour is never muddied by one.
 */
static void draw_tinted_background(const char *id) {
	SDL_Color average;
	if (!id || !ui_art_tint(id, &average)) return;

	int hue = 0;
	int max = average.r > average.g ? (average.r > average.b ? 0 : 2)
			: (average.g > average.b ? 1 : 2);
	int hi = average.r > average.g ? (average.r > average.b ? average.r : average.b)
			: (average.g > average.b ? average.g : average.b);
	int lo = average.r < average.g ? (average.r < average.b ? average.r : average.b)
			: (average.g < average.b ? average.g : average.b);
	int span = hi - lo;

	if (span > 0) {
		if (max == 0) hue = (60 * (average.g - average.b) / span + 360) % 360;
		else if (max == 1) hue = 60 * (average.b - average.r) / span + 120;
		else hue = 60 * (average.r - average.g) / span + 240;
	}

	SDL_Color top = ui_hue_colour(hue, 0.55, 0.26);
	SDL_Color bottom = ui_hue_colour(hue, 0.60, 0.07);

	for (int y = 0; y < SCREEN_H; y++) {
		double t = (double)y / (double)(SCREEN_H - 1);
		SDL_Color mix = {
			(Uint8)(top.r + (bottom.r - top.r) * t),
			(Uint8)(top.g + (bottom.g - top.g) * t),
			(Uint8)(top.b + (bottom.b - top.b) * t),
			0xFF,
		};
		ui_fill_rect(0, y, SCREEN_W, 1, mix);
	}
}

/*
 * Track, buffered-ahead, fill, knob -- four layers at one geometry.
 *
 * The buffered layer exists because everything here except a card file arrives over
 * the network: without it there is no way to tell a stream that is still filling
 * from one that has stalled, which is the question a buffering viewer is asking.
 */
static void draw_scrub(int x, int y, int w, int h, double progress, double buffered,
		bool knob) {
	ui_fill_round_rect(x, y, w, h, h / 2, COLOR_RULE);

	if (buffered > 0) {
		int ahead = (int)(buffered * w);
		SDL_Color wash = { 0xFF, 0xFF, 0xFF, 0x4F };
		if (ahead > 0) ui_fill_round_rect(x, y, ahead, h, h / 2, wash);
	}

	if (progress < 0) return;

	int filled = (int)(progress * w);
	if (filled > 0) ui_fill_round_rect(x, y, filled, h, h / 2, COLOR_ACCENT);

	if (knob) {
		/* Two concentric circles, the outer one dark: the 2px of it that shows round
		 * the edge reads as a border, without needing a stroke primitive. */
		int cx = x + filled;
		int cy = y + h / 2;
		ui_fill_round_rect(cx - SCRUB_KNOB_R, cy - SCRUB_KNOB_R,
				SCRUB_KNOB_R * 2, SCRUB_KNOB_R * 2, SCRUB_KNOB_R, COLOR_BG);
		ui_fill_round_rect(cx - SCRUB_KNOB_R + 2, cy - SCRUB_KNOB_R + 2,
				(SCRUB_KNOB_R - 2) * 2, (SCRUB_KNOB_R - 2) * 2, SCRUB_KNOB_R - 2,
				COLOR_TEXT);
	}
}

static double fraction(const UiNowPlaying *now) {
	if (now->duration <= 0) return -1.0;

	double f = now->position / now->duration;
	return f < 0 ? 0 : (f > 1 ? 1 : f);
}

/* --- the track list ------------------------------------------------------- */

size_t ui_tracks_visible(void) {
	return TRACK_ROWS;
}

void ui_tracks(const UiTrack *tracks, size_t count, size_t selected, size_t scroll,
		size_t total) {
	(void)scroll;
	if (count > TRACK_ROWS) count = TRACK_ROWS;

	for (size_t i = 0; i < count; i++) {
		const UiTrack *t = &tracks[i];
		int y = TRACK_TOP + (int)i * TRACK_ROW_H;
		int mid = y + TRACK_ROW_H / 2;
		int h = TRACK_ROW_H - TRACK_GAP;
		bool on = i == selected;

		if (on) {
			ui_fill_round_rect(CONTENT_X - FOCUS_RING, y + TRACK_GAP / 2 - FOCUS_RING,
					CONTENT_W + 2 * FOCUS_RING, h + 2 * FOCUS_RING, 10 + FOCUS_RING,
					COLOR_ACCENT);
			ui_fill_round_rect(CONTENT_X, y + TRACK_GAP / 2, CONTENT_W, h, 10, COLOR_ROW);
		}

		UiRect cover = { CONTENT_X + TRACK_PAD, mid - TRACK_ART / 2, TRACK_ART, TRACK_ART };
		ui_art(t->id, t->title, cover);

		int right = CONTENT_RIGHT - TRACK_PAD;
		if (t->length && t->length[0]) {
			ui_draw_text_right(right, mid, FONT_SMALL, COLOR_DIM, t->length, 0);
			right -= ui_measure_text(FONT_SMALL, t->length) + 24;
		}

		/*
		 * Title and artist stacked, not side by side. Side by side, a long title ate
		 * the artist's column and the rows stopped lining up; stacked, each has its
		 * own line and the row is the same shape whatever is in it.
		 */
		int name_x = cover.x + TRACK_ART + 18;
		ui_draw_text(name_x, mid - 13, FONT_ROW, on ? COLOR_ACCENT : COLOR_TEXT,
				t->title, right - name_x);
		if (t->artist && t->artist[0]) {
			ui_draw_text(name_x, mid + 15, FONT_SMALL, COLOR_DIM, t->artist,
					right - name_x);
		}
	}

	if (total > TRACK_ROWS) {
		int track_h = TRACK_ROWS * TRACK_ROW_H;
		int thumb_h = (int)((double)track_h * TRACK_ROWS / (double)total);
		if (thumb_h < 30) thumb_h = 30;

		double span = (double)(total - TRACK_ROWS);
		int offset = (int)((double)(track_h - thumb_h) * (double)scroll / span);

		ui_fill_round_rect(1203, TRACK_TOP, 5, track_h, 2, COLOR_RULE);
		ui_fill_round_rect(1203, TRACK_TOP + offset, 5, thumb_h, 2, COLOR_ACCENT);
	}
}

int ui_hit_track(int x, int y) {
	if (x < CONTENT_X || x >= CONTENT_RIGHT) return -1;
	if (y < TRACK_TOP || y >= TRACK_TOP + TRACK_ROWS * TRACK_ROW_H) return -1;

	return (y - TRACK_TOP) / TRACK_ROW_H;
}

/* --- the corner --------------------------------------------------------- */

bool ui_hit_now_corner(int x, int y) {
	return x >= CORNER_X - 8 && x < CORNER_X + CORNER_ART + 8 &&
			y >= CORNER_Y - 8 && y < CORNER_Y + CORNER_ART + 8;
}

void ui_now_corner(const UiNowPlaying *now) {
	if (!now->title || !now->title[0]) return;

	UiRect art = { CORNER_X, CORNER_Y, CORNER_ART, CORNER_ART };

	/* The ring is what says this is a thing you can press, rather than a decoration
	 * that happens to be a picture. */
	ui_fill_round_rect(art.x - 3, art.y - 3, art.w + 6, art.h + 6, 9, COLOR_ACCENT);
	ui_art(now->id, now->title, art);
}

/* --- the full screen ------------------------------------------------------ */

UiRect ui_music_scrub(void) {
	/* A band, not the bar. Sony's own progress control is 10px drawn inside a 100px
	 * touch area; twelve pixels is not something a thumb can be asked to hit. */
	return (UiRect){ COL_X, SCRUB_FULL_Y + SCRUB_FULL_H / 2 - SCRUB_TOUCH_H / 2,
			COL_W, SCRUB_TOUCH_H };
}

static int transport_centre(int slot) {
	int middle = (COL_X + CONTENT_RIGHT) / 2;
	return middle + (slot - 1) * TRANSPORT_PITCH;
}

int ui_hit_transport(int x, int y) {
	for (int slot = 0; slot < 3; slot++) {
		int cx = transport_centre(slot);
		if (x >= cx - TOUCH_MIN / 2 && x < cx + TOUCH_MIN / 2 &&
				y >= TRANSPORT_Y - TOUCH_MIN / 2 && y < TRANSPORT_Y + TOUCH_MIN / 2) {
			return slot;
		}
	}
	return -1;
}

int ui_hit_music_chip(int x, int y) {
	if (y < CHIP_Y - CHIP_H / 2 - 8 || y >= CHIP_Y + CHIP_H / 2 + 8) return -1;
	if (x < COL_X || x >= CONTENT_RIGHT) return -1;

	return x < (COL_X + CONTENT_RIGHT) / 2 ? 0 : 1;
}

static void draw_chip(int x, int w, const char *label, bool on) {
	SDL_Color fill = on ? COLOR_ACCENT : COLOR_RULE;
	SDL_Color ink = on ? COLOR_ON_ACCENT : COLOR_DIM;

	ui_fill_round_rect(x, CHIP_Y - CHIP_H / 2, w, CHIP_H, CHIP_H / 2, fill);

	int text_w = ui_measure_text(FONT_SMALL, label);
	ui_draw_text(x + (w - text_w) / 2, CHIP_Y, FONT_SMALL, ink, label, w);
}

static void draw_transport(const UiNowPlaying *now) {
	/* Previous and next are a bar against a triangle, the shape every transport has
	 * used since tape, so nothing here has to be learned. */
	int back = transport_centre(0);
	triangle(back + 4, TRANSPORT_Y, 14, false, COLOR_TEXT);
	ui_fill_rect(back - 14, TRANSPORT_Y - 14, 4, 28, COLOR_TEXT);

	int forward = transport_centre(2);
	triangle(forward - 4, TRANSPORT_Y, 14, true, COLOR_TEXT);
	ui_fill_rect(forward + 10, TRANSPORT_Y - 14, 4, 28, COLOR_TEXT);

	/* The middle one is the only filled circle on the screen, which is how it reads
	 * as the thing to press without being labelled. */
	int middle = transport_centre(1);
	ui_fill_round_rect(middle - PLAY_R, TRANSPORT_Y - PLAY_R, PLAY_R * 2, PLAY_R * 2,
			PLAY_R, COLOR_ACCENT);

	if (now->paused) {
		triangle(middle + 3, TRANSPORT_Y, 15, true, COLOR_ON_ACCENT);
	} else {
		ui_fill_rect(middle - 11, TRANSPORT_Y - 15, 7, 30, COLOR_ON_ACCENT);
		ui_fill_rect(middle + 4, TRANSPORT_Y - 15, 7, 30, COLOR_ON_ACCENT);
	}

}

void ui_now_playing(const UiNowPlaying *now) {
	draw_tinted_background(now->id);

	UiRect art = { ART_X, ART_Y, ART_SIZE, ART_SIZE };
	ui_art(now->id, now->title, art);

	if (now->artist && now->artist[0]) {
		ui_draw_text(COL_X, STACK_ARTIST_Y, FONT_SIDE, COLOR_DIM, now->artist, COL_W);
	}

	ui_draw_text(COL_X, STACK_TITLE_Y, FONT_TITLE, COLOR_TEXT,
			now->title ? now->title : "", COL_W);

	if (now->detail && now->detail[0]) {
		ui_draw_text(COL_X, STACK_ALBUM_Y, FONT_SIDE, COLOR_DIM, now->detail, COL_W);
	}

	/* Elapsed in the accent, total dim: the number that is changing is the one worth
	 * looking at, and colour says so without a second glance. */
	char at[16];
	ui_format_time(now->position, at, sizeof(at));
	ui_draw_text(COL_X, SCRUB_FULL_Y - 24, FONT_SIDE, COLOR_ACCENT, at, COL_W / 2);

	if (now->duration > 0) {
		char total[16];
		ui_format_time(now->duration, total, sizeof(total));
		ui_draw_text_right(CONTENT_RIGHT, SCRUB_FULL_Y - 24, FONT_SIDE, COLOR_DIM,
				total, COL_W / 2);
	}

	draw_scrub(COL_X, SCRUB_FULL_Y, COL_W, SCRUB_FULL_H, fraction(now), now->buffered,
			now->duration > 0);

	if (now->state && now->state[0]) {
		ui_draw_text(COL_X, SCRUB_FULL_Y + 40, FONT_SMALL,
				now->paused ? COLOR_DIM : COLOR_LIVE, now->state, COL_W);
	}

	draw_transport(now);

	int half = (CONTENT_RIGHT - COL_X) / 2;
	draw_chip(COL_X, half - 8, now->shuffle_label, now->shuffle);
	draw_chip(COL_X + half + 8, half - 8, now->repeat_label, now->repeat);

	if (now->next && now->next[0]) {
		ui_draw_text(COL_X, NEXT_Y, FONT_SMALL, COLOR_DIM, now->next, COL_W);
	}
}
