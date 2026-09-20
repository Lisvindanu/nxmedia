#pragma once

#include <stdbool.h>
#include <stddef.h>

/*
 * Every string the interface shows, in both languages. English is the default
 * because that is what a stranger downloading a Switch homebrew will read; the
 * choice is remembered on the SD card so it is only ever made once.
 *
 * Diagnostics from curl, SDL and mpg123 are deliberately left alone: they carry
 * library names and are meant to be searched for, not read.
 */

typedef enum {
	LANG_EN,
	LANG_ID,
	LANG_COUNT,
} Lang;

typedef enum {
	STR_RADIO,
	STR_RADIO_BLURB,
	STR_MUSIC,
	STR_MUSIC_BLURB,
	STR_VIDEO,
	STR_VIDEO_BLURB,
	STR_SETTINGS,
	STR_SETTINGS_BLURB,

	STR_HOME_EYEBROW,
	STR_RADIO_EYEBROW,
	STR_SETTINGS_EYEBROW,
	STR_VIDEO_EYEBROW,
	STR_MUSIC_EYEBROW,
	STR_LIVE_EYEBROW,
	STR_SOON,

	STR_OPEN,
	STR_LISTEN,
	STR_PLAY,
	STR_WATCH,
	STR_PAUSE,
	STR_RESUME,
	STR_UP,
	STR_BACK,
	STR_REFRESH,
	STR_STOP,
	STR_SCREEN_OFF,
	STR_HOME,
	STR_QUIT,
	STR_SWITCH,

	STR_KEY_STICK,
	STR_KEY_TOUCH,
	STR_KEY_ZOOM,
	STR_KEY_DPAD,
	STR_DO_SPIN,
	STR_DO_PICK,
	STR_DO_ZOOM,
	STR_DO_STATION,

	STR_PICK_COUNTRY,
	STR_RADIO_PROMPT,
	STR_SCREEN_OFF_NOTE,
	STR_LOADING_STATIONS,
	STR_NO_STATIONS,
	STR_READY,
	STR_CONNECTING,
	STR_PLAYING,

	STR_BROWSE_HINT,
	STR_NO_MEDIA,
	STR_KIND_FOLDER,
	STR_KIND_VIDEO,
	STR_KIND_AUDIO,
	STR_KIND_LIVE,
	STR_KIND_SD,

	STR_PICK_SOURCE,
	STR_WATCH_LIVE,
	STR_WATCH_LIVE_NOTE,
	STR_WATCH_CARD,
	STR_WATCH_CARD_NOTE,
	STR_LIVE_HINT,
	STR_LOADING_CHANNELS,
	STR_NO_CHANNELS,

	STR_LANGUAGE,
	STR_SETTINGS_HINT,

	STR_VERSION,
	STR_UPDATE,
	STR_CHECK_UPDATE,
	STR_CHECKING,
	STR_UP_TO_DATE,
	STR_UPDATE_READY,
	STR_DOWNLOADING,
	STR_UPDATE_DONE,
	STR_INSTALL,

	STR_LOADING_MAP,
	STR_ROMFS_FAIL,
	STR_MAP_FAIL,
	STR_NET_FAIL,
	STR_NET_FAIL_WHY,
	STR_AUDIO_FAIL,

	STR_COUNT,
} StringId;

/** Reads the remembered choice. Safe to call before anything else is up. */
void lang_load(void);

/** Switches language and writes the choice back to the SD card. */
void lang_set(Lang lang);

Lang lang_get(void);

/** Name of a language in its own language, for the picker. */
const char *lang_name(Lang lang);

const char *T(StringId id);
