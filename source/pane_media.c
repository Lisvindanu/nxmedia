#include "pane_media.h"

#include <stdio.h>
#include <string.h>

#include <switch.h>

#include "i18n.h"
#include "keyboard.h"
#include "player.h"
#include "stage.h"
#include "ui.h"

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

/* Room for whatever the list module says fits on screen, without this file having to
 * agree with it on a number. */
#define ROW_CAP 16

/*
 * How often the walk repaints the screen, counted in folders rather than in
 * milliseconds so no clock is needed here.
 *
 * Every repaint is a whole frame, so repainting per folder would make drawing the
 * progress cost more than the search it is reporting on. Cancelling is checked on
 * every folder regardless -- that part is only a pad read.
 */
#define SCAN_REDRAW_EVERY 24

static UiHint SCAN_HINTS[] = {
	{ "B", NULL, HidNpadButton_B, false },
};

/*
 * Whether B was pressed since the last look.
 *
 * The frame loop is stopped while the card is being walked, so the pane cannot be
 * handed the buttons the usual way and reads a pad of its own. libnx is happy with
 * more than one: each keeps its own idea of what was held last, which is what
 * "pressed" is measured against.
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

static bool on_scanning(void *user, size_t folders, size_t found) {
	(void)user;

	if (folders % SCAN_REDRAW_EVERY == 0) {
		char detail[96];
		snprintf(detail, sizeof(detail), "%zu folder, %zu hasil", folders, found);

		SCAN_HINTS[0].label = T(STR_CANCEL);
		ui_message(T(STR_CARD_SCANNING), detail, SCAN_HINTS, 1);
	}

	/* Whatever was found so far is kept, so giving up costs the rest of the walk
	 * and none of the matches. */
	if (cancel_pressed()) return false;

	return appletMainLoop();
}

static UiHint HINTS[] = {
	{ "A", NULL, HidNpadButton_A, true },
	{ "Y", NULL, HidNpadButton_Y, false },
	{ "ZR", NULL, HidNpadButton_ZR, false },
	{ "X", NULL, HidNpadButton_X, false },
	{ "B", NULL, HidNpadButton_B, false },
	{ "+", NULL, HidNpadButton_Plus, false },
};

/* --- state -------------------------------------------------------------- */

static void reload(MediaPane *pane) {
	library_listing_free(&pane->listing);
	pane->selected = 0;
	pane->scroll = 0;
	pane->note[0] = '\0';

	char err[160];
	bool partial = false;
	bool ok = pane->searching
			? library_search(pane->query, pane->kind, &pane->listing, on_scanning, pane,
					&partial, err, sizeof(err))
			: library_list(pane->dir, pane->kind, &pane->listing, err, sizeof(err));

	if (!ok) snprintf(pane->note, sizeof(pane->note), "%.150s", err);
	else if (partial) snprintf(pane->note, sizeof(pane->note), "%s", T(STR_CARD_PARTIAL));
}

/*
 * Asks for a name and shows every match on the whole card.
 *
 * This is what a folder tree cannot do: a track downloaded weeks ago is in a
 * folder whose name is the first thing forgotten, and walking the card to find it
 * again is the one job a computer should be doing instead.
 *
 * The screen is frozen while this runs -- the frame loop is not turning -- so a
 * message goes up first, and the walk is bounded in library.c rather than left to
 * discover how large the card is.
 */
static void search(MediaPane *pane) {
	char typed[sizeof(pane->query)];
	if (!keyboard_prompt(T(STR_CARD_SEARCH_HEADER), pane->query, typed, sizeof(typed))) {
		return;
	}

	snprintf(pane->query, sizeof(pane->query), "%s", typed);
	pane->searching = true;
	reload(pane);
}

void media_pane_open(MediaPane *pane, LibraryKind kind) {
	if (pane->dir[0]) return;

	pane->kind = kind;
	pane->searching = false;
	snprintf(pane->dir, sizeof(pane->dir), "%s", LIBRARY_ROOT);
	reload(pane);
}

void media_pane_exit(MediaPane *pane) {
	library_listing_free(&pane->listing);
}

static void move(MediaPane *pane, int delta) {
	size_t count = pane->listing.count;
	if (count == 0) return;

	pane->selected = (pane->selected + count + (size_t)delta) % count;

	size_t rows = ui_rows_visible();
	if (pane->selected < pane->scroll) pane->scroll = pane->selected;
	if (pane->selected >= pane->scroll + rows) pane->scroll = pane->selected - rows + 1;
}

static void open_selected(MediaPane *pane) {
	if (pane->selected >= pane->listing.count) return;
	const LibraryEntry *item = &pane->listing.items[pane->selected];

	/* A search result names the folder it was found in; a browsed row is in the
	 * folder on screen. Joining to the wrong one is how a match opens nothing. */
	const char *holder = item->dir ? item->dir : pane->dir;

	char path[LIBRARY_PATH_MAX];
	if (!library_join(holder, item->name, path, sizeof(path))) return;

	if (item->is_dir) {
		pane->searching = false;
		snprintf(pane->dir, sizeof(pane->dir), "%s", path);
		reload(pane);
		return;
	}

	/* ffmpeg treats everything before the first colon as a protocol name, and every path
	 * on this console begins "sdmc:". The file protocol takes the rest verbatim, which
	 * is the only way to name a local file whose path carries a device prefix. */
	char url[sizeof(path) + 5];
	snprintf(url, sizeof(url), "file:%s", path);

	char err[160];
	if (!player_play(url, NULL, err, sizeof(err))) {
		snprintf(pane->note, sizeof(pane->note), "%.150s", err);
		return;
	}

	snprintf(pane->title, sizeof(pane->title), "%s", item->name);
	pane->note[0] = '\0';

	/* A film takes the screen; a song leaves the list up so the next one can be
	 * found while it plays. */
	if (pane->kind == LibraryKind_Video) stage_begin(item->name);
}

static void stop(MediaPane *pane) {
	player_stop();
	pane->title[0] = '\0';
}

bool media_pane_input(MediaPane *pane, uint64_t down) {
	if (down & HidNpadButton_Down) move(pane, 1);
	if (down & HidNpadButton_Up) move(pane, -1);
	if (down & HidNpadButton_A) open_selected(pane);
	if (down & HidNpadButton_Y) search(pane);
	if (down & HidNpadButton_ZR) stop(pane);

	/* Leaving results comes first: `dir` was never touched by the search, so this
	 * puts the browser back exactly where it was. */
	if ((down & HidNpadButton_B) && pane->searching) {
		pane->searching = false;
		reload(pane);
		return true;
	}

	/* At the root there is nowhere further up, so B means what it means everywhere
	 * else in the app and the caller gets it back. */
	if (down & HidNpadButton_B) {
		if (!library_parent(pane->dir)) return false;
		reload(pane);
	}

	return true;
}

void media_pane_touch(MediaPane *pane, int x, int y) {
	int hit = ui_hit_row(x, y);
	if (hit < 0) return;

	size_t index = pane->scroll + (size_t)hit;
	if (index >= pane->listing.count) return;

	/* A tap picks and opens at once; the cursor still moves so the pane is where the
	 * user left it if they come back with the buttons. */
	pane->selected = index;
	open_selected(pane);
}

/* --- drawing ------------------------------------------------------------ */

/** The folder being looked at. The root has no name after its own slash. */
static const char *folder_name(const MediaPane *pane) {
	if (pane->searching) return pane->query;

	const char *slash = strrchr(pane->dir, '/');
	return (slash && slash[1]) ? slash + 1 : "SD";
}

static void draw_list(const MediaPane *pane) {
	if (pane->listing.count == 0) {
		ui_empty(pane->searching ? T(STR_CARD_NO_MATCH) : T(STR_NO_MEDIA));
		return;
	}

	size_t visible = ui_rows_visible();
	if (visible > ROW_CAP) visible = ROW_CAP;
	if (visible > pane->listing.count - pane->scroll) {
		visible = pane->listing.count - pane->scroll;
	}

	const char *file_kind = pane->kind == LibraryKind_Video ? T(STR_KIND_VIDEO)
			: T(STR_KIND_AUDIO);

	UiRow rows[ROW_CAP];
	char detail[ROW_CAP][24];

	for (size_t i = 0; i < visible; i++) {
		const LibraryEntry *item = &pane->listing.items[pane->scroll + i];
		detail[i][0] = '\0';
		if (!item->is_dir) ui_format_size(item->size, detail[i], sizeof(detail[i]));

		rows[i] = (UiRow){
			.name = item->name,
			.detail = item->is_dir ? NULL : detail[i],
			.kind = item->is_dir ? T(STR_KIND_FOLDER) : file_kind,
			/* Tinted means playable, so the rows worth pressing A on carry the accent. */
			.accent = !item->is_dir,
		};
	}

	ui_rows(rows, visible, pane->selected - pane->scroll, pane->listing.count, pane->scroll);
}

static bool playing_now(void) {
	PlayerState state = player_state();
	return state == PlayerState_Connecting || state == PlayerState_Playing;
}

static void now_playing(const MediaPane *pane, char *out, size_t len) {
	if (player_state() == PlayerState_Connecting) {
		snprintf(out, len, "%.100s  --  %s", pane->title, T(STR_CONNECTING));
		return;
	}

	char now[16];
	ui_format_time(player_position(), now, sizeof(now));

	if (player_duration() > 0) {
		char total[16];
		ui_format_time(player_duration(), total, sizeof(total));
		snprintf(out, len, "%.100s  --  %s / %s", pane->title, now, total);
	} else {
		snprintf(out, len, "%.100s  --  %s", pane->title, now);
	}
}

void media_pane_draw(MediaPane *pane, const char *eyebrow, uint64_t held) {
	char status[192];
	const char *subtitle = T(STR_BROWSE_HINT);

	if (pane->note[0]) {
		subtitle = pane->note;
	} else if (playing_now() && pane->title[0]) {
		/* Sound with no picture keeps the list up, so the header is the only place
		 * left to say what is still running. */
		now_playing(pane, status, sizeof(status));
		subtitle = status;
	}

	ui_header(eyebrow, folder_name(pane), subtitle);
	draw_list(pane);

	HINTS[0].label = pane->selected < pane->listing.count &&
			pane->listing.items[pane->selected].is_dir ? T(STR_OPEN) : T(STR_PLAY);
	HINTS[1].label = T(STR_SEARCH);
	HINTS[2].label = T(STR_STOP);
	HINTS[3].label = T(STR_SCREEN_OFF);
	HINTS[4].label = T(STR_UP);
	HINTS[5].label = T(STR_QUIT);
	ui_footer(HINTS, COUNT_OF(HINTS), held);
}
