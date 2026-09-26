#include "shelf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <json-c/json.h>

#ifdef __SWITCH__
#include <switch.h>
#endif

#include "util.h"

/* Overridable so the host test can point the lists at a scratch directory; the
 * console build never defines it and gets the card. */
#ifndef SHELF_DIR
#define SHELF_DIR "sdmc:/switch/nxmedia"
#endif
/* Enough to be useful, small enough that the whole file is read and written in
 * one go without anyone noticing. */
#define HISTORY_MAX 60
#define FAVOURITES_MAX 120
#define SHELF_MAX_BYTES (512 * 1024)

static struct {
	MediaListing list;
	size_t cap;
	const char *path;
} shelves[SHELF_COUNT] = {
	[SHELF_HISTORY] = { .cap = HISTORY_MAX, .path = SHELF_DIR "/history.json" },
	[SHELF_FAVOURITES] = { .cap = FAVOURITES_MAX, .path = SHELF_DIR "/favourites.json" },
};

static bool loaded;

/* ------------------------------------------------------------------- reading */

static const char *string_field(json_object *obj, const char *key) {
	json_object *value = NULL;
	if (!json_object_object_get_ex(obj, key, &value)) return NULL;
	if (!json_object_is_type(value, json_type_string)) return NULL;

	const char *text = json_object_get_string(value);
	return (text && text[0]) ? text : NULL;
}

static int64_t int_field(json_object *obj, const char *key) {
	json_object *value = NULL;
	if (!json_object_object_get_ex(obj, key, &value)) return 0;
	if (!json_object_is_type(value, json_type_int)) return 0;
	return json_object_get_int64(value);
}

static char *read_file(const char *path) {
	FILE *file = fopen(path, "rb");
	if (!file) return NULL;

	if (fseek(file, 0, SEEK_END) != 0) {
		fclose(file);
		return NULL;
	}

	long size = ftell(file);
	if (size <= 0 || size > SHELF_MAX_BYTES) {
		fclose(file);
		return NULL;
	}
	rewind(file);

	char *text = malloc((size_t)size + 1);
	if (!text) {
		fclose(file);
		return NULL;
	}

	size_t read = fread(text, 1, (size_t)size, file);
	fclose(file);
	text[read] = '\0';
	return text;
}

static bool copy_item(json_object *node, MediaItem *out) {
	const char *id = string_field(node, "id");
	const char *filename = string_field(node, "filename");
	if (!id || !filename) return false;

	const char *title = string_field(node, "title");
	const char *author = string_field(node, "author");

	*out = (MediaItem){
		.id = strdup(id),
		.title = strdup(title ? title : filename),
		.filename = strdup(filename),
		.author = author ? strdup(author) : NULL,
		.views = int_field(node, "views"),
		.duration = (int)int_field(node, "duration"),
	};

	if (!out->id || !out->title || !out->filename) {
		media_item_free(out);
		return false;
	}
	return true;
}

static void load_one(ShelfKind kind) {
	char *text = read_file(shelves[kind].path);
	if (!text) return;

	json_object *root = json_tokener_parse(text);
	free(text);

	if (!root) return;

	if (json_object_is_type(root, json_type_array)) {
		size_t total = json_object_array_length(root);
		if (total > shelves[kind].cap) total = shelves[kind].cap;

		MediaListing *list = &shelves[kind].list;
		list->items = calloc(shelves[kind].cap, sizeof(MediaItem));

		if (list->items) {
			for (size_t i = 0; i < total; i++) {
				if (copy_item(json_object_array_get_idx(root, i), &list->items[list->count])) {
					list->count++;
				}
			}
		}
	}

	json_object_put(root);
}

void shelf_load(void) {
	if (loaded) return;
	loaded = true;

	for (int kind = 0; kind < SHELF_COUNT; kind++) load_one((ShelfKind)kind);
}

void shelf_exit(void) {
	for (int kind = 0; kind < SHELF_COUNT; kind++) media_listing_free(&shelves[kind].list);
	loaded = false;
}

/* ------------------------------------------------------------------- writing */

static void save_one(ShelfKind kind) {
	if (!mkdir_p(SHELF_DIR)) return;

	json_object *root = json_object_new_array();
	if (!root) return;

	const MediaListing *list = &shelves[kind].list;
	for (size_t i = 0; i < list->count; i++) {
		const MediaItem *item = &list->items[i];

		json_object *node = json_object_new_object();
		if (!node) continue;

		json_object_object_add(node, "id", json_object_new_string(item->id));
		json_object_object_add(node, "title", json_object_new_string(item->title));
		json_object_object_add(node, "filename", json_object_new_string(item->filename));
		if (item->author) {
			json_object_object_add(node, "author", json_object_new_string(item->author));
		}
		json_object_object_add(node, "views", json_object_new_int64(item->views));
		json_object_object_add(node, "duration", json_object_new_int64(item->duration));

		json_object_array_add(root, node);
	}

	const char *text = json_object_to_json_string(root);
	FILE *file = text ? fopen(shelves[kind].path, "w") : NULL;

	if (file) {
		fputs(text, file);
		fclose(file);

		/*
		 * Closing only empties stdio into the filesystem; the directory entry stays
		 * in Horizon's cache until the device is committed. Without this the lists
		 * read back fine all session and are gone after a reboot -- the same trap
		 * that lost finished downloads in nxdrive and nearly lost the self-update.
		 *
		 * Guarded because this file is also built by the host test, where there is
		 * no Horizon and the write has already reached the disk.
		 */
#ifdef __SWITCH__
		fsdevCommitDevice("sdmc");
#endif
	}

	json_object_put(root);
}

/* ------------------------------------------------------------------ the lists */

const MediaListing *shelf_list(ShelfKind kind) {
	return &shelves[kind].list;
}

static size_t find(ShelfKind kind, const char *id) {
	const MediaListing *list = &shelves[kind].list;
	for (size_t i = 0; i < list->count; i++) {
		if (strcmp(list->items[i].id, id) == 0) return i;
	}
	return (size_t)-1;
}

static void remove_at(ShelfKind kind, size_t index) {
	MediaListing *list = &shelves[kind].list;

	media_item_free(&list->items[index]);
	memmove(&list->items[index], &list->items[index + 1],
			(list->count - index - 1) * sizeof(MediaItem));
	list->count--;
}

/** Copies an item to the front, pushing the rest down. False means out of memory. */
static bool push_front(ShelfKind kind, const MediaItem *item) {
	MediaListing *list = &shelves[kind].list;

	if (!list->items) {
		list->items = calloc(shelves[kind].cap, sizeof(MediaItem));
		if (!list->items) return false;
	}

	MediaItem copy = {
		.id = strdup(item->id),
		.title = strdup(item->title),
		.filename = strdup(item->filename),
		.author = item->author ? strdup(item->author) : NULL,
		.views = item->views,
		.duration = item->duration,
	};

	if (!copy.id || !copy.title || !copy.filename) {
		media_item_free(&copy);
		return false;
	}

	if (list->count == shelves[kind].cap) remove_at(kind, list->count - 1);

	memmove(&list->items[1], &list->items[0], list->count * sizeof(MediaItem));
	list->items[0] = copy;
	list->count++;
	return true;
}

void shelf_remember(const MediaItem *item) {
	shelf_load();

	/* Watching something again moves it back to the front rather than adding a
	 * second line for it, which is what makes the list read as places you have
	 * been rather than a tally of how often. */
	size_t at = find(SHELF_HISTORY, item->id);
	if (at != (size_t)-1) remove_at(SHELF_HISTORY, at);

	if (push_front(SHELF_HISTORY, item)) save_one(SHELF_HISTORY);
}

bool shelf_toggle_favourite(const MediaItem *item) {
	shelf_load();

	size_t at = find(SHELF_FAVOURITES, item->id);
	bool now_favourite;

	if (at != (size_t)-1) {
		remove_at(SHELF_FAVOURITES, at);
		now_favourite = false;
	} else {
		now_favourite = push_front(SHELF_FAVOURITES, item);
	}

	save_one(SHELF_FAVOURITES);
	return now_favourite;
}

bool shelf_is_favourite(const char *id) {
	return id && find(SHELF_FAVOURITES, id) != (size_t)-1;
}
