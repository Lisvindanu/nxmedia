#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Browsing the SD card for something to play. Folders and playable files only:
 * everything else on a Switch card is saves, homebrew and firmware clutter, and
 * hiding it is the difference between a media library and a file manager.
 */

#define LIBRARY_ROOT "sdmc:/"
#define LIBRARY_PATH_MAX 512

/** Which half of the card a listing is after. Folders are in both. */
typedef enum {
	LibraryKind_Audio,
	LibraryKind_Video,
} LibraryKind;

typedef struct {
	char *name;
	/**
	 * The folder this is in, or NULL when it is in the folder being browsed.
	 *
	 * Only a search result carries one, because only a search produces rows from
	 * more than one place at once. Without it the pane would have nothing to join
	 * the name to and every match would open the wrong path.
	 */
	char *dir;
	int64_t size;
	bool is_dir;
} LibraryEntry;

typedef struct {
	LibraryEntry *items;
	size_t count;
} LibraryListing;

/**
 * Replaces `out` with the folders in `dir` and the files of `kind` inside it, folders
 * first and then by name. Music and video are browsed separately, so a listing only
 * ever holds one kind and the rows need no way to tell them apart.
 */
bool library_list(const char *dir, LibraryKind kind, LibraryListing *out,
		char *err, size_t err_len);
void library_listing_free(LibraryListing *listing);

/**
 * Every playable file of `kind` anywhere on the card whose name contains `text`,
 * matched without regard to case. Each result carries the folder it was found in.
 *
 * Folders holding games and system files are skipped outright rather than filtered
 * afterwards. This card's emuMMC alone holds several hundred thousand entries, and
 * walking into it would turn a search into a minute of staring at a frozen screen
 * to find nothing -- there is no music in there by construction.
 */
/**
 * Called as the walk goes, so the screen can say what is happening and the user can
 * stop it. Returning false abandons the search and keeps whatever was found so far.
 */
typedef bool (*LibrarySearchProgress)(void *user, size_t folders, size_t found);

/**
 * `partial` (optional) is set when the walk stopped at one of its own limits rather
 * than at the end of the card, which means there may be more matches it never
 * reached. Saying so matters: silence there reads as "the card has nothing else".
 */
bool library_search(const char *text, LibraryKind kind, LibraryListing *out,
		LibrarySearchProgress on_progress, void *user, bool *partial,
		char *err, size_t err_len);

/** Joins a directory and an entry name. False when the result would not fit. */
bool library_join(const char *dir, const char *name, char *out, size_t out_len);

/** Walks `dir` up one level in place. False at the root, which has no parent. */
bool library_parent(char *dir);
