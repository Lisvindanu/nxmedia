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
#define THUMB_W 128
#define THUMB_H 72
/* 320x180 and about twelve kilobytes. The 720p variant is twenty times the bytes
 * for a picture drawn at a seventh of the size. */
#define THUMB_URL "https://i.ytimg.com/vi/%s/mqdefault.jpg"

#define ROW_H 84
#define ROW_GAP 6
#define ROW_PITCH (ROW_H + ROW_GAP)

static const SDL_Color COLOR_ROW_ON = { 0x2C, 0x34, 0x40, 0xFF };
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
			(enum AVPixelFormat)frame->format, THUMB_W, THUMB_H, AV_PIX_FMT_RGBA,
			SWS_BILINEAR, NULL, NULL, NULL);
	if (!sws) goto done;

	rgba = malloc((size_t)THUMB_W * THUMB_H * 4);
	if (rgba) {
		uint8_t *dst[4] = { rgba, NULL, NULL, NULL };
		int stride[4] = { THUMB_W * 4, 0, 0, 0 };
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
				SDL_TEXTUREACCESS_STATIC, THUMB_W, THUMB_H);
		if (slot->texture) {
			SDL_UpdateTexture(slot->texture, NULL, slot->pixels, THUMB_W * 4);
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

static void draw_thumb(const MediaItem *item, int x, int y) {
	SDL_Texture *texture = thumb_for(item->id);

	if (!texture) {
		ui_fill_round_rect(x, y, THUMB_W, THUMB_H, 6, COLOR_PANEL);
		/* A play triangle rather than a spinner: it says "video" while it waits and
		 * needs no frame clock to look right. */
		int cx = x + THUMB_W / 2 - 4;
		int cy = y + THUMB_H / 2;
		for (int i = 0; i < 10; i++) ui_fill_rect(cx + i, cy - 9 + i, 1, 18 - 2 * i, COLOR_RULE);
		return;
	}

	SDL_Rect dst = { x, y, THUMB_W, THUMB_H };
	SDL_RenderCopy(ui_renderer, texture, NULL, &dst);

	char time[16];
	format_duration(item->duration, time, sizeof(time));

	int badge_w = ui_measure_text(FONT_SMALL, time) + 12;
	int badge_x = x + THUMB_W - badge_w - 5;
	int badge_y = y + THUMB_H - 21;

	ui_fill_round_rect(badge_x, badge_y, badge_w, 16, 3, COLOR_SHADE);
	ui_draw_text(badge_x + 6, badge_y + 8, FONT_SMALL, COLOR_TEXT, time, badge_w);
}

static void draw_row(const MediaItem *item, int y, bool selected) {
	ui_fill_round_rect(CONTENT_X, y, CONTENT_W, ROW_H, 10,
			selected ? COLOR_ROW_ON : COLOR_ROW);

	/* A bar down the selected edge, so which row is live survives being read from
	 * across a room where a slightly lighter panel would not. */
	if (selected) ui_fill_round_rect(CONTENT_X, y + 10, 4, ROW_H - 20, 2, COLOR_ACCENT);

	draw_thumb(item, CONTENT_X + 12, y + (ROW_H - THUMB_H) / 2);

	int text_x = CONTENT_X + 12 + THUMB_W + 18;
	int text_w = CONTENT_RIGHT - text_x - 16;

	ui_draw_text(text_x, y + 32, FONT_ROW, COLOR_TEXT, item->title, text_w);
	ui_draw_text(text_x, y + 58, FONT_SMALL, COLOR_DIM,
			item->author ? item->author : "YouTube", text_w);
}

void ui_results(const MediaListing *listing, size_t selected, size_t scroll) {
	for (size_t row = 0; row < UI_RESULT_ROWS; row++) {
		size_t index = scroll + row;
		if (index >= listing->count) break;

		draw_row(&listing->items[index], LIST_TOP + (int)row * ROW_PITCH, index == selected);
	}
}

int ui_hit_result(int x, int y) {
	if (x < CONTENT_X || x > CONTENT_RIGHT) return -1;

	int offset = y - LIST_TOP;
	if (offset < 0) return -1;

	/* A tap in the gap between two rows belongs to neither. */
	if (offset % ROW_PITCH > ROW_H) return -1;

	int row = offset / ROW_PITCH;
	return row < (int)UI_RESULT_ROWS ? row : -1;
}
