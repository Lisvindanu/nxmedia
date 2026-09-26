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
 * The two buttons do genuinely different things because the server offers two
 * different routes. Audio is proxied live and starts at once, which is what makes
 * this usable as a music player. Video has no public streaming route, so saving it
 * is the only way to watch it -- the file lands on the card and plays from the
 * Card branch like anything else there.
 */

#define SAVE_DIR "sdmc:/nxmedia"
#define NOTE_MAX 192

static UiHint HINTS[] = {
	{ "A", NULL, HidNpadButton_A, true },
	{ "X", NULL, HidNpadButton_X, false },
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

static void move(int delta) {
	size_t count = pane.list.count;
	if (count == 0) return;

	pane.selected = (pane.selected + count + (size_t)delta) % count;

	if (pane.selected < pane.scroll) pane.scroll = pane.selected;
	if (pane.selected >= pane.scroll + UI_RESULT_ROWS) {
		pane.scroll = pane.selected - UI_RESULT_ROWS + 1;
	}
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

static void play_selected(void) {
	if (pane.selected >= pane.list.count) return;
	const MediaItem *item = &pane.list.items[pane.selected];

	/* The proxy hands back a complete m4a with byte ranges, so ffmpeg opens it the
	 * same way it opens a file and there is nothing to wait for. */
	char url[640];
	snprintf(url, sizeof(url), "%s/audio/%s", pane.settings.mediavault_url, item->id);

	char err[160];
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

static void save_selected(void) {
	if (pane.selected >= pane.list.count) return;
	const MediaItem *item = &pane.list.items[pane.selected];

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
	if (down & HidNpadButton_Down) move(1);
	if (down & HidNpadButton_Up) move(-1);
	if (down & HidNpadButton_A) play_selected();
	if (down & HidNpadButton_X) save_selected();
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
	play_selected();
}

void youtube_pane_draw(uint64_t held) {
	HINTS[0].label = T(STR_PLAY);
	HINTS[1].label = T(STR_SAVE);
	HINTS[2].label = T(STR_SEARCH);
	HINTS[3].label = T(STR_BACK);

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
