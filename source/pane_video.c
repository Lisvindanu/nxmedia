#include "pane_video.h"

#include <switch.h>

#include "i18n.h"
#include "pane_iptv.h"
#include "pane_media.h"
#include "ui.h"

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

typedef enum {
	View_Menu,
	View_Live,
	View_Card,
} View;

#define MENU_LIVE 0
#define MENU_CARD 1
#define MENU_COUNT 2

static UiHint HINTS[] = {
	{ "A", NULL, HidNpadButton_A, true },
	{ "B", NULL, HidNpadButton_B, false },
	{ "+", NULL, HidNpadButton_Plus, false },
};

static struct {
	View view;
	size_t choice;
	MediaPane card;
} pane;

void video_pane_open(void) {
	/* Straight back to whichever half was being used last: the choice is only
	 * interesting the first time. */
	if (pane.view == View_Live) iptv_pane_open();
	if (pane.view == View_Card) media_pane_open(&pane.card, LibraryKind_Video);
}

void video_pane_exit(void) {
	iptv_pane_exit();
	media_pane_exit(&pane.card);
}

static void enter(size_t choice) {
	if (choice == MENU_LIVE) {
		pane.view = View_Live;
		iptv_pane_open();
	} else {
		pane.view = View_Card;
		media_pane_open(&pane.card, LibraryKind_Video);
	}
}

bool video_pane_input(uint64_t down) {
	switch (pane.view) {
		case View_Live:
			if (iptv_pane_input(down)) return true;
			pane.view = View_Menu;
			return true;

		case View_Card:
			if (media_pane_input(&pane.card, down)) return true;
			pane.view = View_Menu;
			return true;

		case View_Menu:
			break;
	}

	if (down & (HidNpadButton_Down | HidNpadButton_Up)) {
		pane.choice = (down & HidNpadButton_Down)
				? (pane.choice + 1) % MENU_COUNT
				: (pane.choice + MENU_COUNT - 1) % MENU_COUNT;
	}

	if (down & HidNpadButton_A) enter(pane.choice);
	if (down & HidNpadButton_B) return false;

	return true;
}

void video_pane_touch(int x, int y) {
	switch (pane.view) {
		case View_Live: iptv_pane_touch(x, y); return;
		case View_Card: media_pane_touch(&pane.card, x, y); return;
		case View_Menu: break;
	}

	int hit = ui_hit_row(x, y);
	if (hit < 0 || hit >= MENU_COUNT) return;

	pane.choice = (size_t)hit;
	enter(pane.choice);
}

void video_pane_draw(uint64_t held) {
	switch (pane.view) {
		case View_Live: iptv_pane_draw(held); return;
		case View_Card: media_pane_draw(&pane.card, T(STR_VIDEO_EYEBROW), held); return;
		case View_Menu: break;
	}

	ui_header(T(STR_VIDEO_EYEBROW), T(STR_VIDEO), T(STR_PICK_SOURCE));

	UiRow rows[MENU_COUNT] = {
		[MENU_LIVE] = {
			.name = T(STR_WATCH_LIVE),
			.detail = T(STR_WATCH_LIVE_NOTE),
			.kind = T(STR_KIND_LIVE),
			.accent = true,
		},
		[MENU_CARD] = {
			.name = T(STR_WATCH_CARD),
			.detail = T(STR_WATCH_CARD_NOTE),
			.kind = T(STR_KIND_SD),
			.accent = false,
		},
	};

	ui_rows(rows, MENU_COUNT, pane.choice, MENU_COUNT, 0);

	HINTS[0].label = T(STR_OPEN);
	HINTS[1].label = T(STR_HOME);
	HINTS[2].label = T(STR_QUIT);
	ui_footer(HINTS, COUNT_OF(HINTS), held);
}
