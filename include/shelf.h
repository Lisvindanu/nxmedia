#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "mediavault.h"

/*
 * Two lists kept on the card: what you played, and what you marked.
 *
 * They exist because everything else in the YouTube pane is borrowed from a
 * server that could be down, rate limited, or simply slow. These two answer from
 * the card, so the pane has something to show even when nothing else does -- and
 * finding again what you already found once should not cost another search.
 *
 * Losing either is a small matter, so a write that fails is not reported: the
 * pane still works, it just forgets.
 */

typedef enum {
	SHELF_HISTORY,
	SHELF_FAVOURITES,
	/**
	 * What has been downloaded to the card.
	 *
	 * The card already holds the files, but only under the names the server gave
	 * them -- transliterated, with an id glued on. This keeps the title, the author
	 * and the id beside each one, which is the difference between a list you can
	 * read and a folder you have to search.
	 */
	SHELF_SAVED,
	SHELF_COUNT,
} ShelfKind;

/** Reads both lists off the card. Safe to call more than once. */
void shelf_load(void);

/** Frees both. The lists on the card are left as they are. */
void shelf_exit(void);

const MediaListing *shelf_list(ShelfKind kind);

/**
 * Records that this was played. Moves an item already there back to the front
 * rather than repeating it, and drops the oldest once the list is full.
 */
void shelf_remember(const MediaItem *item);

/** Adds or removes, and reports which it did. */
bool shelf_toggle_favourite(const MediaItem *item);

bool shelf_is_favourite(const char *id);

/** Notes that this now exists on the card. Replacing an entry keeps it in place. */
void shelf_remember_saved(const MediaItem *item);

/** Seconds into `id` when it was last left, or 0 if it was never left part-way. */
int shelf_position(const char *id);

/**
 * Records how far into `id` playback had reached.
 *
 * Deliberately does not reorder the history: this is called while watching, and
 * moving the entry every minute would make the list shuffle under the cursor of
 * someone browsing it. A position close enough to the end is stored as zero, so
 * something watched through starts over rather than resuming at its credits.
 */
void shelf_remember_position(const char *id, int seconds);
