#include "library.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "util.h"

/* What the bundled ffmpeg was built to demux and decode -- see tools/build-ffmpeg.sh.
 * Matching on the extension rather than probing the file keeps opening a folder of a
 * few hundred entries instant. */
static const char *const VIDEO_EXTENSIONS[] = {
	".mp4", ".mkv", ".webm", ".avi", ".mov", ".m4v", ".ts", ".flv", ".wmv", ".mpg", ".mpeg",
};

static const char *const AUDIO_EXTENSIONS[] = {
	".mp3", ".m4a", ".aac", ".flac", ".ogg", ".opus", ".wav", ".wma", ".aif", ".aiff",
};

static bool has_extension(const char *name, const char *const *list, size_t count) {
	const char *dot = strrchr(name, '.');
	if (!dot) return false;

	for (size_t i = 0; i < count; i++) {
		if (strcasecmp(dot, list[i]) == 0) return true;
	}
	return false;
}

bool library_join(const char *dir, const char *name, char *out, size_t out_len) {
	size_t dir_len = strlen(dir);
	/* The root already ends in a slash, and "sdmc://switch" is not the same path on
	 * every filesystem the card may be read by. */
	const char *separator = (dir_len > 0 && dir[dir_len - 1] == '/') ? "" : "/";

	int written = snprintf(out, out_len, "%s%s%s", dir, separator, name);
	return written >= 0 && (size_t)written < out_len;
}

bool library_parent(char *dir) {
	size_t root_len = strlen(LIBRARY_ROOT);
	if (strlen(dir) <= root_len) return false;

	char *slash = strrchr(dir, '/');
	if (!slash) return false;

	/* The root's own slash has to survive, or walking up out of "sdmc:/switch" would
	 * leave a bare "sdmc:" that opendir cannot use. */
	size_t cut = (size_t)(slash - dir);
	dir[cut < root_len ? root_len : cut] = '\0';
	return true;
}

void library_listing_free(LibraryListing *listing) {
	for (size_t i = 0; i < listing->count; i++) free(listing->items[i].name);
	free(listing->items);
	*listing = (LibraryListing){0};
}

static bool listing_push(LibraryListing *listing, size_t *capacity, LibraryEntry entry) {
	if (listing->count == *capacity) {
		size_t grown = *capacity ? *capacity * 2 : 32;
		LibraryEntry *items = realloc(listing->items, grown * sizeof(LibraryEntry));
		if (!items) return false;
		listing->items = items;
		*capacity = grown;
	}

	listing->items[listing->count++] = entry;
	return true;
}

static int compare_entries(const void *lhs, const void *rhs) {
	const LibraryEntry *a = lhs;
	const LibraryEntry *b = rhs;

	if (a->is_dir != b->is_dir) return a->is_dir ? -1 : 1;
	return strcasecmp(a->name, b->name);
}

bool library_list(const char *dir, LibraryKind kind, LibraryListing *out,
		char *err, size_t err_len) {
	*out = (LibraryListing){0};

	const char *const *wanted = kind == LibraryKind_Video ? VIDEO_EXTENSIONS : AUDIO_EXTENSIONS;
	size_t wanted_count = kind == LibraryKind_Video
			? sizeof(VIDEO_EXTENSIONS) / sizeof(*VIDEO_EXTENSIONS)
			: sizeof(AUDIO_EXTENSIONS) / sizeof(*AUDIO_EXTENSIONS);

	DIR *handle = opendir(dir);
	if (!handle) {
		set_err(err, err_len, "tidak bisa membuka %s", dir);
		return false;
	}

	size_t capacity = 0;
	bool ok = true;

	for (struct dirent *entry; ok && (entry = readdir(handle)) != NULL;) {
		if (entry->d_name[0] == '.') continue;

		bool playable = has_extension(entry->d_name, wanted, wanted_count);

		char path[LIBRARY_PATH_MAX];
		if (!library_join(dir, entry->d_name, path, sizeof(path))) continue;

		struct stat info;
		if (stat(path, &info) != 0) continue;

		bool is_dir = S_ISDIR(info.st_mode);
		if (!is_dir && !playable) continue;

		LibraryEntry item = {
			.name = strdup(entry->d_name),
			.size = is_dir ? -1 : (int64_t)info.st_size,
			.is_dir = is_dir,
		};

		ok = item.name && listing_push(out, &capacity, item);
		if (!ok) free(item.name);
	}

	closedir(handle);

	if (!ok) {
		library_listing_free(out);
		set_err(err, err_len, "kehabisan memori membaca folder");
		return false;
	}

	qsort(out->items, out->count, sizeof(LibraryEntry), compare_entries);
	return true;
}
