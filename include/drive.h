#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Google Drive as a place to play from, not to copy from.
 *
 * Drive serves a file over plain HTTP with byte ranges, which is all ffmpeg asks
 * of a source -- so a film in Drive can be watched the same way an IPTV channel
 * is, without a gigabyte landing on the card first just to be watched once.
 *
 * The credentials are nxdrive's, read from its own file and never written back.
 * Two apps holding two copies of the same refresh token would mean two things to
 * revoke; and there is nothing here that nxdrive has not already been granted.
 * Where that file is absent the Drive section simply does not appear.
 */

#define DRIVE_ROOT "root"
#define DRIVE_ID_MAX 96
#define DRIVE_URL_MAX 320
#define DRIVE_SEARCH_MAX 128

typedef struct {
	char *id;
	char *name;
	bool is_folder;
	/**
	 * Whether this carries pictures. Sound alone leaves the list on screen so the
	 * next thing can be found while it plays, the same as the card listing does.
	 */
	bool is_video;
	/** Bytes, or -1 for a folder and for anything Drive declines to size. */
	int64_t size;
} DriveEntry;

typedef struct {
	DriveEntry *items;
	size_t count;
} DriveListing;

/**
 * Whether nxdrive's config is present and complete enough to try.
 *
 * Checked before the section is offered rather than when it is entered: a menu
 * row that always fails is worse than one that is not there.
 */
bool drive_available(void);

/** Folders and playable files inside `folder_id`, folders first then by name. */
bool drive_list(const char *folder_id, DriveListing *out, char *err, size_t err_len);

/**
 * Everything whose name contains `text`, anywhere in the drive, newest first.
 *
 * Browsing alone is not enough to find anything in a drive that has been used for
 * years: the folder something is in is exactly what gets forgotten. This searches
 * across all of it and ignores where things sit.
 */
bool drive_search(const char *text, DriveListing *out, char *err, size_t err_len);

void drive_listing_free(DriveListing *listing);

/**
 * Fills in what the player needs to open this file: the media URL, and a token to
 * present with it.
 *
 * The token is handed over rather than embedded in the URL because Drive stopped
 * accepting credentials as a query parameter. It is valid for an hour, which
 * outlasts any single viewing, and it is fetched fresh if the cached one is close
 * to expiring -- a token that dies mid-film would stall the stream with no way to
 * explain itself.
 */
bool drive_stream(const char *file_id, char *url_out, size_t url_len,
		char *token_out, size_t token_len, char *err, size_t err_len);
