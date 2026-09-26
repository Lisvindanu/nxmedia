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

/*
 * Merging runs at roughly a third of playback time: measured twice on the server
 * we use, 60 seconds for a 3m33s video and 184 seconds for a ten-minute one, which
 * is 0.28 and 0.31 of their length. Budgeting double that absorbs a loaded server
 * without letting a job that is truly stuck hang around forever.
 *
 * The ceiling has to clear the longest thing anyone would queue: at a third of
 * real time, half an hour of budget covers a video of about an hour and a half.
 */
#define WAIT_PER_SECOND_OF_VIDEO 0.6
#define WAIT_FLOOR_SECONDS 90
#define WAIT_CEILING_SECONDS 1800
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

/** Copies one search result out of the array. False means it was unusable. */
static bool read_item(json_object *node, MediaItem *out) {
	const char *id = string_field(node, "videoId");
	const char *filename = string_field(node, "filename");
	if (!id || !filename) return false;

	const char *title = string_field(node, "title");
	const char *author = string_field(node, "author");
	const char *author_id = string_field(node, "authorId");

	*out = (MediaItem){
		.id = strdup(id),
		.title = strdup(title ? title : filename),
		.filename = strdup(filename),
		.author = author ? strdup(author) : NULL,
		.author_id = author_id ? strdup(author_id) : NULL,
		.size = int_field(node, "size"),
		.views = int_field(node, "viewCount"),
		.duration = (int)int_field(node, "lengthSeconds"),
	};

	if (!out->id || !out->title || !out->filename) {
		media_item_free(out);
		return false;
	}
	return true;
}

/*
 * Search, trending and a channel's videos all answer with the same envelope, so
 * they share a reader. Kept apart from the fetching because the channel body is
 * also read a second time, for the channel's own name.
 */
static bool parse_listing(const char *text, MediaListing *out, char *err, size_t err_len) {
	*out = (MediaListing){0};

	json_object *root = json_tokener_parse(text ? text : "");

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

static bool fetch_listing(const char *url, MediaListing *out, char *err, size_t err_len) {
	HttpBuffer body = {0};
	if (!http_get(url, NULL, &body, err, err_len)) {
		*out = (MediaListing){0};
		return false;
	}

	bool ok = parse_listing(body.data, out, err, err_len);
	http_buffer_free(&body);
	return ok;
}

bool media_trending(const Settings *cfg, MediaListing *out, char *err, size_t err_len) {
	char url[640];
	snprintf(url, sizeof(url), "%s/trending", cfg->mediavault_url);
	return fetch_listing(url, out, err, err_len);
}

bool media_channel(const Settings *cfg, const char *channel_id, MediaListing *out,
		char *name_out, size_t name_len, char *err, size_t err_len) {
	char *escaped = http_escape(channel_id);
	if (!escaped) {
		*out = (MediaListing){0};
		set_err(err, err_len, "gagal menyusun URL channel");
		return false;
	}

	char url[768];
	snprintf(url, sizeof(url), "%s/channel/%s", cfg->mediavault_url, escaped);
	http_free_escaped(escaped);

	/* The name arrives beside the videos rather than in them, so it is read from a
	 * second look at the same body. Cheap next to the request itself. */
	HttpBuffer body = {0};
	if (!http_get(url, NULL, &body, err, err_len)) {
		*out = (MediaListing){0};
		return false;
	}

	json_object *root = json_tokener_parse(body.data ? body.data : "");
	if (root) {
		json_object *channel = NULL;
		if (json_object_object_get_ex(root, "channel", &channel)) {
			const char *name = string_field(channel, "name");
			if (name) snprintf(name_out, name_len, "%s", name);
		}
		json_object_put(root);
	}

	bool ok = parse_listing(body.data, out, err, err_len);
	http_buffer_free(&body);
	return ok;
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

bool media_prepare_audio(const Settings *cfg, const MediaItem *item,
		char *url_out, size_t url_len, char *err, size_t err_len) {
	char url[640];
	snprintf(url, sizeof(url), "%s/audio-url/%s", cfg->mediavault_url, item->id);

	HttpBuffer body = {0};
	if (!http_get(url, NULL, &body, err, err_len)) return false;

	/* Only the success flag is read. The body also carries a direct googlevideo
	 * URL, which is deliberately ignored: those are tied to the address that asked
	 * for them, and the console is not that address. */
	json_object *root = json_tokener_parse(body.data ? body.data : "");
	http_buffer_free(&body);

	if (!root) {
		set_err(err, err_len, "jawaban audio tidak dikenali");
		return false;
	}

	json_object *ok = NULL;
	bool usable = json_object_object_get_ex(root, "success", &ok) &&
			json_object_get_boolean(ok);
	json_object_put(root);

	if (!usable) {
		set_err(err, err_len, "server tidak bisa menyiapkan audionya");
		return false;
	}

	snprintf(url_out, url_len, "%s/audio/%s", cfg->mediavault_url, item->id);
	return true;
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

/* Generous, because the cost here is the server resolving the video and the
 * heaviest one measured took twenty-five seconds. Still bounded: a server that
 * never answers has to stop being waited for. */
#define WARM_TIMEOUT_SECONDS 60

bool media_prepare_stream(const Settings *cfg, const MediaItem *item,
		char *url_out, size_t url_len, char *err, size_t err_len) {
	char url[640];
	snprintf(url, sizeof(url), "%s/proxy/%s", cfg->mediavault_url, item->id);

	/* Printed before the wait, not after: when this is the last line in the log, the
	 * warm-up is where it stopped, and the URL says which route was taken. */
	printf("[media] memanaskan %s\n", url);

	HttpBuffer reply = {0};
	if (!http_touch(url, WARM_TIMEOUT_SECONDS, &reply, err, err_len)) {
		/*
		 * The server says why in its body, and its reason is worth far more than the
		 * status code: "members only" or "unavailable in your country" is something a
		 * person can act on, where "HTTP 500" is not.
		 */
		json_object *root = json_tokener_parse(reply.data ? reply.data : "");
		if (root) {
			const char *reason = string_field(root, "error");
			if (!reason) reason = string_field(root, "message");
			if (reason) set_err(err, err_len, "%.200s", reason);
			json_object_put(root);
		}

		http_buffer_free(&reply);
		printf("[media] pemanasan gagal: %s\n", err);
		return false;
	}

	snprintf(url_out, url_len, "%s", url);
	return true;
}

bool media_prepare_video(const Settings *cfg, const MediaItem *item,
		char *url_out, size_t url_len, void *user, MediaWaiting on_waiting,
		char *err, size_t err_len) {
	MediaState state;
	if (!wait_until_ready(cfg, item, &state, user, on_waiting, err, err_len)) return false;

	if (!state.url[0]) {
		set_err(err, err_len, "server bilang siap tapi tidak memberi URL");
		return false;
	}

	snprintf(url_out, url_len, "%s", state.url);
	return true;
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
