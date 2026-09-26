#pragma once

#include <stdbool.h>
#include <stddef.h>

/**
 * Shows the system keyboard and writes what was typed into out.
 *
 * Returns false when the user cancels, leaves it empty, or the keyboard cannot be
 * opened -- in every one of those cases the caller has nothing to act on.
 */
bool keyboard_prompt(const char *header, const char *initial, char *out, size_t out_len);
