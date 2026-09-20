#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "http.h"

#define UPDATE_TAG_LEN 32
#define UPDATE_URL_LEN 512

typedef struct {
	bool available;
	char latest[UPDATE_TAG_LEN];
	char asset_url[UPDATE_URL_LEN];
	int64_t asset_size;
} UpdateInfo;

/**
 * Records where this build is running from, which is where a new build has to be
 * written back. Pass argv[0]; an empty or missing value falls back to the stock
 * install path.
 */
void update_init(const char *self_path);

/** The version this binary was compiled with, without a leading "v". */
const char *update_version(void);

/**
 * Asks the public releases repo what the newest tag is. Sets out->available only
 * when that tag parses as a higher version than this build, so a release with a
 * malformed tag is ignored rather than triggering a pointless download.
 */
bool update_check(UpdateInfo *out, char *err, size_t err_len);

/**
 * Downloads the release asset, checks it is really an NRO, then swaps it over this
 * binary keeping the old one until the swap has succeeded. On success the new build
 * is queued to launch when the app exits.
 */
bool update_apply(const UpdateInfo *info, HttpProgress on_progress, void *user,
		char *err, size_t err_len);
