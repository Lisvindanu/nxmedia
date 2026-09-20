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

/** Joins a directory and an entry name. False when the result would not fit. */
bool library_join(const char *dir, const char *name, char *out, size_t out_len);

/** Walks `dir` up one level in place. False at the root, which has no parent. */
bool library_parent(char *dir);
