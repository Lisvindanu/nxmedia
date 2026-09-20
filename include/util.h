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
