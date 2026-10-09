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
	for (size_t i = 0; i < listing->count; i++) {
		free(listing->items[i].name);
		free(listing->items[i].dir);
	}
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

/* ------------------------------------------------------------------ search */

/*
 * Folders a media search has no business entering.
 *
 * Games and the emuMMC are the reason this list exists at all: the emuMMC on a
 * card like this one holds a few hundred thousand files, none of them playable,
 * and FAT32 has no index to ask -- every one would have to be read.
 */
static const char *const SKIP_DIRS[] = {
	"Nintendo", "emuMMC", "atmosphere", "bootloader", "warmboot_mariko",
	"System Volume Information",
};

/* Deep enough for anything anybody files by hand, shallow enough that a loop of
 * links or a pathological tree cannot run away with the frame loop stopped. */
#define SEARCH_MAX_DEPTH 10
#define SEARCH_MAX_HITS 300
/*
 * A ceiling on folders opened, not on files seen. Without one, a search is as slow
 * as the worst card anybody owns; with it, the search gives up and says so, which
 * is a far better outcome than a console that looks dead.
 */
#define SEARCH_MAX_DIRS 4000

static bool skipped(const char *name) {
	for (size_t i = 0; i < sizeof(SKIP_DIRS) / sizeof(*SKIP_DIRS); i++) {
		if (strcasecmp(name, SKIP_DIRS[i]) == 0) return true;
	}
	return false;
}

/* strcasestr is not in every libc this is built against, and the haystacks here are
 * file names rather than documents, so the obvious loop is fast enough. */
static bool contains_nocase(const char *haystack, const char *needle) {
	size_t n = strlen(needle);
	if (n == 0) return true;

	for (; *haystack; haystack++) {
		if (strncasecmp(haystack, needle, n) == 0) return true;
	}
	return false;
}

/* Set when the walk was stopped early, so the pane can say so instead of claiming
 * the card holds nothing else. */
typedef struct {
	const char *text;
	const char *const *wanted;
	size_t wanted_count;
	LibrarySearchProgress on_progress;
	void *user;
	size_t dirs_seen;
	bool truncated;
	bool abandoned;
} Walk;

static bool search_dir(Walk *walk, const char *dir, LibraryListing *out,
		size_t *capacity, int depth) {
	if (depth > SEARCH_MAX_DEPTH) return true;

	if (out->count >= SEARCH_MAX_HITS || walk->dirs_seen >= SEARCH_MAX_DIRS) {
		walk->truncated = true;
		return true;
	}

	walk->dirs_seen++;

	if (walk->on_progress &&
			!walk->on_progress(walk->user, walk->dirs_seen, out->count)) {
		walk->abandoned = true;
		return true;
	}

	DIR *handle = opendir(dir);
	/* A folder that cannot be opened is not a failure of the search: cards carry
	 * directories Horizon will not hand over, and the rest still has to be read. */
	if (!handle) return true;

	bool ok = true;

	for (struct dirent *entry; ok && (entry = readdir(handle)) != NULL;) {
		if (entry->d_name[0] == '.') continue;
		if (skipped(entry->d_name)) continue;

		/*
		 * d_type first, and the name tested before anything is stat()ed.
		 *
		 * stat is a syscall apiece, and on FAT32 through Horizon it is the whole cost
		 * of the walk: a card with tens of thousands of files spends all its time
		 * there and none of it matching. Almost every file fails on its extension,
		 * which costs nothing, so only the handful that could be results are ever
		 * asked about.
		 */
		bool is_dir;
		if (entry->d_type == DT_DIR) {
			is_dir = true;
		} else if (entry->d_type == DT_REG) {
			is_dir = false;
		} else {
			/* Horizon normally fills d_type in; this is for the filesystems that
			 * do not, where there is no way to tell but to ask. */
			char probe[LIBRARY_PATH_MAX];
			struct stat info;
			if (!library_join(dir, entry->d_name, probe, sizeof(probe))) continue;
			if (stat(probe, &info) != 0) continue;
			is_dir = S_ISDIR(info.st_mode);
		}

		if (is_dir) {
			char path[LIBRARY_PATH_MAX];
			if (!library_join(dir, entry->d_name, path, sizeof(path))) continue;

			ok = search_dir(walk, path, out, capacity, depth + 1);
			if (walk->abandoned) break;
			continue;
		}

		if (!has_extension(entry->d_name, walk->wanted, walk->wanted_count)) continue;
		if (!contains_nocase(entry->d_name, walk->text)) continue;

		char path[LIBRARY_PATH_MAX];
		struct stat info;
		int64_t size = -1;
		if (library_join(dir, entry->d_name, path, sizeof(path)) && stat(path, &info) == 0) {
			size = (int64_t)info.st_size;
		}

		LibraryEntry item = {
			.name = strdup(entry->d_name),
			.dir = strdup(dir),
			.size = size,
			.is_dir = false,
		};

		ok = item.name && item.dir && listing_push(out, capacity, item);
		if (!ok) {
			free(item.name);
			free(item.dir);
		}

		if (out->count >= SEARCH_MAX_HITS) {
			walk->truncated = true;
			break;
		}
	}

	closedir(handle);
	return ok;
}

bool library_search(const char *text, LibraryKind kind, LibraryListing *out,
		LibrarySearchProgress on_progress, void *user, bool *partial,
		char *err, size_t err_len) {
	*out = (LibraryListing){0};
	if (partial) *partial = false;

	Walk walk = {
		.text = text,
		.wanted = kind == LibraryKind_Video ? VIDEO_EXTENSIONS : AUDIO_EXTENSIONS,
		.wanted_count = kind == LibraryKind_Video
				? sizeof(VIDEO_EXTENSIONS) / sizeof(*VIDEO_EXTENSIONS)
				: sizeof(AUDIO_EXTENSIONS) / sizeof(*AUDIO_EXTENSIONS),
		.on_progress = on_progress,
		.user = user,
	};

	size_t capacity = 0;

	if (!search_dir(&walk, LIBRARY_ROOT, out, &capacity, 0)) {
		library_listing_free(out);
		set_err(err, err_len, "kehabisan memori mencari");
		return false;
	}

	printf("[card] cari \"%s\": %zu hasil, %zu folder%s%s\n", text, out->count,
			walk.dirs_seen, walk.truncated ? ", terpotong" : "",
			walk.abandoned ? ", dibatalkan" : "");

	/* Abandoning on purpose is not the same as running into a limit: the user who
	 * pressed B already knows why the list is short. */
	if (partial) *partial = walk.truncated;

	/* Sorted by name rather than by where they were found: which folder something
	 * sits in is exactly what the person searching has forgotten. */
	qsort(out->items, out->count, sizeof(LibraryEntry), compare_entries);
	return true;
}
