#include "pane_drive.h"

#include <stdio.h>
#include <string.h>

#include <switch.h>

#include "i18n.h"
#include "keyboard.h"
#include "player.h"
#include "stage.h"
#include "ui.h"

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

/* Room for whatever the list module says fits, without this file having to agree
 * with it on a number. */
#define ROW_CAP 16

static UiHint HINTS[] = {
	{ "A", NULL, HidNpadButton_A, true },
	{ "Y", NULL, HidNpadButton_Y, false },
	{ "X", NULL, HidNpadButton_X, false },
	{ "ZR", NULL, HidNpadButton_ZR, false },
	{ "B", NULL, HidNpadButton_B, false },
	{ "+", NULL, HidNpadButton_Plus, false },
};

/* --- state -------------------------------------------------------------- */

static const char *current_id(const DrivePane *pane) {
	return pane->depth ? pane->stack[pane->depth - 1].id : DRIVE_ROOT;
}

/*
 * Fetches the folder on screen. This blocks on the network, so the frame loop is
 * not running: a message goes up first or the console looks hung for the second
 * or two Drive takes to answer.
 */
static void reload(DrivePane *pane) {
	ui_message(T(STR_DRIVE_LOADING), NULL, NULL, 0);

	drive_listing_free(&pane->listing);
	pane->selected = 0;
	pane->scroll = 0;
	pane->note[0] = '\0';

	char err[176];
	printf("[drive] memuat %s\n", pane->searching ? pane->query : current_id(pane));

	bool ok = pane->searching
			? drive_search(pane->query, &pane->listing, err, sizeof(err))
			: drive_list(current_id(pane), &pane->listing, err, sizeof(err));

	if (!ok) snprintf(pane->note, sizeof(pane->note), "%.180s", err);
}

/*
 * Asks for a name and shows every match in the whole drive.
 *
 * The folder stack is deliberately left alone: the thing being looked for is
 * usually in a folder the user has forgotten, and after watching it they want to
 * be back where they were rather than somewhere they never chose to be.
 */
static void search(DrivePane *pane) {
	char typed[DRIVE_SEARCH_MAX];
	if (!keyboard_prompt(T(STR_DRIVE_SEARCH_HEADER), pane->query, typed, sizeof(typed))) {
		return;
	}

	snprintf(pane->query, sizeof(pane->query), "%s", typed);
	pane->searching = true;
	reload(pane);
}

void drive_pane_open(DrivePane *pane) {
	/* Opened once and then left where it was: coming back to the section should not
	 * cost another round trip, nor lose the folder being looked through. */
	if (pane->opened) return;

	pane->opened = true;
	pane->depth = 0;
	pane->searching = false;
	reload(pane);
}

void drive_pane_exit(DrivePane *pane) {
	drive_listing_free(&pane->listing);
	pane->opened = false;
}

static void move(DrivePane *pane, int delta) {
	size_t count = pane->listing.count;
	if (count == 0) return;

	pane->selected = (pane->selected + count + (size_t)delta) % count;

	size_t rows = ui_rows_visible();
	if (pane->selected < pane->scroll) pane->scroll = pane->selected;
	if (pane->selected >= pane->scroll + rows) pane->scroll = pane->selected - rows + 1;
}

static void descend(DrivePane *pane, const DriveEntry *entry) {
	/* Opening a folder out of a search result is how the user gets back to browsing,
	 * so the results give way to that folder's own contents. */
	pane->searching = false;

	if (pane->depth == DRIVE_STACK_MAX) {
		/* Deeper than this is a folder tree nobody is navigating with a thumbstick,
		 * and silently doing nothing would read as a broken button. */
		snprintf(pane->note, sizeof(pane->note), "%s", T(STR_DRIVE_EMPTY));
		return;
	}

	DriveCrumb *crumb = &pane->stack[pane->depth++];
	snprintf(crumb->id, sizeof(crumb->id), "%s", entry->id);
	snprintf(crumb->name, sizeof(crumb->name), "%s", entry->name);

	reload(pane);
}

static void play(DrivePane *pane, const DriveEntry *entry) {
	char url[DRIVE_URL_MAX];
	char token[2048];
	char err[176];

	ui_message(T(STR_PREPARING), entry->name, NULL, 0);

	if (!drive_stream(entry->id, url, sizeof(url), token, sizeof(token), err, sizeof(err))) {
		snprintf(pane->note, sizeof(pane->note), "%.180s", err);
		printf("[drive] gagal menyiapkan: %s\n", err);
		return;
	}

	/* The token travels as a header rather than in the URL, so it never reaches a
	 * log line or a status row by accident. */
	PlayerHeaders headers = { .bearer = token };

	if (!player_play(url, &headers, err, sizeof(err))) {
		snprintf(pane->note, sizeof(pane->note), "%.180s", err);
		printf("[drive] player menolak: %s\n", err);
		return;
	}

	snprintf(pane->title, sizeof(pane->title), "%s", entry->name);
	pane->note[0] = '\0';

	/* A film takes the screen; sound alone leaves the list up. */
	if (entry->is_video) stage_begin(entry->name);
}

static void open_selected(DrivePane *pane) {
	if (pane->selected >= pane->listing.count) return;

	const DriveEntry *entry = &pane->listing.items[pane->selected];
	if (entry->is_folder) descend(pane, entry);
	else play(pane, entry);
}

static void stop(DrivePane *pane) {
	player_stop();
	pane->title[0] = '\0';
}

bool drive_pane_input(DrivePane *pane, uint64_t down) {
	if (down & HidNpadButton_Down) move(pane, 1);
	if (down & HidNpadButton_Up) move(pane, -1);
	if (down & HidNpadButton_A) open_selected(pane);
	if (down & HidNpadButton_Y) search(pane);
	if (down & HidNpadButton_X) stop(pane);
	if (down & HidNpadButton_ZR) reload(pane);

	if (down & HidNpadButton_B) {
		/* Leaving results comes before climbing folders: the stack was never touched
		 * by the search, so this puts the list back exactly where it was. */
		if (pane->searching) {
			pane->searching = false;
			reload(pane);
			return true;
		}

		/* At My Drive there is nowhere further up, so B means what it means
		 * everywhere else in the app and the caller gets it back. */
		if (pane->depth == 0) return false;

		pane->depth--;
		reload(pane);
	}

	return true;
}

void drive_pane_touch(DrivePane *pane, int x, int y) {
	int hit = ui_hit_row(x, y);
	if (hit < 0) return;

	size_t index = pane->scroll + (size_t)hit;
	if (index >= pane->listing.count) return;

	pane->selected = index;
	open_selected(pane);
}

/* --- drawing ------------------------------------------------------------ */

static const char *folder_name(const DrivePane *pane) {
	if (pane->searching) return pane->query;
	return pane->depth ? pane->stack[pane->depth - 1].name : "My Drive";
}

static void draw_list(const DrivePane *pane) {
	if (pane->listing.count == 0) {
		ui_empty(T(STR_DRIVE_EMPTY));
		return;
	}

	size_t visible = ui_rows_visible();
	if (visible > ROW_CAP) visible = ROW_CAP;
	if (visible > pane->listing.count - pane->scroll) {
		visible = pane->listing.count - pane->scroll;
	}

	UiRow rows[ROW_CAP];
	char detail[ROW_CAP][24];

	for (size_t i = 0; i < visible; i++) {
		const DriveEntry *item = &pane->listing.items[pane->scroll + i];
		detail[i][0] = '\0';
		if (!item->is_folder && item->size >= 0) {
			ui_format_size(item->size, detail[i], sizeof(detail[i]));
		}

		rows[i] = (UiRow){
			.name = item->name,
			.detail = item->is_folder ? NULL : detail[i],
			.kind = item->is_folder ? T(STR_KIND_FOLDER)
					: (item->is_video ? T(STR_KIND_VIDEO) : T(STR_KIND_AUDIO)),
			/* Tinted means playable, so the rows worth pressing A on carry the accent. */
			.accent = !item->is_folder,
		};
	}

	ui_rows(rows, visible, pane->selected - pane->scroll, pane->listing.count, pane->scroll);
}

static bool playing_now(void) {
	PlayerState state = player_state();
	return state == PlayerState_Connecting || state == PlayerState_Playing;
}

static void now_playing(const DrivePane *pane, char *out, size_t len) {
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

void drive_pane_draw(DrivePane *pane, const char *eyebrow, uint64_t held) {
	char status[192];
	const char *subtitle = T(STR_BROWSE_HINT);

	if (pane->note[0]) {
		subtitle = pane->note;
	} else if (playing_now() && pane->title[0]) {
		now_playing(pane, status, sizeof(status));
		subtitle = status;
	}

	ui_header(eyebrow, folder_name(pane), subtitle);
	draw_list(pane);

	HINTS[0].label = pane->selected < pane->listing.count &&
			pane->listing.items[pane->selected].is_folder ? T(STR_OPEN) : T(STR_PLAY);
	HINTS[1].label = T(STR_SEARCH);
	HINTS[2].label = T(STR_STOP);
	HINTS[3].label = T(STR_RELOAD);
	HINTS[4].label = T(STR_UP);
	HINTS[5].label = T(STR_QUIT);
	ui_footer(HINTS, COUNT_OF(HINTS), held);
}
