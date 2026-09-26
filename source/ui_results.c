#include "ui_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <switch.h>

#include "http.h"

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
#define CARD_PITCH_Y (CARD_H + CARD_ROW_GAP)

/* How far the focused card grows past its slot on every side. The pictures either
 * side stay put, so the grown one reads as lifted rather than as a shifted grid. */
#define FOCUS_GROW 8

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

static void draw_thumb(const MediaItem *item, int x, int y, int w, int h) {
	SDL_Texture *texture = thumb_for(item->id);

	if (!texture) {
		ui_fill_round_rect(x, y, w, h, 8, COLOR_PANEL);
		/* A play triangle rather than a spinner: it says "video" while it waits and
		 * needs no frame clock to look right. */
		int cx = x + w / 2 - 6;
		int cy = y + h / 2;
		for (int i = 0; i < 14; i++) ui_fill_rect(cx + i, cy - 13 + i, 1, 26 - 2 * i, COLOR_RULE);
		return;
	}

	SDL_Rect dst = { x, y, w, h };
	SDL_RenderCopy(ui_renderer, texture, NULL, &dst);

	char time[16];
	format_duration(item->duration, time, sizeof(time));

	int badge_w = ui_measure_text(FONT_SMALL, time) + 14;
	int badge_x = x + w - badge_w - 8;
	int badge_y = y + h - 26;

	ui_fill_round_rect(badge_x, badge_y, badge_w, 20, 4, COLOR_SHADE);
	ui_draw_text(badge_x + 7, badge_y + 10, FONT_SMALL, COLOR_TEXT, time, badge_w);
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
