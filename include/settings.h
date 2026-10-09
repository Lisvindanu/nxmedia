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
	/**
	 * Base of the edge search service, without a trailing slash.
	 *
	 * Searching and single-video lookups live out here. Downloading may not: a
	 * progressive googlevideo URL is tied to the address that extracted it and
	 * answers 403 from anywhere else, so whichever machine resolves one has to be the
	 * one that serves its bytes.
	 *
	 * That turned out to be true only of the progressive route. An HLS manifest is
	 * not bound to its extractor, so watching no longer passes through our server at
	 * all -- the segments come to the console straight from YouTube, and the server
	 * hands over nothing but the manifest.
	 */
	char mediavault_edge_url[SETTINGS_URL_MAX];

	/**
	 * Ask the server for an HLS manifest instead of the 360p proxy.
	 *
	 * Off by default, and that is a measurement rather than a preference. The
	 * manifest answers in half a second from a desktop and gives 720p without any
	 * merging, which is why it was tried -- but on the console it did not finish
	 * opening at all: two minutes in, still connecting. The proxy route opens in
	 * one to three seconds there, every time.
	 *
	 * Left in rather than deleted because the fault is not understood yet, and a
	 * switch in the settings file is how it gets tested again without a rebuild and
	 * without costing anybody else a hung player in the meantime.
	 */
	bool prefer_hls;
} Settings;

/** Fills out with the file's values, or with defaults where it is silent. */
void settings_load(Settings *out);
