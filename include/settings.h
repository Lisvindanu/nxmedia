#pragma once

#include <stdbool.h>
#include <stddef.h>

/*
 * The handful of things that belong to this console rather than to the build.
 *
 * Read once at startup from a file on the card. Everything here has a working
 * default, so a console with no file at all still runs -- the file exists to point
 * the app at a different server, not to make it work in the first place.
 */

#define SETTINGS_PATH "sdmc:/switch/nxmedia/nxmedia.json"
#define SETTINGS_URL_MAX 256

typedef struct {
	/** Base of the MediaVault public API, without a trailing slash. */
	char mediavault_url[SETTINGS_URL_MAX];
} Settings;

/** Fills out with the file's values, or with defaults where it is silent. */
void settings_load(Settings *out);
