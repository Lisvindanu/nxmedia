#include "pane_youtube.h"

#include <stdio.h>
#include <string.h>

#include <switch.h>

#include "i18n.h"
#include "keyboard.h"
#include "mediavault.h"
#include "player.h"
#include "settings.h"
#include "shelf.h"
#include "stage.h"
#include "ui.h"
#include "util.h"

/*
 * YouTube by way of the MediaVault server: what is popular now, whatever you
 * search for, and the two lists the card remembers by itself.
 *
 * The pane fills itself on the way in rather than waiting to be asked. Opening to
 * a blank screen and a keyboard would make the visitor think of something before
 * they have seen anything at all.
 *
 * Watching and listening are separate buttons because the server offers two
 * genuinely different routes, at two very different costs. Audio is proxied and
 * starts in a few seconds, which is what makes this usable as a music player.
 * Video has to be merged first, half a minute of it, so asking for a picture is a
 * decision rather than the default -- and the merged file is streamed rather than
 * kept, since it is seekable over HTTP and the card need not be involved.
 */

#define SAVE_DIR "sdmc:/nxmedia"
#define NOTE_MAX 192
/* Comfortably past MEDIA_SEARCH_MAX, which is what the longest label can be. */
#define LABEL_MAX 192

/* Where the cards on screen come from. Browse holds whatever the server last
 * answered with; the other two answer from the card. */
typedef enum {
	SRC_BROWSE,
	SRC_HISTORY,
	SRC_FAVOURITES,
	SRC_COUNT,
} Source;

/* Every action the pane has, all of them named. A button nobody can see is a
 * button nobody presses: leaving save off this bar is how it came to look as
 * though the feature had never been built. */
static UiHint HINTS[] = {
	{ "A", NULL, HidNpadButton_A, true },
	{ "X", NULL, HidNpadButton_X, false },
	{ "ZL", NULL, HidNpadButton_ZL, false },
	{ "ZR", NULL, HidNpadButton_ZR, false },
	{ "LR", NULL, HidNpadButton_L, false },
	{ "-", NULL, HidNpadButton_Minus, false },
	{ "Y", NULL, HidNpadButton_Y, false },
	{ "B", NULL, HidNpadButton_B, false },
};

static const char *TABS[SRC_COUNT];

static struct {
	Settings settings;
	/* Trending, or the last search. The other two sources belong to shelf.c. */
	MediaListing fetched;
	Source source;
	/* A cursor per source: stepping over to the favourites and back should land
	 * where it was, not at the top. */
	size_t selected[SRC_COUNT];
	size_t scroll[SRC_COUNT];
	char query[MEDIA_SEARCH_MAX];
	/* What the browse tab is showing: trending, a search term, or a channel name.
	 * Sized past the longest search term, so a label is never quietly cut. */
	char browse_label[LABEL_MAX];
	/* One step of history, so diving into a channel can be undone. Deeper than one
	 * is not worth the memory: the tab strip already says where you are. */
	MediaListing stashed;
	char stashed_label[LABEL_MAX];
	bool has_stash;
	char note[NOTE_MAX];
	int percent;
	bool cancelled;
	bool loaded;
} pane;

static const MediaListing *current_list(void) {
	switch (pane.source) {
		case SRC_HISTORY: return shelf_list(SHELF_HISTORY);
		case SRC_FAVOURITES: return shelf_list(SHELF_FAVOURITES);
		default: return &pane.fetched;
	}
}

static const MediaItem *current_item(void) {
	const MediaListing *list = current_list();
	size_t at = pane.selected[pane.source];

	return at < list->count ? &list->items[at] : NULL;
}

/* Pulls the cursor back inside a list that shrank underneath it -- a favourite
 * dropped, say, or a source switched to while it was shorter. */
static void clamp_cursor(void) {
	size_t count = current_list()->count;

	if (count == 0) {
		pane.selected[pane.source] = 0;
		pane.scroll[pane.source] = 0;
		return;
	}

	if (pane.selected[pane.source] >= count) pane.selected[pane.source] = count - 1;

	size_t row = pane.selected[pane.source] / UI_RESULT_COLS;
	size_t first = pane.scroll[pane.source] / UI_RESULT_COLS;

	if (row < first) first = row;
	if (row >= first + UI_RESULT_ROWS) first = row - UI_RESULT_ROWS + 1;

	pane.scroll[pane.source] = first * UI_RESULT_COLS;
}

void youtube_pane_open(void) {
	if (pane.loaded) return;

	settings_load(&pane.settings);
	shelf_load();
	pane.loaded = true;
	snprintf(pane.browse_label, sizeof(pane.browse_label), "%s", T(STR_TRENDING));

	/* Opening to an empty screen with a keyboard prompt asks the visitor to think
	 * of something before they have seen anything. What is popular now costs one
	 * request and gives the pane something to be. */
	ui_message(T(STR_SEARCHING), NULL, NULL, 0);

	char err[160];
	if (!media_trending(&pane.settings, &pane.fetched, err, sizeof(err))) {
		snprintf(pane.note, sizeof(pane.note), "%.150s", err);
	}
}

void youtube_pane_exit(void) {
	media_listing_free(&pane.fetched);
	media_listing_free(&pane.stashed);
	pane.has_stash = false;
	shelf_exit();
	memset(pane.selected, 0, sizeof(pane.selected));
	memset(pane.scroll, 0, sizeof(pane.scroll));
	pane.loaded = false;
}

/*
 * Steps the cursor through the grid. Clamped rather than wrapped: running off the
 * bottom row and reappearing at the top is disorienting when the cards are laid
 * out in space rather than in a line.
 */
static void move_by(int delta) {
	size_t count = current_list()->count;
	if (count == 0) return;

	long target = (long)pane.selected[pane.source] + delta;
	if (target < 0) target = 0;
	if (target >= (long)count) target = (long)count - 1;
	pane.selected[pane.source] = (size_t)target;

	clamp_cursor();
}

static void switch_source(int delta) {
	pane.source = (Source)(((int)pane.source + SRC_COUNT + delta) % SRC_COUNT);
	pane.note[0] = '\0';
	clamp_cursor();
}

/* --- searching ---------------------------------------------------------- */

static void search(void) {
	char typed[MEDIA_SEARCH_MAX];
	if (!keyboard_prompt(T(STR_SEARCH_HEADER), pane.query, typed, sizeof(typed))) return;

	snprintf(pane.query, sizeof(pane.query), "%s", typed);
	ui_message(T(STR_SEARCHING), pane.query, NULL, 0);

	MediaListing found = {0};
	char err[160];
	if (!media_search(&pane.settings, pane.query, &found, err, sizeof(err))) {
		snprintf(pane.note, sizeof(pane.note), "%.150s", err);
		return;
	}

	media_listing_free(&pane.fetched);
	pane.fetched = found;
	snprintf(pane.browse_label, sizeof(pane.browse_label), "%s", pane.query);

	/* A search is a fresh start, not a place to come back from. */
	media_listing_free(&pane.stashed);
	pane.has_stash = false;

	/* Results belong to the browse tab, so searching from anywhere lands there. */
	pane.source = SRC_BROWSE;
	pane.selected[SRC_BROWSE] = 0;
	pane.scroll[SRC_BROWSE] = 0;
	pane.note[0] = '\0';
}

/* --- playing, saving, marking -------------------------------------------- */

/* A live stream has no length and no finished file behind it, so the server can
 * neither merge it nor proxy it. Refusing here costs nothing; letting it through
 * costs a minute of waiting for a failure that was knowable in advance. */
static bool refuse_live(const MediaItem *item) {
	if (item->duration > 0) return false;

	snprintf(pane.note, sizeof(pane.note), "%s", T(STR_LIVE_NO_SUPPORT));
	return true;
}

/* Audio only, and quick. Worth its own button because a song does not need the
 * server to spend half a minute merging a picture nobody is going to look at. */
static void listen_selected(void) {
	const MediaItem *item = current_item();
	if (!item || refuse_live(item)) return;

	/* Resolved first, then played. The proxy stays silent until yt-dlp has finished
	 * with a cold video, which outlasts ffmpeg's own read timeout -- so asking the
	 * player to wait it out is how this ended up connecting forever. */
	ui_message(T(STR_PREPARING), item->title, NULL, 0);

	char url[640];
	char err[160];
	if (!media_prepare_audio(&pane.settings, item, url, sizeof(url), err, sizeof(err))) {
		snprintf(pane.note, sizeof(pane.note), "%.150s", err);
		return;
	}

	if (!player_play(url, NULL, err, sizeof(err))) {
		snprintf(pane.note, sizeof(pane.note), "%.150s", err);
		return;
	}

	pane.note[0] = '\0';
	shelf_remember(item);
	stage_begin(item->title);
}

static UiHint WAIT_HINTS[] = {
	{ "B", NULL, HidNpadButton_B, false },
};

/*
 * Whether B was pressed since the last look. The frame loop is not running during
 * a blocking transfer, so the pane cannot be handed the buttons the usual way and
 * reads a pad of its own instead. libnx is happy with more than one: each keeps
 * its own idea of what was held last, which is what "pressed" is measured against.
 */
static bool cancel_pressed(void) {
	static PadState pad;
	static bool ready;

	if (!ready) {
		padConfigureInput(1, HidNpadStyleSet_NpadStandard);
		padInitializeDefault(&pad);
		ready = true;
	}

	padUpdate(&pad);
	return (padGetButtonsDown(&pad) & HidNpadButton_B) != 0;
}

/* Both callbacks paint a whole frame themselves: the transfer blocks the loop, so
 * without that the console would look hung for the length of it. Both also offer
 * the way out, because a wait that runs for minutes and cannot be abandoned is
 * indistinguishable from one that has hung. */
static bool on_waiting(void *user, int seconds) {
	(void)user;

	WAIT_HINTS[0].label = T(STR_CANCEL);

	char detail[NOTE_MAX];
	snprintf(detail, sizeof(detail), "%s  (%d s)", T(STR_PREPARING), seconds);
	ui_message(T(STR_PREPARING), detail, WAIT_HINTS, 1);

	if (cancel_pressed()) {
		pane.cancelled = true;
		return false;
	}

	return appletMainLoop();
}

static bool on_progress(void *user, int64_t done, int64_t total) {
	(void)user;

	int percent = total > 0 ? (int)((done * 100) / total) : 0;
	if (percent != pane.percent) {
		pane.percent = percent;

		WAIT_HINTS[0].label = T(STR_CANCEL);

		char detail[NOTE_MAX];
		snprintf(detail, sizeof(detail), "%s %d%%", T(STR_DOWNLOADING), percent);
		ui_message(T(STR_DOWNLOADING), detail, WAIT_HINTS, 1);
	}

	/* Stopping mid-transfer leaves the .part file, which the next attempt resumes,
	 * so giving up here costs only the time and none of the bytes. */
	if (cancel_pressed()) {
		pane.cancelled = true;
		return false;
	}

	return appletMainLoop();
}

/*
 * Plays the stream YouTube already serves with its sound inside it, which the
 * server only has to pass along. Nothing is merged, so the wait does not grow with
 * the video -- an hour-long recording starts as quickly as a three-minute one,
 * which is the whole reason long videos are watchable now.
 *
 * Quality is 360p. The high-resolution copy is what the save button fetches, and
 * that one is worth its wait because it is kept.
 */
static void watch_selected(void) {
	const MediaItem *item = current_item();
	if (!item || refuse_live(item)) return;

	ui_message(T(STR_PREPARING), item->title, NULL, 0);

	char url[640];
	char err[160];
	if (!media_prepare_stream(&pane.settings, item, url, sizeof(url), err, sizeof(err))) {
		snprintf(pane.note, sizeof(pane.note), "%.150s", err);
		return;
	}

	if (!player_play(url, NULL, err, sizeof(err))) {
		snprintf(pane.note, sizeof(pane.note), "%.150s", err);
		return;
	}

	pane.note[0] = '\0';
	shelf_remember(item);
	stage_begin(item->title);
}

static void save_selected(void) {
	const MediaItem *item = current_item();
	if (!item || refuse_live(item)) return;

	if (!mkdir_p(SAVE_DIR)) {
		snprintf(pane.note, sizeof(pane.note), "%s: %s", T(STR_SAVE_FAIL), SAVE_DIR);
		return;
	}

	pane.percent = -1;
	pane.cancelled = false;

	char err[160];
	if (media_download(&pane.settings, item, SAVE_DIR, on_progress, NULL, on_waiting,
			err, sizeof(err))) {
		snprintf(pane.note, sizeof(pane.note), "%s: %.120s", T(STR_SAVED), item->filename);
	} else if (pane.cancelled) {
		/* The .part file stays behind, so pressing save again picks up where this
		 * left off rather than starting the transfer over. */
		snprintf(pane.note, sizeof(pane.note), "%s", T(STR_CANCELLED));
	} else {
		snprintf(pane.note, sizeof(pane.note), "%s: %.120s", T(STR_SAVE_FAIL), err);
	}
}

/*
 * Opens the channel behind the selected card. What was on the browse tab is kept
 * aside so B can put it back -- diving into a channel is going somewhere, and
 * going somewhere should be undoable.
 */
static void open_channel(void) {
	const MediaItem *item = current_item();
	if (!item) return;

	if (!item->author_id) {
		/* Entries YouTube credits to several parties name no single channel, and it
		 * does not supply one anywhere else either. */
		snprintf(pane.note, sizeof(pane.note), "%s", T(STR_NO_CHANNEL));
		return;
	}

	ui_message(T(STR_SEARCHING), item->author ? item->author : T(STR_CHANNEL), NULL, 0);

	MediaListing found = {0};
	char name[LABEL_MAX] = {0};
	char err[160];
	if (!media_channel(&pane.settings, item->author_id, &found, name, sizeof(name),
			err, sizeof(err))) {
		snprintf(pane.note, sizeof(pane.note), "%.150s", err);
		return;
	}

	/* Only one step is kept, so a second dive replaces the first rather than
	 * stacking: the list under it was itself a channel, and the tab says so. */
	if (!pane.has_stash) {
		pane.stashed = pane.fetched;
		snprintf(pane.stashed_label, sizeof(pane.stashed_label), "%s", pane.browse_label);
		pane.has_stash = true;
	} else {
		media_listing_free(&pane.fetched);
	}

	pane.fetched = found;
	snprintf(pane.browse_label, sizeof(pane.browse_label), "%s",
			name[0] ? name : (item->author ? item->author : T(STR_CHANNEL)));

	pane.source = SRC_BROWSE;
	pane.selected[SRC_BROWSE] = 0;
	pane.scroll[SRC_BROWSE] = 0;
	pane.note[0] = '\0';
}

/** Puts back what the channel replaced. False means there was nothing to go back to. */
static bool leave_channel(void) {
	if (!pane.has_stash) return false;

	media_listing_free(&pane.fetched);
	pane.fetched = pane.stashed;
	pane.stashed = (MediaListing){0};
	pane.has_stash = false;

	snprintf(pane.browse_label, sizeof(pane.browse_label), "%s", pane.stashed_label);

	pane.source = SRC_BROWSE;
	pane.selected[SRC_BROWSE] = 0;
	pane.scroll[SRC_BROWSE] = 0;
	pane.note[0] = '\0';
	return true;
}

static void toggle_favourite(void) {
	const MediaItem *item = current_item();
	if (!item) return;

	bool added = shelf_toggle_favourite(item);
	snprintf(pane.note, sizeof(pane.note), "%s",
			added ? T(STR_FAVOURITE_ADD) : T(STR_FAVOURITE_DROP));

	/* Unmarking one while standing in the favourites shortens the list under the
	 * cursor, so it has to be pulled back inside. */
	clamp_cursor();
}

/* --- frame --------------------------------------------------------------- */

bool youtube_pane_input(uint64_t down) {
	if (down & HidNpadButton_Left) move_by(-1);
	if (down & HidNpadButton_Right) move_by(1);
	if (down & HidNpadButton_Up) move_by(-UI_RESULT_COLS);
	if (down & HidNpadButton_Down) move_by(UI_RESULT_COLS);

	if (down & HidNpadButton_L) switch_source(-1);
	if (down & HidNpadButton_R) switch_source(1);

	if (down & HidNpadButton_A) watch_selected();
	if (down & HidNpadButton_X) listen_selected();
	if (down & HidNpadButton_ZL) toggle_favourite();
	if (down & HidNpadButton_ZR) save_selected();
	if (down & HidNpadButton_Minus) open_channel();
	if (down & HidNpadButton_Y) search();

	/* B climbs out of a channel first, and only gives up the section once there is
	 * nowhere left to climb -- the same way the card browser spends it. */
	if (down & HidNpadButton_B) return leave_channel();

	return true;
}

void youtube_pane_touch(int x, int y) {
	int hit = ui_hit_result(x, y);
	if (hit < 0) return;

	size_t index = pane.scroll[pane.source] + (size_t)hit;
	if (index >= current_list()->count) return;

	pane.selected[pane.source] = index;
	watch_selected();
}

void youtube_pane_draw(uint64_t held) {
	const MediaItem *item = current_item();

	HINTS[0].label = T(STR_WATCH);
	HINTS[1].label = T(STR_LISTEN);
	HINTS[2].label = (item && shelf_is_favourite(item->id))
			? T(STR_UNFAVOURITE)
			: T(STR_FAVOURITES);
	HINTS[3].label = T(STR_SAVE);
	HINTS[4].label = T(STR_TAB);
	HINTS[5].label = T(STR_CHANNEL);
	HINTS[6].label = T(STR_SEARCH);
	HINTS[7].label = T(STR_BACK);

	/* The browse tab wears whatever is actually in it -- trending, a search term, or
	 * a channel's name -- rather than a word that stopped being true. */
	TABS[SRC_BROWSE] = pane.browse_label;
	TABS[SRC_HISTORY] = T(STR_HISTORY);
	TABS[SRC_FAVOURITES] = T(STR_FAVOURITES);

	const MediaListing *list = current_list();

	char title[96];
	if (list->count > 0) {
		snprintf(title, sizeof(title), "%s  (%zu)", T(STR_YT), list->count);
	} else {
		snprintf(title, sizeof(title), "%s", T(STR_YT));
	}

	ui_header(T(STR_YT_EYEBROW), title, pane.note);
	ui_result_tabs(TABS, SRC_COUNT, pane.source);

	if (list->count == 0) {
		ui_empty(pane.source == SRC_BROWSE ? T(STR_YT_EMPTY) : T(STR_SHELF_EMPTY));
	} else {
		ui_results(list, pane.selected[pane.source], pane.scroll[pane.source]);
	}

	ui_footer(HINTS, sizeof(HINTS) / sizeof(HINTS[0]), held);
}
