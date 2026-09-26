#include "mediavault.h"

#include <stdlib.h>

/*
 * The lifetime of one search result, kept apart from the code that fetches them.
 *
 * Nothing here talks to the network or to the console, which is what lets the
 * history and favourites lists be tested with a host compiler instead of only on
 * the card.
 */

void media_item_free(MediaItem *item) {
	free(item->id);
	free(item->title);
	free(item->filename);
	free(item->author);
	*item = (MediaItem){0};
}

void media_listing_free(MediaListing *listing) {
	for (size_t i = 0; i < listing->count; i++) media_item_free(&listing->items[i]);
	free(listing->items);
	listing->items = NULL;
	listing->count = 0;
}
