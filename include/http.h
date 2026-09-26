#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
	char *data;
	size_t len;
} HttpBuffer;

/** Returns false to abort the transfer. */
typedef bool (*HttpProgress)(void *user, int64_t done, int64_t total);

bool http_init(char *err, size_t err_len);
void http_exit(void);

void http_buffer_free(HttpBuffer *buf);

bool http_get(const char *url, const char *bearer, HttpBuffer *out, char *err, size_t err_len);
bool http_post_form(const char *url, const char *body, HttpBuffer *out, char *err, size_t err_len);

/**
 * Streams to a part file next to dest_path, then renames on success. An existing
 * part file is resumed from its current length rather than restarted.
 *
 * resume_tag names the part file alongside dest_path and must identify the remote
 * content, not just its name: two different files called "game.zip" would otherwise
 * splice into each other. Pass NULL to key the part file on the name alone.
 *
 * Sets *resumed (optional) when bytes from an earlier attempt were actually kept,
 * which is the only case where the result is worth checksumming.
 */
bool http_download(const char *url, const char *bearer, const char *dest_path,
		const char *resume_tag, bool *resumed,
		HttpProgress on_progress, void *user, char *err, size_t err_len);

/**
 * Opens a resumable upload session and returns its URI, which the caller frees.
 * The session URI carries its own credentials, so the later calls need no bearer.
 */
bool http_upload_start(const char *url, const char *bearer, const char *json_metadata,
		int64_t content_length, char **session_uri, char *err, size_t err_len);

/**
 * Asks a session how many bytes it already holds, so an interrupted upload can
 * pick up where it stopped. Sets *complete when the server already has the file.
 */
bool http_upload_offset(const char *session_uri, int64_t total, int64_t *offset,
		bool *complete, char *err, size_t err_len);

bool http_upload_send(const char *session_uri, const char *path, int64_t offset, int64_t total,
		HttpProgress on_progress, void *user, char *err, size_t err_len);

/**
 * TCP receive window the kernel granted the last download socket, in KB. This is
 * the real ceiling on throughput over a long link, so it is worth showing while
 * the speed is still being tuned. Zero before any download has run.
 */
int http_receive_window_kb(void);

/**
 * Asks for the first byte of `url` and discards it, purely so the server has done
 * its work before someone else asks for the rest.
 *
 * This exists because the media proxy sends nothing at all until yt-dlp has
 * finished resolving, which on a cold video outlasts ffmpeg's own read timeout --
 * so the player gives up before the first byte ever arrives. curl waits it out
 * here instead, under a timeout this side chooses.
 */
bool http_touch(const char *url, int timeout_seconds, char *err, size_t err_len);

/** Percent-encodes a value for use in a query string. Free with http_free_escaped. */
char *http_escape(const char *value);
void http_free_escaped(char *escaped);
