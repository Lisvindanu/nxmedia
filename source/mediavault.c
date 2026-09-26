#include "mediavault.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <json-c/json.h>
#include <switch.h>

#include "util.h"

/* Long enough that a slow server still answers, short enough that a dead one is
 * noticed before the user gives up on the app. */
#define POLL_EVERY_NS 6000000000ULL
/* The server allows thirty polls a minute; asking every six seconds uses ten of
 * them, which leaves room for the rest of the queue. */
#define POLL_EVERY_SECONDS 6

/* Merging runs at roughly a fifth of playback time on the server we use, measured
 * at 21 seconds for a four-minute video. Tripling that leaves room for a loaded
 * server without making a stuck job hang around forever. */
#define WAIT_PER_SECOND_OF_VIDEO 0.6
#define WAIT_FLOOR_SECONDS 90
#define WAIT_CEILING_SECONDS 900
/* Room for whatever the server has to say when it turns a request down. */
#define MESSAGE_MAX 160

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

/** Copies one search result out of the array. False means it was unusable. */
static bool read_item(json_object *node, MediaItem *out) {
	const char *id = string_field(node, "videoId");
	const char *filename = string_field(node, "filename");
	if (!id || !filename) return false;

	const char *title = string_field(node, "title");
	const char *author = string_field(node, "author");

	*out = (MediaItem){
		.id = strdup(id),
		.title = strdup(title ? title : filename),
		.filename = strdup(filename),
		.author = author ? strdup(author) : NULL,
		.size = int_field(node, "size"),
		.duration = (int)int_field(node, "lengthSeconds"),
	};

	if (!out->id || !out->title || !out->filename) {
		media_item_free(out);
		return false;
	}
	return true;
}

/* Search and trending answer with the same envelope, so they share a reader. */
static bool fetch_listing(const char *url, MediaListing *out, char *err, size_t err_len) {
	*out = (MediaListing){0};

	HttpBuffer body = {0};
	if (!http_get(url, NULL, &body, err, err_len)) return false;

	json_object *root = json_tokener_parse(body.data ? body.data : "");
	http_buffer_free(&body);

	json_object *data = NULL;
	if (!root || !json_object_object_get_ex(root, "data", &data) ||
			!json_object_is_type(data, json_type_array)) {
		if (root) json_object_put(root);
		set_err(err, err_len, "jawaban server tidak dikenali");
		return false;
	}

	size_t total = json_object_array_length(data);
	if (total == 0) {
		json_object_put(root);
		return true;
	}

	out->items = calloc(total, sizeof(MediaItem));
	if (!out->items) {
		json_object_put(root);
		set_err(err, err_len, "kehabisan memori");
		return false;
	}

	for (size_t i = 0; i < total; i++) {
		if (read_item(json_object_array_get_idx(data, i), &out->items[out->count])) {
			out->count++;
		}
	}

	json_object_put(root);
	return true;
}

bool media_trending(const Settings *cfg, MediaListing *out, char *err, size_t err_len) {
	char url[640];
	snprintf(url, sizeof(url), "%s/trending", cfg->mediavault_url);
	return fetch_listing(url, out, err, err_len);
}

bool media_search(const Settings *cfg, const char *text, MediaListing *out,
		char *err, size_t err_len) {
	char *escaped = http_escape(text);
	if (!escaped) {
		*out = (MediaListing){0};
		set_err(err, err_len, "gagal menyusun URL pencarian");
		return false;
	}

	char url[768];
	snprintf(url, sizeof(url), "%s/search?q=%s", cfg->mediavault_url, escaped);
	http_free_escaped(escaped);

	return fetch_listing(url, out, err, err_len);
}

typedef struct {
	char url[512];
	char filename[256];
	int64_t size;
	bool ready;
	bool rejected;
	char message[MESSAGE_MAX];
} MediaState;

/** One look at the status endpoint. False means the request itself failed. */
static bool read_state(const Settings *cfg, const char *id, MediaState *out,
		char *err, size_t err_len) {
	*out = (MediaState){0};

	char url[640];
	snprintf(url, sizeof(url), "%s/download/%s", cfg->mediavault_url, id);

	HttpBuffer body = {0};
	if (!http_get(url, NULL, &body, err, err_len)) return false;

	json_object *root = json_tokener_parse(body.data ? body.data : "");
	http_buffer_free(&body);

	if (!root) {
		set_err(err, err_len, "jawaban status tidak dikenali");
		return false;
	}

	const char *state = string_field(root, "state");
	out->ready = state && strcmp(state, "ready") == 0;
	out->rejected = state && strcmp(state, "rejected") == 0;
	out->size = int_field(root, "size");

	const char *url_field = string_field(root, "url");
	if (url_field) snprintf(out->url, sizeof(out->url), "%s", url_field);

	const char *name = string_field(root, "filename");
	if (name) snprintf(out->filename, sizeof(out->filename), "%s", name);

	const char *message = string_field(root, "message");
	if (message) snprintf(out->message, sizeof(out->message), "%s", message);

	json_object_put(root);
	return true;
}

/**
 * A server that has not been asked for this video yet starts merging on the first
 * poll, so the wait has to allow for the whole merge and not just for a queue.
 */
static int wait_budget(int duration) {
	double scaled = duration > 0 ? duration * WAIT_PER_SECOND_OF_VIDEO : 0;
	if (scaled < WAIT_FLOOR_SECONDS) scaled = WAIT_FLOOR_SECONDS;
	if (scaled > WAIT_CEILING_SECONDS) scaled = WAIT_CEILING_SECONDS;
	return (int)scaled;
}

static bool wait_until_ready(const Settings *cfg, const MediaItem *item,
		MediaState *out, void *user, MediaWaiting on_waiting, char *err, size_t err_len) {
	int budget = wait_budget(item->duration);

	for (int waited = 0; ; waited += POLL_EVERY_SECONDS) {
		if (!read_state(cfg, item->id, out, err, err_len)) return false;

		if (out->ready) return true;

		if (out->rejected) {
			set_err(err, err_len, "%s",
					out->message[0] ? out->message : "server menolak permintaan");
			return false;
		}

		if (waited >= budget) {
			set_err(err, err_len, "server belum siap setelah %d detik", waited);
			return false;
		}

		if (on_waiting && !on_waiting(user, waited)) {
			set_err(err, err_len, "dibatalkan");
			return false;
		}

		svcSleepThread(POLL_EVERY_NS);
	}
}

bool media_download(const Settings *cfg, const MediaItem *item, const char *dest_dir,
		HttpProgress on_progress, void *user, MediaWaiting on_waiting,
		char *err, size_t err_len) {
	MediaState state;
	if (!wait_until_ready(cfg, item, &state, user, on_waiting, err, err_len)) return false;

	if (!state.url[0]) {
		set_err(err, err_len, "server bilang siap tapi tidak memberi URL");
		return false;
	}

	/* The name is taken from the ready response rather than from the search result:
	 * only the server that produced the file knows what extension it ended up with. */
	char safe_name[256];
	sanitize_filename(state.filename[0] ? state.filename : item->filename,
			safe_name, sizeof(safe_name));

	char dest_path[512];
	int written = snprintf(dest_path, sizeof(dest_path), "%s/%s", dest_dir, safe_name);
	if (written < 0 || (size_t)written >= sizeof(dest_path)) {
		set_err(err, err_len, "path tujuan terlalu panjang");
		return false;
	}

	bool resumed = false;
	if (!http_download(state.url, NULL, dest_path, item->id, &resumed,
			on_progress, user, err, err_len)) {
		return false;
	}

	fsdevCommitDevice("sdmc");

	struct stat landed;
	if (stat(dest_path, &landed) != 0) {
		set_err(err, err_len, "hilang setelah pindah: %.200s", dest_path);
		return false;
	}

	/* The server reports the exact byte count once the merge is done, so unlike the
	 * search result this figure can be trusted to check the transfer against. */
	if (state.size > 0 && (int64_t)landed.st_size != state.size) {
		set_err(err, err_len, "ukuran tidak cocok: %lld dari %lld byte",
				(long long)landed.st_size, (long long)state.size);
		return false;
	}

	return true;
}
