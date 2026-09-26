#include "util.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

bool mkdir_p(const char *path) {
	char work[512];
	snprintf(work, sizeof(work), "%s", path);

	/* Skip the "sdmc:" device prefix so it is never treated as a directory. */
	char *cursor = strchr(work, ':');
	cursor = cursor ? cursor + 1 : work;
	if (*cursor == '/') cursor++;

	for (; *cursor; cursor++) {
		if (*cursor != '/') continue;
		*cursor = '\0';
		if (mkdir(work, 0777) != 0 && errno != EEXIST) return false;
		*cursor = '/';
	}

	return mkdir(work, 0777) == 0 || errno == EEXIST;
}

/** Reads up to three dot-separated numbers, tolerating a "v" prefix and any suffix. */
static bool parse_version(const char *text, int out[3]) {
	out[0] = out[1] = out[2] = 0;
	if (!text) return false;

	while (*text == 'v' || *text == 'V') text++;

	for (int i = 0; i < 3; i++) {
		if (*text < '0' || *text > '9') return false;

		char *end = NULL;
		long value = strtol(text, &end, 10);
		if (end == text) return false;

		out[i] = (int)value;
		if (*end != '.') break;
		text = end + 1;
	}
	return true;
}

bool version_is_newer(const char *candidate, const char *current) {
	int a[3], b[3];
	if (!parse_version(candidate, a) || !parse_version(current, b)) return false;

	for (int i = 0; i < 3; i++) {
		if (a[i] != b[i]) return a[i] > b[i];
	}
	return false;
}

void set_err(char *err, size_t err_len, const char *fmt, ...) {
	if (!err || err_len == 0) return;

	va_list args;
	va_start(args, fmt);
	vsnprintf(err, err_len, fmt, args);
	va_end(args);
}

/** How many bytes the sequence starting at this lead byte should be, 0 if it is not a lead. */
static size_t utf8_sequence_len(unsigned char lead) {
	if (lead < 0x80) return 1;
	if ((lead & 0xE0) == 0xC0) return 2;
	if ((lead & 0xF0) == 0xE0) return 3;
	if ((lead & 0xF8) == 0xF0) return 4;
	return 0;
}

void sanitize_filename(const char *name, char *out, size_t out_len) {
	size_t written = 0;

	for (const char *c = name; c && *c; ) {
		unsigned char ch = (unsigned char)*c;

		if (ch < 0x20 || strchr("\\/:*?\"<>|", ch) != NULL) {
			if (written + 1 >= out_len) break;
			out[written++] = '_';
			c++;
			continue;
		}

		size_t len = utf8_sequence_len(ch);

		/* A sequence the name ends in the middle of, and a continuation byte with no
		 * lead in front of it, are both dropped along with everything after: there is
		 * no character there to keep. */
		for (size_t i = 1; i < len; i++) {
			if (((unsigned char)c[i] & 0xC0) != 0x80) {
				len = 0;
				break;
			}
		}
		if (len == 0 || written + len >= out_len) break;

		memcpy(out + written, c, len);
		written += len;
		c += len;
	}

	while (written > 0 && (out[written - 1] == '.' || out[written - 1] == ' ')) written--;
	out[written] = '\0';

	if (written == 0) snprintf(out, out_len, "download");
}
