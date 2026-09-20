#include "m3u.h"

#include <stdlib.h>
#include <string.h>

#include "util.h"

#define EXTINF "#EXTINF:"
#define VLCOPT_REFERRER "#EXTVLCOPT:http-referrer="
#define VLCOPT_AGENT "#EXTVLCOPT:http-user-agent="

static char *dup_range(const char *start, size_t len) {
	char *copy = malloc(len + 1);
	if (!copy) return NULL;

	memcpy(copy, start, len);
	copy[len] = '\0';
	return copy;
}

static void free_entry(M3uEntry *entry) {
	free(entry->name);
	free(entry->url);
	free(entry->group);
	free(entry->referer);
	free(entry->user_agent);
	*entry = (M3uEntry){0};
}

void m3u_free(M3uPlaylist *list) {
	for (size_t i = 0; i < list->count; i++) free_entry(&list->items[i]);
	free(list->items);
	*list = (M3uPlaylist){0};
}

/** Value of a key="value" attribute on an #EXTINF line, or NULL when it has none. */
static char *attribute(const char *line, size_t len, const char *key) {
	size_t key_len = strlen(key);

	for (size_t i = 0; i + key_len + 2 <= len; i++) {
		if (memcmp(line + i, key, key_len) != 0) continue;
		if (line[i + key_len] != '=' || line[i + key_len + 1] != '"') continue;

		const char *value = line + i + key_len + 2;
		size_t left = len - (size_t)(value - line);
		const char *end = memchr(value, '"', left);
		return end ? dup_range(value, (size_t)(end - value)) : NULL;
	}

	return NULL;
}

/*
 * The channel name is whatever follows the attributes. Every attribute value is
 * quoted, so the first comma outside quotes is the one that ends them -- taking the
 * last one instead would cut a name like "Metro TV, Jakarta" in half.
 */
static char *display_name(const char *line, size_t len) {
	bool quoted = false;
	size_t comma = len;

	for (size_t i = 0; i < len && comma == len; i++) {
		if (line[i] == '"') quoted = !quoted;
		else if (line[i] == ',' && !quoted) comma = i;
	}
	if (comma == len) return NULL;

	const char *name = line + comma + 1;
	size_t name_len = len - comma - 1;

	while (name_len && *name == ' ') { name++; name_len--; }
	while (name_len && name[name_len - 1] == ' ') name_len--;

	return name_len ? dup_range(name, name_len) : NULL;
}

static bool push(M3uPlaylist *list, size_t *capacity, M3uEntry entry) {
	if (list->count == *capacity) {
		size_t grown = *capacity ? *capacity * 2 : 64;
		M3uEntry *items = realloc(list->items, grown * sizeof(M3uEntry));
		if (!items) return false;
		list->items = items;
		*capacity = grown;
	}

	list->items[list->count++] = entry;
	return true;
}

bool m3u_parse(const char *text, M3uPlaylist *out, char *err, size_t err_len) {
	*out = (M3uPlaylist){0};

	M3uEntry pending = {0};
	size_t capacity = 0;
	bool ok = true;

	for (const char *cursor = text; ok && *cursor;) {
		const char *line = cursor;
		const char *newline = strchr(cursor, '\n');
		size_t len = newline ? (size_t)(newline - cursor) : strlen(cursor);
		cursor = newline ? newline + 1 : line + len;

		/* Playlists are served with whatever line endings the host felt like. */
		while (len && (line[len - 1] == '\r' || line[len - 1] == ' ')) len--;
		while (len && *line == ' ') { line++; len--; }
		if (len == 0) continue;

		if (len > sizeof(EXTINF) - 1 && memcmp(line, EXTINF, sizeof(EXTINF) - 1) == 0) {
			/* Two #EXTINF lines in a row means the first named nothing. */
			free_entry(&pending);
			pending.name = display_name(line, len);
			pending.group = attribute(line, len, "group-title");
			pending.referer = attribute(line, len, "http-referrer");
			pending.user_agent = attribute(line, len, "user-agent");
			continue;
		}

		if (len > sizeof(VLCOPT_REFERRER) - 1 &&
				memcmp(line, VLCOPT_REFERRER, sizeof(VLCOPT_REFERRER) - 1) == 0) {
			free(pending.referer);
			pending.referer = dup_range(line + sizeof(VLCOPT_REFERRER) - 1,
					len - (sizeof(VLCOPT_REFERRER) - 1));
			continue;
		}

		if (len > sizeof(VLCOPT_AGENT) - 1 &&
				memcmp(line, VLCOPT_AGENT, sizeof(VLCOPT_AGENT) - 1) == 0) {
			free(pending.user_agent);
			pending.user_agent = dup_range(line + sizeof(VLCOPT_AGENT) - 1,
					len - (sizeof(VLCOPT_AGENT) - 1));
			continue;
		}

		if (line[0] == '#') continue;

		/* A URL with no #EXTINF above it has nothing to label the row with, so it is
		 * dropped rather than listed blank. */
		if (!pending.name) continue;

		pending.url = dup_range(line, len);
		ok = pending.url && push(out, &capacity, pending);
		if (!ok) free_entry(&pending);
		pending = (M3uEntry){0};
	}

	free_entry(&pending);

	if (!ok) {
		m3u_free(out);
		set_err(err, err_len, "kehabisan memori membaca daftar saluran");
		return false;
	}

	if (out->count == 0) {
		set_err(err, err_len, "daftar saluran kosong");
		return false;
	}

	return true;
}
