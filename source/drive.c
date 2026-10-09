#include "drive.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <json-c/json.h>

#include "http.h"
#include "util.h"

/* nxdrive's file, read and never written. See the note in drive.h. */
#define CONFIG_PATH "sdmc:/switch/nxdrive/nxdrive.json"
#define CONFIG_MAX_BYTES (64 * 1024)

#define TOKEN_ENDPOINT "https://oauth2.googleapis.com/token"
#define FILES_ENDPOINT "https://www.googleapis.com/drive/v3/files"
#define ALL_DRIVES "&supportsAllDrives=true&includeItemsFromAllDrives=true"
#define FOLDER_MIME "application/vnd.google-apps.folder"
#define LIST_FIELDS "nextPageToken,files(id,name,size,mimeType)"
#define PAGE_SIZE 200

/*
 * An access token is good for an hour. It is dropped five minutes early so a film
 * started just before the hour does not stall at the moment it expires -- the
 * stream would simply stop with a 401 the player cannot explain.
 */
#define TOKEN_LIFETIME_SECONDS 3600
#define TOKEN_MARGIN_SECONDS 300

#define TOKEN_MAX 2048

typedef struct {
	char client_id[256];
	char client_secret[128];
	char refresh_token[512];
} Credentials;

static struct {
	Credentials creds;
	bool read;
	bool usable;

	char token[TOKEN_MAX];
	time_t token_until;
} drive;

/* ------------------------------------------------------------ credentials */

static const char *string_field(json_object *obj, const char *key) {
	json_object *value = NULL;
	if (!json_object_object_get_ex(obj, key, &value)) return NULL;
	if (!json_object_is_type(value, json_type_string)) return NULL;

	const char *text = json_object_get_string(value);
	return (text && text[0]) ? text : NULL;
}

static void read_config(void) {
	if (drive.read) return;
	drive.read = true;

	FILE *file = fopen(CONFIG_PATH, "rb");
	if (!file) {
		printf("[drive] tidak ada %s\n", CONFIG_PATH);
		return;
	}

	char text[CONFIG_MAX_BYTES];
	size_t len = fread(text, 1, sizeof(text) - 1, file);
	fclose(file);
	text[len] = '\0';

	json_object *root = json_tokener_parse(text);
	if (!root) return;

	const char *id = string_field(root, "client_id");
	const char *secret = string_field(root, "client_secret");
	const char *refresh = string_field(root, "refresh_token");

	if (id && secret && refresh) {
		snprintf(drive.creds.client_id, sizeof(drive.creds.client_id), "%s", id);
		snprintf(drive.creds.client_secret, sizeof(drive.creds.client_secret), "%s", secret);
		snprintf(drive.creds.refresh_token, sizeof(drive.creds.refresh_token), "%s", refresh);
		drive.usable = true;
	}

	/* Lengths only. The values are the console's keys to a Google account, and a log
	 * on a card anyone can read is the last place they should end up. */
	printf("[drive] config %s: id=%zu rahasia=%zu refresh=%zu\n",
			drive.usable ? "terpakai" : "tidak lengkap",
			id ? strlen(id) : 0, secret ? strlen(secret) : 0, refresh ? strlen(refresh) : 0);

	json_object_put(root);
}

bool drive_available(void) {
	read_config();
	return drive.usable;
}

/* ------------------------------------------------------------------ token */

static bool append_field(char *body, size_t *len, size_t cap,
		const char *key, const char *value) {
	char *encoded = http_escape(value);
	if (!encoded) return false;

	int written = snprintf(body + *len, cap - *len, "%s%s=%s", *len ? "&" : "", key, encoded);
	http_free_escaped(encoded);

	if (written < 0 || (size_t)written >= cap - *len) return false;
	*len += (size_t)written;
	return true;
}

/** Trades the refresh token for an access token, or reuses one still in hand. */
static const char *access_token(char *err, size_t err_len) {
	read_config();
	if (!drive.usable) {
		set_err(err, err_len, "nxdrive belum disetel di konsol ini");
		return NULL;
	}

	if (drive.token[0] && time(NULL) < drive.token_until) return drive.token;

	printf("[drive] menukar refresh token\n");

	char body[1600];
	size_t len = 0;

	if (!append_field(body, &len, sizeof(body), "client_id", drive.creds.client_id) ||
			!append_field(body, &len, sizeof(body), "client_secret", drive.creds.client_secret) ||
			!append_field(body, &len, sizeof(body), "refresh_token", drive.creds.refresh_token) ||
			!append_field(body, &len, sizeof(body), "grant_type", "refresh_token")) {
		set_err(err, err_len, "gagal menyusun permintaan token");
		return NULL;
	}

	HttpBuffer response = {0};
	if (!http_post_form(TOKEN_ENDPOINT, body, &response, err, err_len)) {
		printf("[drive] permintaan token gagal: %s\n", err);
		return NULL;
	}

	json_object *root = json_tokener_parse(response.data ? response.data : "");
	http_buffer_free(&response);

	if (!root) {
		set_err(err, err_len, "respons token bukan JSON");
		return NULL;
	}

	const char *token = string_field(root, "access_token");
	if (!token) {
		/* Google says why in the body, and "invalid_grant" -- a revoked token -- is
		 * the one failure the user has to act on rather than retry. */
		const char *reason = string_field(root, "error");
		set_err(err, err_len, "token ditolak%s%s", reason ? ": " : "", reason ? reason : "");
		printf("[drive] %s\n", err);
		json_object_put(root);
		return NULL;
	}

	snprintf(drive.token, sizeof(drive.token), "%s", token);
	drive.token_until = time(NULL) + TOKEN_LIFETIME_SECONDS - TOKEN_MARGIN_SECONDS;
	printf("[drive] token didapat, %zu karakter\n", strlen(drive.token));

	json_object_put(root);
	return drive.token;
}

/* ---------------------------------------------------------------- listing */

void drive_listing_free(DriveListing *listing) {
	for (size_t i = 0; i < listing->count; i++) {
		free(listing->items[i].id);
		free(listing->items[i].name);
	}
	free(listing->items);
	*listing = (DriveListing){0};
}

static bool push(DriveListing *listing, size_t *capacity, DriveEntry entry) {
	if (listing->count == *capacity) {
		size_t grown = *capacity ? *capacity * 2 : 32;
		DriveEntry *items = realloc(listing->items, grown * sizeof(DriveEntry));
		if (!items) return false;
		listing->items = items;
		*capacity = grown;
	}

	listing->items[listing->count++] = entry;
	return true;
}

/*
 * Whether this is worth offering. A Drive folder holds documents and photos as
 * well, and a list that shows them is a file manager rather than somewhere to
 * find something to watch -- the same reason the card listing hides saves.
 */
static bool playable_mime(const char *mime) {
	return strncmp(mime, "video/", 6) == 0 || strncmp(mime, "audio/", 6) == 0;
}

static bool parse_page(json_object *root, DriveListing *out, size_t *capacity,
		char **next_token) {
	*next_token = NULL;

	const char *token = string_field(root, "nextPageToken");
	if (token) *next_token = strdup(token);

	json_object *files = NULL;
	if (!json_object_object_get_ex(root, "files", &files) ||
			!json_object_is_type(files, json_type_array)) {
		return true;
	}

	size_t total = json_object_array_length(files);
	for (size_t i = 0; i < total; i++) {
		json_object *node = json_object_array_get_idx(files, i);

		const char *id = string_field(node, "id");
		const char *name = string_field(node, "name");
		if (!id || !name) continue;

		json_object *mime_obj = NULL;
		json_object_object_get_ex(node, "mimeType", &mime_obj);
		const char *mime = mime_obj ? json_object_get_string(mime_obj) : "";

		bool is_folder = strcmp(mime, FOLDER_MIME) == 0;
		if (!is_folder && !playable_mime(mime)) continue;

		json_object *size = NULL;
		json_object_object_get_ex(node, "size", &size);

		DriveEntry entry = {
			.id = strdup(id),
			.name = strdup(name),
			.is_folder = is_folder,
			.is_video = strncmp(mime, "video/", 6) == 0,
			.size = size ? json_object_get_int64(size) : -1,
		};

		if (!entry.id || !entry.name || !push(out, capacity, entry)) {
			free(entry.id);
			free(entry.name);
			return false;
		}
	}

	return true;
}

/*
 * Runs one Drive query to exhaustion, following every page.
 *
 * `order` is handed to Drive rather than chosen here because sorting a folder and
 * sorting a search want opposite things: a folder reads best with its subfolders
 * gathered at the top, while a search is only ever asking "which one did I mean",
 * and the newest match is almost always it.
 */
static bool run_query(const char *query, const char *order, DriveListing *out,
		char *err, size_t err_len) {
	*out = (DriveListing){0};

	const char *token = access_token(err, err_len);
	if (!token) return false;

	char *escaped_query = http_escape(query);
	if (!escaped_query) {
		set_err(err, err_len, "kehabisan memori");
		return false;
	}

	printf("[drive] q=%s order=%s\n", query, order);

	size_t capacity = 0;
	char *page = NULL;
	bool ok = true;

	do {
		char url[1024];
		char *escaped_page = page ? http_escape(page) : NULL;

		snprintf(url, sizeof(url),
				FILES_ENDPOINT "?q=%s&fields=%s&pageSize=%d&orderBy=%s"
				ALL_DRIVES "%s%s",
				escaped_query, LIST_FIELDS, PAGE_SIZE, order,
				escaped_page ? "&pageToken=" : "", escaped_page ? escaped_page : "");

		if (escaped_page) http_free_escaped(escaped_page);
		free(page);
		page = NULL;

		HttpBuffer response = {0};
		if (!http_get(url, token, &response, err, err_len)) {
			printf("[drive] daftar gagal: %s\n", err);
			ok = false;
			break;
		}

		json_object *root = json_tokener_parse(response.data ? response.data : "");
		http_buffer_free(&response);

		if (!root) {
			set_err(err, err_len, "daftar Drive bukan JSON");
			ok = false;
			break;
		}

		ok = parse_page(root, out, &capacity, &page);
		json_object_put(root);
	} while (ok && page);

	free(page);
	http_free_escaped(escaped_query);

	if (ok) printf("[drive] %zu entri\n", out->count);

	if (!ok) drive_listing_free(out);
	return ok;
}

bool drive_list(const char *folder_id, DriveListing *out, char *err, size_t err_len) {
	char query[256];
	snprintf(query, sizeof(query), "'%s' in parents and trashed = false",
			folder_id && folder_id[0] ? folder_id : DRIVE_ROOT);

	return run_query(query, "folder,name", out, err, err_len);
}

/*
 * Puts `text` inside a Drive query string literal.
 *
 * Drive's query language delimits values with single quotes and escapes with a
 * backslash, so both characters have to be doubled out of the way. A title with
 * an apostrophe in it -- which is most of them -- would otherwise end the literal
 * early and the whole query would be rejected as malformed.
 */
static void quote_into(const char *text, char *out, size_t out_len) {
	size_t at = 0;

	for (size_t i = 0; text[i] && at + 2 < out_len; i++) {
		if (text[i] == '\'' || text[i] == '\\') out[at++] = '\\';
		out[at++] = text[i];
	}
	out[at] = '\0';
}

bool drive_search(const char *text, DriveListing *out, char *err, size_t err_len) {
	char safe[DRIVE_SEARCH_MAX * 2];
	quote_into(text, safe, sizeof(safe));

	char query[sizeof(safe) + 64];
	snprintf(query, sizeof(query), "name contains '%s' and trashed = false", safe);

	/*
	 * Newest first, which is what someone looking for a thing they put there
	 * recently actually wants. Drive has no relevance order to ask for.
	 *
	 * The space is written %20 because this goes into the URL as it stands. A raw
	 * space ends the request line early, and the server answers 400 with an HTML
	 * page rather than the API's own JSON error -- which is what makes the mistake
	 * look like a rejected query instead of a malformed request.
	 */
	return run_query(query, "modifiedTime%20desc", out, err, err_len);
}

/* ----------------------------------------------------------------- stream */

bool drive_stream(const char *file_id, char *url_out, size_t url_len,
		char *token_out, size_t token_len, char *err, size_t err_len) {
	const char *token = access_token(err, err_len);
	if (!token) return false;

	char *escaped = http_escape(file_id);
	if (!escaped) {
		set_err(err, err_len, "kehabisan memori");
		return false;
	}

	int written = snprintf(url_out, url_len, FILES_ENDPOINT "/%s?alt=media" ALL_DRIVES, escaped);
	http_free_escaped(escaped);

	if (written < 0 || (size_t)written >= url_len) {
		set_err(err, err_len, "alamat file terlalu panjang");
		return false;
	}

	printf("[drive] stream %s\n", url_out);

	snprintf(token_out, token_len, "%s", token);
	return true;
}
