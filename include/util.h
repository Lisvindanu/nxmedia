#pragma once

#include <stdbool.h>
#include <stddef.h>

/** Creates a directory and any missing parent, treating an existing one as success. */
bool mkdir_p(const char *path);

/**
 * True when `candidate` names a higher version than `current`. Either may carry a
 * "v" prefix. A string that does not start with a number is not a version and is
 * reported as not newer, so a stray tag cannot trigger an update.
 */
bool version_is_newer(const char *candidate, const char *current);

/** Writes a formatted message into an optional caller-provided error buffer. */
void set_err(char *err, size_t err_len, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

/**
 * Turns a name from somewhere else into one FAT can hold: path separators and the
 * characters Windows forbids become underscores, control bytes too, and trailing dots
 * and spaces are dropped. Falls back to "download" when nothing survives.
 *
 * Truncation to out_len happens at whole UTF-8 characters. Half a multi-byte sequence
 * is not a character, and Horizon rejects a name ending in one -- which is how a long
 * Japanese title disappeared entirely instead of merely arriving shortened.
 */
void sanitize_filename(const char *name, char *out, size_t out_len);
