#pragma once

#include <stdbool.h>
#include <stddef.h>

/*
 * Extended M3U, the format every IPTV playlist is written in. Only the parts that
 * change what the user sees or whether a channel opens at all are read; the rest of
 * the #EXT tags in the wild are electronic programme guide data for players that
 * have a programme guide.
 */

typedef struct {
	char *name;
	char *url;
	/** What the playlist filed the channel under -- news, sport, religious. */
	char *group;
	/** Sent as Referer. Several hosts hand back a 403 without it. NULL for none. */
	char *referer;
	/** Sent as User-Agent when the playlist names one. NULL leaves the default. */
	char *user_agent;
} M3uEntry;

typedef struct {
	M3uEntry *items;
	size_t count;
} M3uPlaylist;

/** Replaces `out` with the channels in `text`. An entry with no URL is dropped. */
bool m3u_parse(const char *text, M3uPlaylist *out, char *err, size_t err_len);

void m3u_free(M3uPlaylist *list);
