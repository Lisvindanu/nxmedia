#include "audio_out.h"

#include <stdio.h>
#include <string.h>

#include <SDL2/SDL.h>

#define DEV_SAMPLES 2048
#define BYTES_PER_SEC (AUDIO_OUT_RATE * AUDIO_OUT_CHANNELS * (int)sizeof(int16_t))

/* Roughly 2.5 seconds of slack. Large enough to ride out a hiccup on hotel wifi,
 * small enough that stopping does not leave an audible tail. */
#define RING_BYTES (512 * 1024)
/* Wait for half a second of sound before starting, or the first notes stutter. */
#define PREBUFFER (BYTES_PER_SEC / 2)

static struct {
	bool ready;
	SDL_AudioDeviceID dev;

	SDL_mutex *lock;
	/* Raised by the audio callback once it has taken bytes out, which is the only
	 * thing that ever makes room for a writer. */
	SDL_cond *drained;
	uint8_t data[RING_BYTES];
	size_t head;
	size_t tail;
	size_t fill;
	/* Stream position of the newest byte in the ring. Together with the fill level
	 * this is what makes the buffer readable as a clock. */
	double head_pts;
	bool timed;

	SDL_atomic_t volume;
	SDL_atomic_t interrupt;
	bool started;
	bool paused;
} out;

static void on_device_wants_audio(void *user, Uint8 *dst, int len) {
	(void)user;

	SDL_LockMutex(out.lock);

	size_t take = (size_t)len < out.fill ? (size_t)len : out.fill;
	size_t until_end = RING_BYTES - out.tail;
	size_t first = take < until_end ? take : until_end;

	memcpy(dst, out.data + out.tail, first);
	if (take > first) memcpy(dst + first, out.data, take - first);

	out.tail = (out.tail + take) % RING_BYTES;
	out.fill -= take;

	SDL_CondSignal(out.drained);
	SDL_UnlockMutex(out.lock);

	if (take < (size_t)len) memset(dst + take, 0, (size_t)len - take);

	int volume = SDL_AtomicGet(&out.volume);
	if (volume >= 100 || take == 0) return;

	int16_t *samples = (int16_t *)dst;
	for (size_t i = 0; i < take / sizeof(int16_t); i++) {
		samples[i] = (int16_t)((int)samples[i] * volume / 100);
	}
}

bool audio_out_init(char *err, size_t err_len) {
	if (out.ready) return true;

	memset(&out, 0, sizeof(out));
	SDL_AtomicSet(&out.volume, 70);

	out.lock = SDL_CreateMutex();
	out.drained = SDL_CreateCond();
	if (!out.lock || !out.drained) {
		snprintf(err, err_len, "SDL_CreateMutex/Cond: %.180s", SDL_GetError());
		return false;
	}

	SDL_AudioSpec want = {
		.freq = AUDIO_OUT_RATE,
		.format = AUDIO_S16SYS,
		.channels = AUDIO_OUT_CHANNELS,
		.samples = DEV_SAMPLES,
		.callback = on_device_wants_audio,
	};

	out.dev = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
	if (out.dev == 0) {
		snprintf(err, err_len, "SDL_OpenAudioDevice: %.180s", SDL_GetError());
		return false;
	}

	out.ready = true;
	return true;
}

void audio_out_exit(void) {
	if (!out.ready) return;

	SDL_CloseAudioDevice(out.dev);
	SDL_DestroyCond(out.drained);
	SDL_DestroyMutex(out.lock);
	out.ready = false;
}

bool audio_out_push(const uint8_t *pcm, size_t len, double pts) {
	SDL_LockMutex(out.lock);

	/* The timestamp names the first sample handed over, so it is carried forward by
	 * the length of the write to end up describing the newest byte instead. */
	out.head_pts = pts + (double)len / BYTES_PER_SEC;
	out.timed = true;

	while (len > 0) {
		while (out.fill == RING_BYTES) {
			if (SDL_AtomicGet(&out.interrupt)) {
				SDL_UnlockMutex(out.lock);
				return false;
			}
			SDL_CondWaitTimeout(out.drained, out.lock, 100);
		}

		size_t room = RING_BYTES - out.fill;
		size_t take = len < room ? len : room;
		size_t until_end = RING_BYTES - out.head;
		size_t first = take < until_end ? take : until_end;

		memcpy(out.data + out.head, pcm, first);
		if (take > first) memcpy(out.data, pcm + first, take - first);

		out.head = (out.head + take) % RING_BYTES;
		out.fill += take;
		pcm += take;
		len -= take;
	}

	SDL_UnlockMutex(out.lock);
	return true;
}

void audio_out_flush(void) {
	SDL_LockMutex(out.lock);
	out.head = out.tail = out.fill = 0;
	out.head_pts = 0.0;
	out.timed = false;
	SDL_CondBroadcast(out.drained);
	SDL_UnlockMutex(out.lock);
}

bool audio_out_primed(void) {
	SDL_LockMutex(out.lock);
	bool primed = out.fill >= PREBUFFER;
	SDL_UnlockMutex(out.lock);
	return primed;
}

void audio_out_start(void) {
	if (out.started) return;
	out.started = true;
	out.paused = false;
	SDL_PauseAudioDevice(out.dev, 0);
}

void audio_out_pause(bool paused) {
	out.paused = paused;
	SDL_PauseAudioDevice(out.dev, paused || !out.started);
}

bool audio_out_paused(void) {
	return out.paused;
}

double audio_out_clock(void) {
	SDL_LockMutex(out.lock);
	double clock = out.timed ? out.head_pts - (double)out.fill / BYTES_PER_SEC : -1.0;
	SDL_UnlockMutex(out.lock);
	return clock;
}

void audio_out_set_volume(int percent) {
	if (percent < 0) percent = 0;
	if (percent > 100) percent = 100;
	SDL_AtomicSet(&out.volume, percent);
}

int audio_out_volume(void) {
	return SDL_AtomicGet(&out.volume);
}

void audio_out_interrupt(bool on) {
	SDL_AtomicSet(&out.interrupt, on ? 1 : 0);
	if (!on) return;

	SDL_LockMutex(out.lock);
	SDL_CondBroadcast(out.drained);
	SDL_UnlockMutex(out.lock);
}

void audio_out_stop(void) {
	if (!out.ready) return;

	SDL_PauseAudioDevice(out.dev, 1);
	out.started = false;
	out.paused = false;
	audio_out_flush();
}
