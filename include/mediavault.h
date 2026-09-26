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
	int duration;
} MediaItem;

typedef struct {
	MediaItem *items;
	size_t count;
} MediaListing;

void media_listing_free(MediaListing *listing);
void media_item_free(MediaItem *item);

/** Searches YouTube through the server. Text is sent as typed. */
bool media_search(const Settings *cfg, const char *text, MediaListing *out,
		char *err, size_t err_len);

/**
 * Called while the server is still merging, so the caller can show that something
 * is happening rather than appearing stuck. Returning false gives up the wait.
 */
typedef bool (*MediaWaiting)(void *user, int seconds_waited);

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
