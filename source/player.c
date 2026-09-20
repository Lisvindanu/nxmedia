#include "player.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/hwcontext.h>
#include <libavutil/mathematics.h>
#include <libavutil/pixfmt.h>
#include <libswresample/swresample.h>

#include "audio_out.h"
#include "video_out.h"

#define USER_AGENT "nxmedia/" APP_VERSION

/* ffmpeg counts protocol timeouts in microseconds. Long enough to ride out a slow
 * handshake on hotel wifi, short enough that a dead host does not hang the pane. */
#define RW_TIMEOUT_US "15000000"

/* Homebrew gets three of the four cores; the fourth is the system's. Only the
 * software decoders use these -- NVDEC ignores the setting entirely. */
#define DECODE_THREADS 3

/* Nothing to seek within: HLS and shoutcast report no duration, and jumping around
 * a live stream is meaningless anyway. */
#define DURATION_UNKNOWN (-1.0)

/** What the worker was asked to play. It outlives the call, so it owns its strings. */
typedef struct {
	char *url;
	char *referer;
	char *user_agent;
} Request;

typedef struct {
	AVFormatContext *fmt;
	AVCodecContext *audio;
	AVCodecContext *video;
	AVBufferRef *hw_device;
	int audio_index;
	int video_index;
} Media;

static struct {
	bool ready;
	SDL_Thread *worker;
	SDL_atomic_t stop;
	SDL_atomic_t state;
	SDL_atomic_t has_video;
	SDL_atomic_t paused;
	SDL_atomic_t seekable;
	/* Written by the worker before it starts playing and only read afterwards, so
	 * the plain double is safe enough for something the header just displays. */
	double duration;
	/* Set by the render loop, consumed and cleared by the worker. */
	SDL_atomic_t seek_pending;
	double seek_target;
	SDL_mutex *seek_lock;
	/* Fallback clock for sources with no audio track at all. */
	uint32_t silent_start_ms;
	/* Worker only. */
	SwrContext *swr;
	AVChannelLayout in_layout;
	int in_rate;
	int in_fmt;
	uint8_t *out;
	int out_samples;
	char error[256];
} pl;

static bool stopping(void) {
	return SDL_AtomicGet(&pl.stop) != 0;
}

static void set_state(PlayerState state) {
	SDL_AtomicSet(&pl.state, (int)state);
}

static void fail(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	vsnprintf(pl.error, sizeof(pl.error), fmt, args);
	va_end(args);
	printf("[player] gagal: %s\n", pl.error);
	set_state(PlayerState_Error);
}

/* Only ever called from the worker, which is the one thread allowed to format an
 * ffmpeg error, so the shared buffer needs no lock. */
static const char *av_err(int rc) {
	static char text[AV_ERROR_MAX_STRING_SIZE];
	av_strerror(rc, text, sizeof(text));
	return text;
}

/* --- audio decoding ---------------------------------------------------- */

/* A stream can change shape mid-flight -- an HLS variant switch is the ordinary
 * case -- so the converter is rebuilt whenever frames stop matching the one that is
 * currently set up. */
static bool ensure_converter(const AVFrame *frame) {
	if (pl.swr && frame->sample_rate == pl.in_rate && frame->format == pl.in_fmt &&
			av_channel_layout_compare(&frame->ch_layout, &pl.in_layout) == 0) {
		return true;
	}

	swr_free(&pl.swr);

	AVChannelLayout out_layout = AV_CHANNEL_LAYOUT_STEREO;
	int rc = swr_alloc_set_opts2(&pl.swr, &out_layout, AV_SAMPLE_FMT_S16, AUDIO_OUT_RATE,
			&frame->ch_layout, frame->format, frame->sample_rate, 0, NULL);
	if (rc >= 0) rc = swr_init(pl.swr);
	if (rc < 0) {
		fail("swr_init: %.180s", av_err(rc));
		return false;
	}

	av_channel_layout_copy(&pl.in_layout, &frame->ch_layout);
	pl.in_rate = frame->sample_rate;
	pl.in_fmt = frame->format;

	printf("[player] audio %d Hz, %d kanal, %s\n", frame->sample_rate,
			frame->ch_layout.nb_channels, av_get_sample_fmt_name(frame->format));
	return true;
}

static bool push_audio(const AVFrame *frame, double pts) {
	if (!ensure_converter(frame)) return false;

	/* Resampling leaves samples queued inside swr, so the room this call needs is
	 * that backlog plus the frame being handed over. */
	int room = (int)av_rescale_rnd(swr_get_delay(pl.swr, pl.in_rate) + frame->nb_samples,
			AUDIO_OUT_RATE, pl.in_rate, AV_ROUND_UP);

	if (room > pl.out_samples) {
		uint8_t *grown = realloc(pl.out, (size_t)room * AUDIO_OUT_CHANNELS * sizeof(int16_t));
		if (!grown) {
			fail("kehabisan memori");
			return false;
		}
		pl.out = grown;
		pl.out_samples = room;
	}

	int got = swr_convert(pl.swr, &pl.out, room,
			(const uint8_t **)frame->extended_data, frame->nb_samples);
	if (got < 0) {
		fail("swr_convert: %.180s", av_err(got));
		return false;
	}
	if (got == 0) return true;

	return audio_out_push(pl.out, (size_t)got * AUDIO_OUT_CHANNELS * sizeof(int16_t), pts);
}

/* --- opening ----------------------------------------------------------- */

/* ffmpeg polls this from inside every blocking network call, which is what lets a
 * stop request land while a connect is still hanging. */
static int on_interrupt(void *user) {
	(void)user;
	return SDL_AtomicGet(&pl.stop);
}

/* Offered the decoder's list of output formats, this picks the hardware surface when
 * it is there. Falling through to the software path means a Cortex-A57 doing the
 * decode, which is fine for a 480p clip and hopeless for anything larger, but still
 * better than refusing to play the file. */
static enum AVPixelFormat pick_pixel_format(AVCodecContext *ctx,
		const enum AVPixelFormat *formats) {
	(void)ctx;

	for (const enum AVPixelFormat *format = formats; *format != AV_PIX_FMT_NONE; format++) {
		if (*format == AV_PIX_FMT_NVTEGRA) return *format;
	}

	printf("[player] tidak ada jalur hardware, decode lewat CPU\n");
	return formats[0];
}

static AVCodecContext *open_decoder(Media *media, enum AVMediaType type, int *out_index) {
	const AVCodec *codec = NULL;
	int index = av_find_best_stream(media->fmt, type, -1, -1, &codec, 0);
	if (index < 0 || !codec) return NULL;

	AVCodecContext *dec = avcodec_alloc_context3(codec);
	if (!dec) return NULL;

	if (avcodec_parameters_to_context(dec, media->fmt->streams[index]->codecpar) < 0) {
		avcodec_free_context(&dec);
		return NULL;
	}

	if (type == AVMEDIA_TYPE_VIDEO) {
		dec->get_format = pick_pixel_format;
		dec->thread_count = DECODE_THREADS;
		if (media->hw_device) dec->hw_device_ctx = av_buffer_ref(media->hw_device);
	}

	int rc = avcodec_open2(dec, codec, NULL);
	if (rc < 0) {
		printf("[player] decoder %s gagal: %s\n", codec->name, av_err(rc));
		avcodec_free_context(&dec);
		return NULL;
	}

	printf("[player] %s: %s\n", av_get_media_type_string(type), codec->name);
	*out_index = index;
	return dec;
}

static void close_media(Media *media) {
	avcodec_free_context(&media->audio);
	avcodec_free_context(&media->video);
	av_buffer_unref(&media->hw_device);
	if (media->fmt) avformat_close_input(&media->fmt);
}

static bool open_media(const Request *request, Media *media) {
	memset(media, 0, sizeof(*media));
	media->audio_index = -1;
	media->video_index = -1;

	media->fmt = avformat_alloc_context();
	if (!media->fmt) {
		fail("kehabisan memori");
		return false;
	}
	media->fmt->interrupt_callback.callback = on_interrupt;

	AVDictionary *opts = NULL;
	av_dict_set(&opts, "user_agent", USER_AGENT, 0);
	av_dict_set(&opts, "rw_timeout", RW_TIMEOUT_US, 0);
	/* Shoutcast servers drop clients routinely; reconnecting in place keeps a station
	 * alive without the user pressing anything. */
	av_dict_set(&opts, "reconnect", "1", 0);
	av_dict_set(&opts, "reconnect_streamed", "1", 0);
	if (request->referer) av_dict_set(&opts, "referer", request->referer, 0);
	if (request->user_agent) av_dict_set(&opts, "user_agent", request->user_agent, 0);

	/* Nulls the context itself on failure, so there is nothing left to free here. */
	int rc = avformat_open_input(&media->fmt, request->url, NULL, &opts);
	av_dict_free(&opts);
	if (rc < 0) {
		if (!stopping()) fail("tidak bisa membuka sumber: %.180s", av_err(rc));
		return false;
	}

	rc = avformat_find_stream_info(media->fmt, NULL);
	if (rc < 0) {
		if (!stopping()) fail("tidak mengenali isinya: %.180s", av_err(rc));
		close_media(media);
		return false;
	}

	/* Failing to reach NVDEC is not fatal: the software decoders are still there, and
	 * an audio-only source never wanted it in the first place. */
	if (av_hwdevice_ctx_create(&media->hw_device, AV_HWDEVICE_TYPE_NVTEGRA, NULL, NULL, 0) < 0) {
		printf("[player] NVDEC tidak tersedia\n");
		media->hw_device = NULL;
	}

	media->audio = open_decoder(media, AVMEDIA_TYPE_AUDIO, &media->audio_index);
	media->video = open_decoder(media, AVMEDIA_TYPE_VIDEO, &media->video_index);

	if (!media->audio && !media->video) {
		fail("tidak ada jalur audio atau video yang didukung");
		close_media(media);
		return false;
	}

	pl.duration = media->fmt->duration > 0
			? (double)media->fmt->duration / AV_TIME_BASE
			: DURATION_UNKNOWN;
	SDL_AtomicSet(&pl.has_video, media->video != NULL);
	SDL_AtomicSet(&pl.seekable, pl.duration > 0);
	return true;
}

/* --- playing ----------------------------------------------------------- */

/** Stream time of a packet or frame in seconds, or 0 when the source left it unset. */
static double seconds_of(int64_t stamp, AVRational base) {
	if (stamp == AV_NOPTS_VALUE) return 0.0;
	return (double)stamp * base.num / base.den;
}

/* Starts the device once there is enough sound to survive a hiccup. A source with no
 * audio at all is timed off the wall clock instead, so it is started right away.
 *
 * Sound has to be there before the clock is, and not merely for smoothness: the
 * moment the first sample lands the master clock stops being the wall and becomes the
 * audio ring, so starting the picture before then makes the clock jump backwards to
 * the beginning of the sound and the queue stalls until it has caught up again. */
static void start_when_primed(const Media *media) {
	if (player_state() == PlayerState_Playing) return;
	if (media->audio && !audio_out_primed()) return;

	pl.silent_start_ms = SDL_GetTicks();
	audio_out_start();
	set_state(PlayerState_Playing);
	printf("[player] mulai\n");
}

/* Both drains check before pushing rather than after, because the push is the call
 * that blocks and starting is what unblocks it. */

static bool drain_audio(Media *media, AVFrame *frame) {
	AVRational base = media->fmt->streams[media->audio_index]->time_base;

	while (avcodec_receive_frame(media->audio, frame) == 0) {
		start_when_primed(media);
		bool pushed = push_audio(frame, seconds_of(frame->best_effort_timestamp, base));
		av_frame_unref(frame);
		if (!pushed) return false;
	}
	return true;
}

static bool drain_video(Media *media, AVFrame *frame) {
	AVRational base = media->fmt->streams[media->video_index]->time_base;

	while (avcodec_receive_frame(media->video, frame) == 0) {
		start_when_primed(media);
		bool pushed = video_out_push(frame, seconds_of(frame->best_effort_timestamp, base),
				player_state() != PlayerState_Playing);
		av_frame_unref(frame);
		if (!pushed && !stopping()) {
			fail("tidak bisa menyiapkan gambar");
			return false;
		}
		if (!pushed) return false;
	}
	return true;
}

static void apply_seek(Media *media) {
	SDL_LockMutex(pl.seek_lock);
	double target = pl.seek_target;
	SDL_AtomicSet(&pl.seek_pending, 0);
	SDL_UnlockMutex(pl.seek_lock);

	if (target < 0) target = 0;

	int64_t stamp = (int64_t)(target * AV_TIME_BASE);
	/* Backwards, so the seek lands on the keyframe at or before the target rather
	 * than skipping past what was asked for. */
	if (av_seek_frame(media->fmt, -1, stamp, AVSEEK_FLAG_BACKWARD) < 0) return;

	if (media->audio) avcodec_flush_buffers(media->audio);
	if (media->video) avcodec_flush_buffers(media->video);
	audio_out_flush();
	video_out_flush();
}

static void free_request(Request *request) {
	if (!request) return;

	free(request->url);
	free(request->referer);
	free(request->user_agent);
	free(request);
}

static int worker_main(void *user) {
	Request *request = user;
	printf("[player] membuka: %s\n", request->url);

	Media media;
	bool opened = open_media(request, &media);
	free_request(request);
	if (!opened) return 0;

	AVPacket *packet = av_packet_alloc();
	AVFrame *frame = av_frame_alloc();
	if (!packet || !frame) {
		fail("kehabisan memori");
	} else {
		while (!stopping()) {
			if (SDL_AtomicGet(&pl.seek_pending)) apply_seek(&media);

			int rc = av_read_frame(media.fmt, packet);
			if (rc < 0) {
				/* The interrupt callback surfaces as a read error, so a pending stop is
				 * checked first: it is a request, not a fault. */
				if (stopping()) break;
				/* Reaching the end of a file is how it is supposed to go, so it is not
				 * dressed up as a failure; a stream that ends has still just ended. */
				if (rc == AVERROR_EOF) set_state(PlayerState_Ended);
				else fail("sumber terputus: %.180s", av_err(rc));
				break;
			}

			AVCodecContext *dec = NULL;
			if (packet->stream_index == media.audio_index) dec = media.audio;
			else if (packet->stream_index == media.video_index) dec = media.video;

			if (!dec) {
				av_packet_unref(packet);
				continue;
			}

			rc = avcodec_send_packet(dec, packet);
			bool is_audio = dec == media.audio;
			av_packet_unref(packet);

			/* A corrupt packet in a live stream is normal; only a decoder that has given
			 * up for good is worth stopping over. */
			if (rc < 0 && rc != AVERROR(EAGAIN) && rc != AVERROR_INVALIDDATA) {
				fail("decoder berhenti: %.180s", av_err(rc));
				break;
			}

			if (!(is_audio ? drain_audio(&media, frame) : drain_video(&media, frame))) break;
		}
	}

	av_frame_free(&frame);
	av_packet_free(&packet);
	close_media(&media);
	return 0;
}

/* --- public ------------------------------------------------------------ */

bool player_init(char *err, size_t err_len) {
	if (pl.ready) return true;

	memset(&pl, 0, sizeof(pl));
	pl.duration = DURATION_UNKNOWN;

	pl.seek_lock = SDL_CreateMutex();
	if (!pl.seek_lock) {
		snprintf(err, err_len, "SDL_CreateMutex: %.180s", SDL_GetError());
		return false;
	}

	if (!video_out_init()) {
		snprintf(err, err_len, "SDL_CreateMutex/Cond: %.180s", SDL_GetError());
		SDL_DestroyMutex(pl.seek_lock);
		return false;
	}

	if (!audio_out_init(err, err_len)) {
		video_out_exit();
		SDL_DestroyMutex(pl.seek_lock);
		return false;
	}

	avformat_network_init();
	pl.ready = true;
	return true;
}

void player_exit(void) {
	if (!pl.ready) return;

	player_stop();
	audio_out_exit();
	video_out_exit();
	SDL_DestroyMutex(pl.seek_lock);
	avformat_network_deinit();
	pl.ready = false;
}

void player_stop(void) {
	if (!pl.ready) return;

	SDL_AtomicSet(&pl.stop, 1);
	audio_out_interrupt(true);
	video_out_interrupt(true);

	if (pl.worker) {
		SDL_WaitThread(pl.worker, NULL);
		pl.worker = NULL;
	}

	audio_out_stop();
	video_out_clear();
	audio_out_interrupt(false);
	video_out_interrupt(false);

	swr_free(&pl.swr);
	av_channel_layout_uninit(&pl.in_layout);
	free(pl.out);
	pl.out = NULL;
	pl.out_samples = 0;
	pl.in_rate = 0;
	pl.in_fmt = AV_SAMPLE_FMT_NONE;
	pl.duration = DURATION_UNKNOWN;

	SDL_AtomicSet(&pl.stop, 0);
	SDL_AtomicSet(&pl.has_video, 0);
	SDL_AtomicSet(&pl.paused, 0);
	SDL_AtomicSet(&pl.seekable, 0);
	SDL_AtomicSet(&pl.seek_pending, 0);
	set_state(PlayerState_Idle);
}

/** strdup that reports success for a NULL source, which simply copies to nothing. */
static bool dup_opt(const char *from, char **to) {
	if (!from) return true;

	*to = strdup(from);
	return *to != NULL;
}

bool player_play(const char *url, const PlayerHeaders *headers, char *err, size_t err_len) {
	if (!pl.ready) {
		snprintf(err, err_len, "pemutar belum siap");
		return false;
	}

	player_stop();
	pl.error[0] = '\0';

	/* The worker outlives this call, so it gets its own copy to own and free. */
	Request *request = calloc(1, sizeof(Request));
	bool copied = request && dup_opt(url, &request->url);
	if (copied && headers) {
		copied = dup_opt(headers->referer, &request->referer) &&
				dup_opt(headers->user_agent, &request->user_agent);
	}

	if (!copied) {
		snprintf(err, err_len, "kehabisan memori");
		free_request(request);
		return false;
	}

	set_state(PlayerState_Connecting);
	pl.worker = SDL_CreateThread(worker_main, "nxmedia-player", request);
	if (!pl.worker) {
		snprintf(err, err_len, "SDL_CreateThread: %.180s", SDL_GetError());
		free_request(request);
		set_state(PlayerState_Idle);
		return false;
	}

	return true;
}

PlayerState player_state(void) {
	return (PlayerState)SDL_AtomicGet(&pl.state);
}

const char *player_error(void) {
	return pl.error;
}

void player_set_volume(int percent) {
	audio_out_set_volume(percent);
}

int player_volume(void) {
	return audio_out_volume();
}

bool player_has_video(void) {
	return SDL_AtomicGet(&pl.has_video) != 0;
}

/* Where playback is, in seconds. Sound is the master clock because drift there is
 * the one kind a person can hear; a source with no sound is paced off the wall. */
static double current_clock(void) {
	double clock = audio_out_clock();
	if (clock >= 0) return clock;
	if (player_state() != PlayerState_Playing) return 0.0;
	return (SDL_GetTicks() - pl.silent_start_ms) / 1000.0;
}

bool player_draw_video(UiRect box) {
	if (!player_has_video()) return false;

	return video_out_draw(current_clock(), box);
}

double player_position(void) {
	return current_clock();
}

double player_duration(void) {
	return pl.duration;
}

void player_set_paused(bool paused) {
	SDL_AtomicSet(&pl.paused, paused ? 1 : 0);
	audio_out_pause(paused);
}

bool player_is_paused(void) {
	return SDL_AtomicGet(&pl.paused) != 0;
}

bool player_can_seek(void) {
	return SDL_AtomicGet(&pl.seekable) != 0;
}

void player_seek(double seconds) {
	if (!player_can_seek()) return;

	SDL_LockMutex(pl.seek_lock);
	pl.seek_target = seconds;
	SDL_AtomicSet(&pl.seek_pending, 1);
	SDL_UnlockMutex(pl.seek_lock);
}
