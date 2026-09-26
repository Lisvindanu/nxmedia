#include "pane_youtube.h"

#include <stdio.h>
#include <string.h>

#include <switch.h>

#include "i18n.h"
#include "keyboard.h"
#include "mediavault.h"
#include "player.h"
#include "settings.h"
#include "stage.h"
#include "ui.h"
#include "util.h"

/*
 * YouTube by way of the MediaVault server: what is popular now, whatever you
 * search for, played as sound or kept on the card as video.
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

static UiHint HINTS[] = {
	{ "A", NULL, HidNpadButton_A, true },
	{ "X", NULL, HidNpadButton_X, false },
	{ "ZR", NULL, HidNpadButton_ZR, false },
	{ "Y", NULL, HidNpadButton_Y, false },
	{ "B", NULL, HidNpadButton_B, false },
};

static struct {
	Settings settings;
	MediaListing list;
	size_t selected;
	size_t scroll;
	char query[MEDIA_SEARCH_MAX];
	char note[NOTE_MAX];
	int percent;
	bool loaded;
} pane;

void youtube_pane_open(void) {
	if (pane.loaded) return;

	settings_load(&pane.settings);
	pane.loaded = true;

	/* Opening to an empty screen with a keyboard prompt asks the visitor to think
	 * of something before they have seen anything. What is popular now costs one
	 * request and gives the pane something to be. */
	ui_message(T(STR_SEARCHING), NULL, NULL, 0);

	char err[160];
	if (!media_trending(&pane.settings, &pane.list, err, sizeof(err))) {
		snprintf(pane.note, sizeof(pane.note), "%.150s", err);
	}
}

void youtube_pane_exit(void) {
	media_listing_free(&pane.list);
	pane.selected = 0;
	pane.scroll = 0;
	pane.loaded = false;
}

/*
 * Steps the cursor through the grid. Clamped rather than wrapped: running off the
 * bottom row and reappearing at the top is disorienting when the cards are laid
 * out in space rather than in a line.
 */
static void move_by(int delta) {
	size_t count = pane.list.count;
	if (count == 0) return;

	long target = (long)pane.selected + delta;
	if (target < 0) target = 0;
	if (target >= (long)count) target = (long)count - 1;
	pane.selected = (size_t)target;

	/* The window moves a whole row at a time, so the grid never shows half a row
	 * of cards sliced by the top of the content area. */
	size_t row = pane.selected / UI_RESULT_COLS;
	size_t first = pane.scroll / UI_RESULT_COLS;

	if (row < first) first = row;
	if (row >= first + UI_RESULT_ROWS) first = row - UI_RESULT_ROWS + 1;

	pane.scroll = first * UI_RESULT_COLS;
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

	media_listing_free(&pane.list);
	pane.list = found;
	pane.selected = 0;
	pane.scroll = 0;
	pane.note[0] = '\0';
}

/* --- playing and saving -------------------------------------------------- */

/* A live stream has no length and no finished file behind it, so the server can
 * neither merge it nor proxy it. Refusing here costs nothing; letting it through
 * costs a minute of waiting for a failure that was knowable in advance. */
static bool refuse_live(const MediaItem *item) {
	if (item->duration > 0) return false;

	snprintf(pane.note, sizeof(pane.note), "%s", T(STR_LIVE_NO_SUPPORT));
	return true;
}

/* Audio only, and instant. Worth its own button because a song does not need the
 * server to spend half a minute merging a picture nobody is going to look at. */
static void listen_selected(void) {
	if (pane.selected >= pane.list.count) return;
	const MediaItem *item = &pane.list.items[pane.selected];
	if (refuse_live(item)) return;

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
	stage_begin(item->title);
}

/* Both callbacks paint a whole frame themselves: the transfer blocks the loop, so
 * without that the console would look hung for the length of it. */
static bool on_waiting(void *user, int seconds) {
	(void)user;

	char detail[NOTE_MAX];
	snprintf(detail, sizeof(detail), "%s  (%d s)", T(STR_PREPARING), seconds);
	ui_message(T(STR_PREPARING), detail, NULL, 0);

	return appletMainLoop();
}

static bool on_progress(void *user, int64_t done, int64_t total) {
	(void)user;

	int percent = total > 0 ? (int)((done * 100) / total) : 0;
	if (percent != pane.percent) {
		pane.percent = percent;

		char detail[NOTE_MAX];
		snprintf(detail, sizeof(detail), "%s %d%%", T(STR_DOWNLOADING), percent);
		ui_message(T(STR_DOWNLOADING), detail, NULL, 0);
	}

	/* Quitting mid-transfer leaves the .part file, which the next attempt resumes. */
	return appletMainLoop();
}

/* Streams the very file the save button would write, without writing it. The
 * server has to merge it first either way, so the wait is the same; what differs
 * is that nothing lands on the card. */
static void watch_selected(void) {
	if (pane.selected >= pane.list.count) return;
	const MediaItem *item = &pane.list.items[pane.selected];
	if (refuse_live(item)) return;

	char url[640];
	char err[160];
	if (!media_prepare_video(&pane.settings, item, url, sizeof(url), NULL, on_waiting,
			err, sizeof(err))) {
		snprintf(pane.note, sizeof(pane.note), "%.150s", err);
		return;
	}

	if (!player_play(url, NULL, err, sizeof(err))) {
		snprintf(pane.note, sizeof(pane.note), "%.150s", err);
		return;
	}

	pane.note[0] = '\0';
	stage_begin(item->title);
}

static void save_selected(void) {
	if (pane.selected >= pane.list.count) return;
	const MediaItem *item = &pane.list.items[pane.selected];
	if (refuse_live(item)) return;

	if (!mkdir_p(SAVE_DIR)) {
		snprintf(pane.note, sizeof(pane.note), "%s: %s", T(STR_SAVE_FAIL), SAVE_DIR);
		return;
	}

	pane.percent = -1;

	char err[160];
	if (media_download(&pane.settings, item, SAVE_DIR, on_progress, NULL, on_waiting,
			err, sizeof(err))) {
		snprintf(pane.note, sizeof(pane.note), "%s: %.120s", T(STR_SAVED), item->filename);
	} else {
		snprintf(pane.note, sizeof(pane.note), "%s: %.120s", T(STR_SAVE_FAIL), err);
	}
}

/* --- frame --------------------------------------------------------------- */

bool youtube_pane_input(uint64_t down) {
	if (down & HidNpadButton_Left) move_by(-1);
	if (down & HidNpadButton_Right) move_by(1);
	if (down & HidNpadButton_Up) move_by(-UI_RESULT_COLS);
	if (down & HidNpadButton_Down) move_by(UI_RESULT_COLS);
	if (down & HidNpadButton_A) watch_selected();
	if (down & HidNpadButton_X) listen_selected();
	if (down & HidNpadButton_ZR) save_selected();
	if (down & HidNpadButton_Y) search();
	if (down & HidNpadButton_B) return false;

	return true;
}

void youtube_pane_touch(int x, int y) {
	int hit = ui_hit_result(x, y);
	if (hit < 0) return;

	size_t index = pane.scroll + (size_t)hit;
	if (index >= pane.list.count) return;

	pane.selected = index;
	watch_selected();
}

void youtube_pane_draw(uint64_t held) {
	HINTS[0].label = T(STR_WATCH);
	HINTS[1].label = T(STR_LISTEN);
	HINTS[2].label = T(STR_SAVE);
	HINTS[3].label = T(STR_SEARCH);
	HINTS[4].label = T(STR_BACK);

	char title[96];
	if (pane.list.count > 0) {
		snprintf(title, sizeof(title), "%s  (%zu)", T(STR_YT), pane.list.count);
	} else {
		snprintf(title, sizeof(title), "%s", T(STR_YT));
	}

	ui_header(T(STR_YT_EYEBROW), title, pane.note[0] ? pane.note : pane.query);

	if (pane.list.count == 0) {
		ui_empty(T(STR_YT_EMPTY));
	} else {
		ui_results(&pane.list, pane.selected, pane.scroll);
	}

	ui_footer(HINTS, sizeof(HINTS) / sizeof(HINTS[0]), held);
}
