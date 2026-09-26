#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "settings.h"
#include "http.h"

/*
 * Client for the MediaVault public API, which fetches YouTube media with yt-dlp on
 * a server of ours and serves the result over plain HTTP.
 *
 * Downloading is two steps rather than one. Asking for a file starts a server-side
 * merge that takes tens of seconds, so the status endpoint is polled until it says
 * ready and only then are the bytes fetched. Doing it in one request would leave
 * the connection silent long enough for the stall detector to cut it.
 */

#define MEDIA_SEARCH_MAX 128

typedef struct {
	char *id;
	/** As YouTube has it, so possibly non-ASCII. For display only. */
	char *title;
	/**
	 * What to call the file on the card. The server transliterates the title and
	 * appends a short id, which is what keeps two different videos from landing on
	 * the same name -- a collision would otherwise overwrite silently.
	 */
	char *filename;
	char *author;
	/** Bytes, or 0 while the server has not merged the file and cannot know. */
	int64_t size;
	int64_t views;
	int duration;
} MediaItem;

typedef struct {
	MediaItem *items;
	size_t count;
} MediaListing;

void media_listing_free(MediaListing *listing);
void media_item_free(MediaItem *item);

/**
 * What the server is showing everyone right now. Fetched when the pane opens so
 * there is something to look at before anyone has thought of a search term.
 */
bool media_trending(const Settings *cfg, MediaListing *out, char *err, size_t err_len);

/** Searches YouTube through the server. Text is sent as typed. */
bool media_search(const Settings *cfg, const char *text, MediaListing *out,
		char *err, size_t err_len);

/**
 * Called while the server is still merging, so the caller can show that something
 * is happening rather than appearing stuck. Returning false gives up the wait.
 */
typedef bool (*MediaWaiting)(void *user, int seconds_waited);

/**
 * Asks the server to resolve this video's audio and reports the URL to play.
 *
 * The proxy does not send a single byte until yt-dlp has finished with it, which
 * on a cold video is twelve to twenty seconds -- longer than ffmpeg's own read
 * timeout, so handing the proxy straight to the player makes it give up half the
 * time. This asks the small metadata route first, which does the same resolving
 * for a kilobyte of JSON, and leaves the proxy warm enough to open at once.
 */
bool media_prepare_audio(const Settings *cfg, const MediaItem *item,
		char *url_out, size_t url_len, char *err, size_t err_len);

/**
 * Waits for the server to finish merging, then reports a URL the player can open.
 *
 * The same file the download uses: a complete MP4 served with byte ranges, which
 * ffmpeg streams as happily as it reads one off the card -- and being seekable is
 * what lets the scrub bar work on it. Nothing is written to the card.
 */
bool media_prepare_video(const Settings *cfg, const MediaItem *item,
		char *url_out, size_t url_len, void *user, MediaWaiting on_waiting,
		char *err, size_t err_len);

/**
 * Waits for the server to have the file, then downloads it into dest_dir.
 *
 * The wait is budgeted from the video's duration rather than a fixed number:
 * merging runs roughly in proportion to length, so one timeout cannot fit both a
 * three-minute song and an hour-long set.
 */
bool media_download(const Settings *cfg, const MediaItem *item, const char *dest_dir,
		HttpProgress on_progress, void *user, MediaWaiting on_waiting,
		char *err, size_t err_len);
