#include "ui_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <switch.h>

#include "http.h"
#include "i18n.h"

/*
 * Search results, and the thumbnail cache behind them.
 *
 * Pictures arrive over the network and have to be decoded, neither of which fits
 * inside a frame, so a second thread does both while the list keeps drawing. Only
 * the upload to the GPU happens on this thread: an SDL_Texture belongs to the
 * renderer and may not be touched from anywhere else.
 *
 * JPEG is decoded with the ffmpeg already linked for playback rather than by
 * adding an image library. A second JPEG decoder in the binary would earn nothing.
 */

#define THUMB_SLOTS 24

/* Four across the content strip, two rows deep. Sized for a screen you look at
 * from a sofa rather than a list you scan from a desk: eight big pictures beat
 * five rows of small ones when the only pointer is a thumbstick. */
#define COLS 4
#define ROWS 2
#define CARD_GAP 20
#define CARD_W ((CONTENT_W - (COLS - 1) * CARD_GAP) / COLS)
#define THUMB_W CARD_W
#define THUMB_H (CARD_W * 9 / 16)
#define CARD_TEXT_H 54
#define CARD_H (THUMB_H + CARD_TEXT_H)
#define CARD_ROW_GAP 24
/* Thin enough to read as a mark on the picture rather than as part of the card. */
#define PROGRESS_H 4
#define CARD_PITCH_Y (CARD_H + CARD_ROW_GAP)

/* How far the focused card grows past its slot on every side. The pictures either
 * side stay put, so the grown one reads as lifted rather than as a shifted grid. */
#define FOCUS_GROW 8

/* In the gap the header rule already leaves above the grid, so the strip costs the
 * cards no height. Follows the rule rather than repeating its number. */
#define TAB_Y (HEADER_RULE_Y + 4)
#define TAB_H 22

/* Decoded at the size the focused card draws it, not the size the others do, so
 * growing one never stretches it past the pixels it actually has. The unfocused
 * cards shrink it slightly instead, which is the direction that looks fine. */
#define DECODE_W (THUMB_W + 2 * FOCUS_GROW)
#define DECODE_H (THUMB_H + 2 * FOCUS_GROW)
/* 320x180 and about twelve kilobytes. The 720p variant is twenty times the bytes
 * for a picture drawn at a seventh of the size. */
#define THUMB_URL "https://i.ytimg.com/vi/%s/mqdefault.jpg"

static const SDL_Color COLOR_SHADE = { 0x0A, 0x0D, 0x12, 0xC8 };

typedef enum {
	SLOT_FREE,
	/* Asked for by the list, not yet picked up by the fetch thread. */
	SLOT_WANTED,
	SLOT_FETCHING,
	/* Pixels are in memory; the next frame turns them into a texture. */
	SLOT_DECODED,
	SLOT_READY,
	SLOT_FAILED,
} SlotState;

typedef struct {
	char id[32];
	SlotState state;
	uint8_t *pixels;
	SDL_Texture *texture;
	uint64_t touched;
	/*
	 * The cover's average colour, kept because the pixels are not.
	 *
	 * Computed on the fetch thread while the buffer is still in hand -- it is freed
	 * as soon as the texture exists, and reading it back out of a texture would mean
	 * a round trip from the GPU for three bytes.
	 */
	SDL_Color tint;
	bool has_tint;
} ThumbSlot;

static struct {
	Mutex lock;
	CondVar wake;
	Thread thread;
	ThumbSlot slots[THUMB_SLOTS];
	uint64_t clock;
	bool running;
	bool started;
} thumbs;

/* ------------------------------------------------------------------ decoding */

/** Decodes a JPEG straight to RGBA at display size. Caller frees the buffer. */
static uint8_t *decode_jpeg(const uint8_t *data, size_t len) {
	const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_MJPEG);
	if (!codec) return NULL;

	AVCodecContext *ctx = avcodec_alloc_context3(codec);
	if (!ctx) return NULL;

	uint8_t *rgba = NULL;
	AVPacket *packet = NULL;
	AVFrame *frame = NULL;

	if (avcodec_open2(ctx, codec, NULL) < 0) goto done;

	packet = av_packet_alloc();
	frame = av_frame_alloc();
	if (!packet || !frame) goto done;

	/* The buffer outlives the call, so ffmpeg may read it in place rather than
	 * being handed a copy it would only read once anyway. */
	packet->data = (uint8_t *)data;
	packet->size = (int)len;

	if (avcodec_send_packet(ctx, packet) < 0) goto done;
	if (avcodec_receive_frame(ctx, frame) < 0) goto done;

	/* Scaled here rather than at draw time: the thumbnail is never shown at any
	 * other size, so keeping the full 320x180 would be seven times the memory for
	 * a picture nobody sees. */
	struct SwsContext *sws = sws_getContext(frame->width, frame->height,
			(enum AVPixelFormat)frame->format, DECODE_W, DECODE_H, AV_PIX_FMT_RGBA,
			SWS_BILINEAR, NULL, NULL, NULL);
	if (!sws) goto done;

	rgba = malloc((size_t)DECODE_W * DECODE_H * 4);
	if (rgba) {
		uint8_t *dst[4] = { rgba, NULL, NULL, NULL };
		int stride[4] = { DECODE_W * 4, 0, 0, 0 };
		sws_scale(sws, (const uint8_t *const *)frame->data, frame->linesize, 0,
				frame->height, dst, stride);
	}
	sws_freeContext(sws);

done:
	av_frame_free(&frame);
	av_packet_free(&packet);
	avcodec_free_context(&ctx);
	return rgba;
}

/*
 * The average colour of a decoded cover.
 *
 * An average rather than a dominant-colour search: a histogram of a 289x178 picture
 * costs more than it buys here, because the hue is all that survives -- saturation
 * and lightness are forced to fixed values when the background is drawn, so that a
 * pale cover and a dark one give equally readable results. Every fourth pixel is
 * enough for a hue.
 */
static SDL_Color average_colour(const uint8_t *rgba) {
	uint64_t r = 0, g = 0, b = 0, n = 0;

	for (int i = 0; i < DECODE_W * DECODE_H; i += 4) {
		r += rgba[i * 4 + 0];
		g += rgba[i * 4 + 1];
		b += rgba[i * 4 + 2];
		n++;
	}

	if (n == 0) return (SDL_Color){ 0x80, 0x80, 0x80, 0xFF };
	return (SDL_Color){ (Uint8)(r / n), (Uint8)(g / n), (Uint8)(b / n), 0xFF };
}

/* ------------------------------------------------------------------ fetching */

static void release_slot(ThumbSlot *slot) {
	free(slot->pixels);
	if (slot->texture) SDL_DestroyTexture(slot->texture);
	*slot = (ThumbSlot){0};
}

/** Finds a slot for this id, evicting the one left alone longest if none is free. */
static ThumbSlot *claim_slot(const char *id) {
	ThumbSlot *oldest = NULL;

	for (size_t i = 0; i < THUMB_SLOTS; i++) {
		ThumbSlot *slot = &thumbs.slots[i];
		if (slot->state != SLOT_FREE && strcmp(slot->id, id) == 0) return slot;
		if (slot->state == SLOT_FREE) return slot;

		/* A fetch in flight writes to the slot from the other thread, so it is never
		 * a candidate for reuse however long it has been sitting there. */
		if (slot->state == SLOT_FETCHING) continue;
		if (!oldest || slot->touched < oldest->touched) oldest = slot;
	}

	if (!oldest) return NULL;
	release_slot(oldest);
	return oldest;
}

static void fetch_worker(void *arg) {
	(void)arg;

	for (;;) {
		mutexLock(&thumbs.lock);

		ThumbSlot *slot = NULL;
		while (thumbs.running && !slot) {
			for (size_t i = 0; i < THUMB_SLOTS && !slot; i++) {
				if (thumbs.slots[i].state == SLOT_WANTED) slot = &thumbs.slots[i];
			}
			if (!slot) condvarWait(&thumbs.wake, &thumbs.lock);
		}

		if (!thumbs.running) {
			mutexUnlock(&thumbs.lock);
			return;
		}

		char id[32];
		snprintf(id, sizeof(id), "%s", slot->id);
		slot->state = SLOT_FETCHING;
		mutexUnlock(&thumbs.lock);

		char url[128];
		snprintf(url, sizeof(url), THUMB_URL, id);

		char err[128];
		HttpBuffer body = {0};
		uint8_t *pixels = NULL;

		if (http_get(url, NULL, &body, err, sizeof(err)) && body.data && body.len) {
			pixels = decode_jpeg((const uint8_t *)body.data, body.len);
		}
		http_buffer_free(&body);

		mutexLock(&thumbs.lock);
		/* The slot may have been given away while the picture was in the air, in
		 * which case these pixels belong to nobody. */
		if (slot->state == SLOT_FETCHING && strcmp(slot->id, id) == 0) {
			slot->pixels = pixels;
			if (pixels) {
				slot->tint = average_colour(pixels);
				slot->has_tint = true;
			}
			slot->state = pixels ? SLOT_DECODED : SLOT_FAILED;
		} else {
			free(pixels);
		}
		mutexUnlock(&thumbs.lock);
	}
}

void ui_thumbs_init(void) {
	if (thumbs.started) return;

	/* Photographs shrink badly under nearest sampling, and every card but the
	 * focused one is shrunk. Set before any thumbnail texture exists, since the
	 * hint is read at creation. */
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

	mutexInit(&thumbs.lock);
	condvarInit(&thumbs.wake);
	thumbs.running = true;

	if (R_FAILED(threadCreate(&thumbs.thread, fetch_worker, NULL, NULL, 128 * 1024, 0x2E, -2))) {
		thumbs.running = false;
		return;
	}
	if (R_FAILED(threadStart(&thumbs.thread))) {
		threadClose(&thumbs.thread);
		thumbs.running = false;
		return;
	}
	thumbs.started = true;
}

void ui_thumbs_exit(void) {
	if (!thumbs.started) return;

	mutexLock(&thumbs.lock);
	thumbs.running = false;
	condvarWakeAll(&thumbs.wake);
	mutexUnlock(&thumbs.lock);

	threadWaitForExit(&thumbs.thread);
	threadClose(&thumbs.thread);
	thumbs.started = false;

	for (size_t i = 0; i < THUMB_SLOTS; i++) release_slot(&thumbs.slots[i]);
}

/**
 * The texture for this video, or NULL while it is still coming. Asking is what
 * queues the fetch, so a picture is only pulled for a row someone can see.
 */
static SDL_Texture *thumb_for(const char *id) {
	if (!thumbs.started || !id || !id[0]) return NULL;

	mutexLock(&thumbs.lock);

	ThumbSlot *slot = claim_slot(id);
	if (!slot) {
		mutexUnlock(&thumbs.lock);
		return NULL;
	}

	if (slot->state == SLOT_FREE) {
		snprintf(slot->id, sizeof(slot->id), "%s", id);
		slot->state = SLOT_WANTED;
		condvarWakeOne(&thumbs.wake);
	}

	slot->touched = ++thumbs.clock;

	if (slot->state == SLOT_DECODED) {
		slot->texture = SDL_CreateTexture(ui_renderer, SDL_PIXELFORMAT_RGBA32,
				SDL_TEXTUREACCESS_STATIC, DECODE_W, DECODE_H);
		if (slot->texture) {
			SDL_UpdateTexture(slot->texture, NULL, slot->pixels, DECODE_W * 4);
		}
		free(slot->pixels);
		slot->pixels = NULL;
		slot->state = slot->texture ? SLOT_READY : SLOT_FAILED;
	}

	SDL_Texture *texture = slot->state == SLOT_READY ? slot->texture : NULL;
	mutexUnlock(&thumbs.lock);
	return texture;
}

/*
 * The cover's colour, for whatever wants to be tinted by it.
 *
 * Does not queue a fetch of its own: it answers only for a picture the list has
 * already asked for, so a background cannot be the reason a thumbnail is pulled.
 */
bool ui_art_tint(const char *id, SDL_Color *out) {
	if (!thumbs.started || !id || !id[0]) return false;

	mutexLock(&thumbs.lock);

	bool found = false;
	for (size_t i = 0; i < THUMB_SLOTS; i++) {
		ThumbSlot *slot = &thumbs.slots[i];
		if (slot->has_tint && strcmp(slot->id, id) == 0) {
			*out = slot->tint;
			found = true;
			break;
		}
	}

	mutexUnlock(&thumbs.lock);
	return found;
}

/*
 * The same cached picture, drawn as album art.
 *
 * A thumbnail is 16:9 and art is square, so a centred square is taken out of the
 * middle of it rather than squashing the whole frame -- a stretched face is more
 * obviously wrong than a cropped background.
 *
 * With nothing cached the box is filled from the id instead of left grey: a hue
 * derived from the id, a vertical two-stop gradient in it, and the first character
 * of the title over the top. It is deterministic, so the same track always looks
 * the same, and it reads as a design rather than as something that failed to load.
 */
void ui_art(const char *id, const char *title, UiRect box) {
	SDL_Texture *texture = thumb_for(id);

	if (texture) {
		int side = DECODE_H < DECODE_W ? DECODE_H : DECODE_W;
		SDL_Rect src = { (DECODE_W - side) / 2, (DECODE_H - side) / 2, side, side };
		SDL_Rect dst = { box.x, box.y, box.w, box.h };

		SDL_RenderCopy(ui_renderer, texture, &src, &dst);
		ui_art_frame(box);
		return;
	}

	unsigned hash = 2166136261u;
	for (const char *c = id ? id : ""; *c; c++) hash = (hash ^ (unsigned char)*c) * 16777619u;

	SDL_Color top = ui_hue_colour(hash % 360, 0.30, 0.42);
	SDL_Color bottom = ui_hue_colour(hash % 360, 0.34, 0.24);

	for (int i = 0; i < box.h; i++) {
		double t = (double)i / (double)(box.h - 1);
		SDL_Color mix = {
			(Uint8)(top.r + (bottom.r - top.r) * t),
			(Uint8)(top.g + (bottom.g - top.g) * t),
			(Uint8)(top.b + (bottom.b - top.b) * t),
			0xFF,
		};
		ui_fill_rect(box.x, box.y + i, box.w, 1, mix);
	}

	/* One character, not the whole title: at this size anything longer is a texture
	 * of grey rather than a letter. */
	if (title && title[0]) {
		char initial[5] = {0};
		int len = 1;
		unsigned char lead = (unsigned char)title[0];
		if ((lead & 0xE0) == 0xC0) len = 2;
		else if ((lead & 0xF0) == 0xE0) len = 3;
		else if ((lead & 0xF8) == 0xF0) len = 4;
		for (int i = 0; i < len && title[i]; i++) initial[i] = title[i];

		SDL_Color wash = { 0xFF, 0xFF, 0xFF, 0x2E };
		int w = ui_measure_text(FONT_DISPLAY, initial);
		ui_draw_text(box.x + (box.w - w) / 2, box.y + box.h / 2, FONT_DISPLAY, wash, initial, 0);
	}

	ui_art_frame(box);
}

/* ------------------------------------------------------------------- drawing */

static void format_duration(int seconds, char *out, size_t out_len) {
	if (seconds <= 0) {
		snprintf(out, out_len, "--:--");
		return;
	}

	int hours = seconds / 3600;
	int minutes = (seconds % 3600) / 60;
	int secs = seconds % 60;

	if (hours > 0) snprintf(out, out_len, "%d:%02d:%02d", hours, minutes, secs);
	else snprintf(out, out_len, "%d:%02d", minutes, secs);
}

/** Compact view count: the exact number is noise at a glance. */
static void format_views(int64_t views, char *out, size_t out_len) {
	if (views >= 1000000000) snprintf(out, out_len, "%.1fB", views / 1000000000.0);
	else if (views >= 1000000) snprintf(out, out_len, "%.1fM", views / 1000000.0);
	else if (views >= 1000) snprintf(out, out_len, "%.0fK", views / 1000.0);
	else snprintf(out, out_len, "%lld", (long long)views);
}

/*
 * The thin bar along the bottom edge saying how far in this was left.
 *
 * Drawn on the picture rather than written as a time next to the title, because
 * at a glance the useful question is "roughly where was I", and a bar answers it
 * without being read. Nothing is drawn for something never left part-way, so an
 * untouched grid looks exactly as it did before this existed.
 */
static void draw_progress(const MediaItem *item, int x, int y, int w, int h) {
	if (item->position <= 0 || item->duration <= 0) return;

	double fraction = (double)item->position / (double)item->duration;
	if (fraction > 1.0) fraction = 1.0;

	int filled = (int)(fraction * w);
	/* A sliver is still worth a pixel: a bar that rounds away to nothing reads as
	 * never watched, which is the one thing it exists to deny. */
	if (filled < 2) filled = 2;

	ui_fill_rect(x, y + h - PROGRESS_H, w, PROGRESS_H, COLOR_SHADE);
	ui_fill_rect(x, y + h - PROGRESS_H, filled, PROGRESS_H, COLOR_LIVE);
}

static void draw_thumb(const MediaItem *item, int x, int y, int w, int h) {
	SDL_Texture *texture = thumb_for(item->id);

	if (!texture) {
		ui_panel(x, y, w, h, 8);
		/* A play triangle rather than a spinner: it says "video" while it waits and
		 * needs no frame clock to look right. */
		int cx = x + w / 2 - 6;
		int cy = y + h / 2;
		for (int i = 0; i < 14; i++) ui_fill_rect(cx + i, cy - 13 + i, 1, 26 - 2 * i, COLOR_RULE);
		draw_progress(item, x, y, w, h);
		return;
	}

	SDL_Rect dst = { x, y, w, h };
	SDL_RenderCopy(ui_renderer, texture, NULL, &dst);

	/* A video with no length is a live stream. It gets a chip of its own rather
	 * than "--:--", because the difference is not that the length is unknown but
	 * that the thing cannot be fetched at all. */
	bool live = item->duration <= 0;

	char time[24];
	snprintf(time, sizeof(time), "%s", live ? T(STR_LIVE_CHIP) : "");
	if (!live) format_duration(item->duration, time, sizeof(time));

	int badge_w = ui_measure_text(FONT_SMALL, time) + 14;
	int badge_x = x + w - badge_w - 8;
	int badge_y = y + h - 26;

	ui_fill_round_rect(badge_x, badge_y, badge_w, 20, 4, live ? COLOR_LIVE : COLOR_SHADE);
	ui_draw_text(badge_x + 7, badge_y + 10, FONT_SMALL,
			live ? COLOR_ON_ACCENT : COLOR_TEXT, time, badge_w);

	draw_progress(item, x, y, w, h);
}

static void draw_card(const MediaItem *item, int x, int y, bool focused) {
	int tx = x, ty = y, tw = THUMB_W, th = THUMB_H;

	if (focused) {
		/* Grown outwards from its own slot rather than pushing the others along, so
		 * the grid stays where the eye left it. */
		tx -= FOCUS_GROW;
		ty -= FOCUS_GROW;
		tw += 2 * FOCUS_GROW;
		th += 2 * FOCUS_GROW;
		ui_fill_round_rect(tx - 3, ty - 3, tw + 6, th + 6, 10, COLOR_ACCENT);
	}

	draw_thumb(item, tx, ty, tw, th);

	/* The words stay on the slot's grid line even when the picture above them has
	 * grown, or the whole row would appear to jump as the cursor moves along it. */
	int text_y = y + THUMB_H + 18;
	ui_draw_text(x, text_y, FONT_ROW, focused ? COLOR_TEXT : COLOR_DIM,
			item->title, CARD_W);

	char meta[224];
	char views[24];
	if (item->views > 0) {
		format_views(item->views, views, sizeof(views));
		snprintf(meta, sizeof(meta), "%s  ·  %s", views, item->author ? item->author : "YouTube");
	} else {
		snprintf(meta, sizeof(meta), "%s", item->author ? item->author : "YouTube");
	}

	ui_draw_text(x, text_y + 26, FONT_SMALL, COLOR_DIM, meta, CARD_W);
}

/*
 * The strip of source names above the grid. It fits in the gap the header rule
 * already leaves, so adding it costs the cards no height.
 */
void ui_result_tabs(const char *const *labels, size_t count, size_t active) {
	int x = CONTENT_X;

	for (size_t i = 0; i < count; i++) {
		int text_w = ui_measure_text(FONT_SMALL, labels[i]);
		int pill_w = text_w + 26;

		if (i == active) {
			ui_fill_round_rect(x, TAB_Y, pill_w, TAB_H, TAB_H / 2, COLOR_ACCENT);
		}

		ui_draw_text(x + 13, TAB_Y + TAB_H / 2, FONT_SMALL,
				i == active ? COLOR_ON_ACCENT : COLOR_DIM, labels[i], pill_w);

		x += pill_w + 8;
	}
}

static void card_origin(size_t cell, int *x, int *y) {
	*x = CONTENT_X + (int)(cell % COLS) * (CARD_W + CARD_GAP);
	*y = LIST_TOP + (int)(cell / COLS) * CARD_PITCH_Y;
}

void ui_results(const MediaListing *listing, size_t selected, size_t scroll) {
	for (size_t cell = 0; cell < UI_RESULT_ROWS * COLS; cell++) {
		size_t index = scroll + cell;
		if (index >= listing->count) break;

		int x, y;
		card_origin(cell, &x, &y);

		/* The focused card is drawn last so its grown picture and ring sit over its
		 * neighbours rather than under them. */
		if (index != selected) draw_card(&listing->items[index], x, y, false);
	}

	if (selected >= scroll && selected < scroll + UI_RESULT_ROWS * COLS &&
			selected < listing->count) {
		int x, y;
		card_origin(selected - scroll, &x, &y);
		draw_card(&listing->items[selected], x, y, true);
	}
}

int ui_hit_result(int x, int y) {
	for (size_t cell = 0; cell < UI_RESULT_ROWS * COLS; cell++) {
		int cx, cy;
		card_origin(cell, &cx, &cy);

		if (x >= cx && x < cx + CARD_W && y >= cy && y < cy + CARD_H) return (int)cell;
	}
	return -1;
}
