#include "video_out.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>

#include "ui_internal.h"

static struct {
	SDL_mutex *lock;
	/* Raised by the render loop once it has taken a frame, which is the only thing
	 * that ever makes room for the decoder. */
	SDL_cond *drained;
	AVFrame *queue[VIDEO_QUEUE_MAX];
	double pts[VIDEO_QUEUE_MAX];
	size_t head;
	size_t tail;
	size_t fill;
	SDL_atomic_t interrupt;

	/* Render loop only. */
	SDL_Texture *texture;
	int texture_w;
	int texture_h;

	/* Decoder thread only. Only ever built for the odd source the hardware hands
	 * back as something other than YUV420P. */
	struct SwsContext *sws;
	int sws_w;
	int sws_h;
	int sws_fmt;
} vo;

bool video_out_init(void) {
	memset(&vo, 0, sizeof(vo));
	vo.lock = SDL_CreateMutex();
	vo.drained = SDL_CreateCond();
	return vo.lock && vo.drained;
}

static void drop_queued(void) {
	while (vo.fill > 0) {
		av_frame_free(&vo.queue[vo.tail]);
		vo.tail = (vo.tail + 1) % VIDEO_QUEUE_MAX;
		vo.fill--;
	}
	vo.head = vo.tail = 0;
}

void video_out_exit(void) {
	SDL_LockMutex(vo.lock);
	drop_queued();
	SDL_UnlockMutex(vo.lock);

	sws_freeContext(vo.sws);
	vo.sws = NULL;

	if (vo.texture) {
		SDL_DestroyTexture(vo.texture);
		vo.texture = NULL;
	}

	SDL_DestroyCond(vo.drained);
	SDL_DestroyMutex(vo.lock);
	vo.lock = NULL;
	vo.drained = NULL;
}

void video_out_flush(void) {
	SDL_LockMutex(vo.lock);
	drop_queued();
	SDL_CondBroadcast(vo.drained);
	SDL_UnlockMutex(vo.lock);
}

void video_out_clear(void) {
	video_out_flush();

	if (vo.texture) {
		SDL_DestroyTexture(vo.texture);
		vo.texture = NULL;
	}
}

void video_out_interrupt(bool on) {
	SDL_AtomicSet(&vo.interrupt, on ? 1 : 0);
	if (!on) return;

	SDL_LockMutex(vo.lock);
	SDL_CondBroadcast(vo.drained);
	SDL_UnlockMutex(vo.lock);
}

/* --- conversion -------------------------------------------------------- */

/* The VIC wants every plane's address and pitch on a 256-byte boundary. Miss it and
 * ffmpeg silently unswizzles the surface on the CPU instead, which at 1080p is most
 * of a core -- av_frame_get_buffer only ever aligns to 64, hence the hand rolling. */
#define VIC_ALIGN 256

static void free_block(void *block, uint8_t *data) {
	(void)data;
	av_free(block);
}

/** An empty frame the VIC can write straight into, planes packed into one block. */
static AVFrame *alloc_aligned(enum AVPixelFormat format, int w, int h) {
	AVFrame *frame = av_frame_alloc();
	if (!frame) return NULL;

	frame->format = format;
	frame->width = w;
	frame->height = h;

	ptrdiff_t strides[4] = {0};
	size_t sizes[4] = {0};
	if (av_image_fill_linesizes(frame->linesize, format, w) < 0) goto fail;
	for (int i = 0; i < 4; i++) {
		frame->linesize[i] = FFALIGN(frame->linesize[i], VIC_ALIGN);
		strides[i] = frame->linesize[i];
	}
	if (av_image_fill_plane_sizes(sizes, format, h, strides) < 0) goto fail;

	size_t total = 0;
	for (int i = 0; i < 4; i++) total += sizes[i];

	uint8_t *block = av_malloc(total + VIC_ALIGN);
	if (!block) goto fail;

	/* Every plane is a whole number of aligned strides long, so packing them end to
	 * end from an aligned base leaves each one aligned too. */
	uint8_t *base = (uint8_t *)FFALIGN((uintptr_t)block, VIC_ALIGN);
	frame->buf[0] = av_buffer_create(base, total, free_block, block, 0);
	if (!frame->buf[0]) {
		av_free(block);
		goto fail;
	}

	size_t offset = 0;
	for (int i = 0; i < 4 && sizes[i]; i++) {
		frame->data[i] = base + offset;
		offset += sizes[i];
	}

	return frame;

fail:
	av_frame_free(&frame);
	return NULL;
}

/* Everything the screen cannot take as it stands: NV12 off the hardware, and the
 * formats only the software decoders produce -- 10-bit HEVC, full-range mjpeg. */
static AVFrame *convert(const AVFrame *src) {
	if (vo.sws && (src->width != vo.sws_w || src->height != vo.sws_h ||
			src->format != vo.sws_fmt)) {
		sws_freeContext(vo.sws);
		vo.sws = NULL;
	}

	if (!vo.sws) {
		vo.sws = sws_getContext(src->width, src->height, src->format,
				src->width, src->height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);
		if (!vo.sws) return NULL;
		vo.sws_w = src->width;
		vo.sws_h = src->height;
		vo.sws_fmt = src->format;
	}

	AVFrame *dst = av_frame_alloc();
	if (!dst) return NULL;

	dst->format = AV_PIX_FMT_YUV420P;
	dst->width = src->width;
	dst->height = src->height;
	if (av_frame_get_buffer(dst, 0) < 0) {
		av_frame_free(&dst);
		return NULL;
	}

	sws_scale(vo.sws, (const uint8_t *const *)src->data, src->linesize, 0, src->height,
			dst->data, dst->linesize);
	return dst;
}

/*
 * NVDEC keeps its pictures on a hardware surface nothing on the CPU side can read.
 * Pulling one down runs through the VIC block, which is told to read the surface in
 * whatever format the destination frame is in -- so the destination has to be the
 * pool's own sw_format, NV12, and not the YUV420P we actually want. Asking for
 * YUV420P makes the VIC read interleaved chroma as two planes and hand back a
 * striped, miscoloured picture rather than an error.
 */
static AVFrame *download(const AVFrame *src) {
	const AVHWFramesContext *hw = (const AVHWFramesContext *)src->hw_frames_ctx->data;

	/* The surface is rounded up to even dimensions, so the transfer has to be asked
	 * for that size and the picture cropped back afterwards. */
	AVFrame *raw = alloc_aligned(hw->sw_format, hw->width, hw->height);
	if (!raw) return NULL;

	int rc = av_hwframe_transfer_data(raw, src, 0);
	if (rc < 0) {
		printf("[video] transfer dari NVDEC gagal: %d\n", rc);
		av_frame_free(&raw);
		return NULL;
	}

	raw->width = src->width;
	raw->height = src->height;

	AVFrame *dst = convert(raw);
	av_frame_free(&raw);
	return dst;
}

bool video_out_push(const AVFrame *source, double pts, bool may_drop) {
	const AVFrame *src = source;

	AVFrame *frame;
	if (src->format == AV_PIX_FMT_YUV420P) {
		/* Reference counted, so this shares the decoder's buffer instead of copying a
		 * couple of megabytes per frame. */
		frame = av_frame_alloc();
		if (frame && av_frame_ref(frame, src) < 0) av_frame_free(&frame);
	} else if (src->hw_frames_ctx) {
		frame = download(src);
	} else {
		frame = convert(src);
	}
	if (!frame) {
		if (!SDL_AtomicGet(&vo.interrupt))
			printf("[video] gagal menyiapkan %s\n", av_get_pix_fmt_name(src->format));
		return false;
	}

	SDL_LockMutex(vo.lock);

	/* Waiting here before the clock runs would be waiting on the render loop, which
	 * takes nothing until playback starts, which needs audio the decoder can only
	 * reach by carrying on past this frame. The newest picture is the one to give up:
	 * dropping it keeps the opening of the video, which is what gets shown first. */
	if (may_drop && vo.fill == VIDEO_QUEUE_MAX) {
		SDL_UnlockMutex(vo.lock);
		av_frame_free(&frame);
		return true;
	}

	while (vo.fill == VIDEO_QUEUE_MAX) {
		if (SDL_AtomicGet(&vo.interrupt)) {
			SDL_UnlockMutex(vo.lock);
			av_frame_free(&frame);
			return false;
		}
		SDL_CondWaitTimeout(vo.drained, vo.lock, 100);
	}

	vo.queue[vo.head] = frame;
	vo.pts[vo.head] = pts;
	vo.head = (vo.head + 1) % VIDEO_QUEUE_MAX;
	vo.fill++;

	SDL_UnlockMutex(vo.lock);
	return true;
}

/* --- presentation ------------------------------------------------------ */

/**
 * Newest frame whose time has come, with anything older thrown away: when the
 * decoder falls behind, skipping to the current picture is what keeps it in step
 * with the sound rather than sliding further behind.
 */
static AVFrame *take_due_frame(double clock) {
	AVFrame *due = NULL;

	SDL_LockMutex(vo.lock);
	while (vo.fill > 0 && vo.pts[vo.tail] <= clock) {
		av_frame_free(&due);
		due = vo.queue[vo.tail];
		vo.queue[vo.tail] = NULL;
		vo.tail = (vo.tail + 1) % VIDEO_QUEUE_MAX;
		vo.fill--;
	}
	SDL_CondSignal(vo.drained);
	SDL_UnlockMutex(vo.lock);

	return due;
}

static bool ensure_texture(int w, int h) {
	if (vo.texture && vo.texture_w == w && vo.texture_h == h) return true;

	if (vo.texture) SDL_DestroyTexture(vo.texture);
	vo.texture = SDL_CreateTexture(ui_renderer, SDL_PIXELFORMAT_IYUV,
			SDL_TEXTUREACCESS_STREAMING, w, h);
	if (!vo.texture) {
		printf("[video] SDL_CreateTexture %dx%d: %s\n", w, h, SDL_GetError());
		return false;
	}

	vo.texture_w = w;
	vo.texture_h = h;
	return true;
}

/** Largest rect inside `box` with the picture's aspect ratio, centred. */
static SDL_Rect fit_inside(UiRect box, int w, int h) {
	int width = box.w;
	int height = (int)((int64_t)box.w * h / w);

	if (height > box.h) {
		height = box.h;
		width = (int)((int64_t)box.h * w / h);
	}

	return (SDL_Rect){ box.x + (box.w - width) / 2, box.y + (box.h - height) / 2,
			width, height };
}

bool video_out_draw(double clock, UiRect box) {
	AVFrame *frame = take_due_frame(clock);

	if (frame) {
		if (ensure_texture(frame->width, frame->height)) {
			SDL_UpdateYUVTexture(vo.texture, NULL,
					frame->data[0], frame->linesize[0],
					frame->data[1], frame->linesize[1],
					frame->data[2], frame->linesize[2]);
		}
		av_frame_free(&frame);
	}

	if (!vo.texture) return false;

	SDL_Rect dst = fit_inside(box, vo.texture_w, vo.texture_h);
	SDL_RenderCopy(ui_renderer, vo.texture, NULL, &dst);
	return true;
}
