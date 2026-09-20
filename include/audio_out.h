#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The SDL audio device and the ring buffer feeding it. The device is opened once at
 * startup and never reopened, so the decoder converts to this one format rather than
 * the device chasing whatever a station happens to send.
 *
 * This is also the master clock. Video is timed against the audio that is actually
 * leaving the speakers, because drift there is the one kind a person can hear.
 */

#define AUDIO_OUT_RATE 48000
#define AUDIO_OUT_CHANNELS 2

bool audio_out_init(char *err, size_t err_len);
void audio_out_exit(void);

/**
 * Hands over interleaved 16-bit stereo at AUDIO_OUT_RATE, blocking while the ring is
 * full. `pts` is the stream position of the first sample. False means an interrupt
 * landed mid-wait, which is the caller's signal to unwind.
 */
bool audio_out_push(const uint8_t *pcm, size_t len, double pts);

/** Drops everything queued. For a seek, where the buffered sound is now wrong. */
void audio_out_flush(void);

/** Whether enough has piled up that starting will not immediately underrun. */
bool audio_out_primed(void);

void audio_out_start(void);
void audio_out_pause(bool paused);
bool audio_out_paused(void);

/** Silences the device and empties the ring, ready for whatever plays next. */
void audio_out_stop(void);

/** Where the speakers are in the stream, or -1 before any audio has been queued. */
double audio_out_clock(void);

void audio_out_set_volume(int percent);
int audio_out_volume(void);

/** Releases a blocked audio_out_push so a stopping worker is not stuck waiting. */
void audio_out_interrupt(bool on);
