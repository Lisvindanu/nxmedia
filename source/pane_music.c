#include "pane_music.h"

#include <switch.h>

#include "i18n.h"
#include "pane_music_net.h"
#include "ui.h"

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

typedef enum {
	View_Menu,
	View_Card,
	View_Online,
} View;

#define MENU_CARD 0
#define MENU_ONLINE 1
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

void music_pane_open(void) {
	/* Straight back to whichever half was being used last: the choice is only
	 * interesting the first time. */
	if (pane.view == View_Card) media_pane_open(&pane.card, LibraryKind_Audio);
	if (pane.view == View_Online) music_net_open();
}

void music_pane_exit(void) {
	media_pane_exit(&pane.card);
	music_net_exit();
}

static void enter(size_t choice) {
	if (choice == MENU_ONLINE) {
		pane.view = View_Online;
		music_net_open();
	} else {
		pane.view = View_Card;
		media_pane_open(&pane.card, LibraryKind_Audio);
	}
}

bool music_pane_input(uint64_t down) {
	switch (pane.view) {
		case View_Card:
			if (media_pane_input(&pane.card, down)) return true;
			pane.view = View_Menu;
			return true;

		case View_Online:
			if (music_net_input(down)) return true;
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

bool music_pane_animating(void) {
	/* Only the online half: the card browser has a clock in its header too, but it
	 * is already redrawn by the input that got you there. */
	return pane.view == View_Online && music_net_animating();
}

void music_pane_touch(int x, int y) {
	switch (pane.view) {
		case View_Card: media_pane_touch(&pane.card, x, y); return;
		case View_Online: music_net_touch(x, y); return;
		case View_Menu: break;
	}

	int hit = ui_hit_row(x, y);
	if (hit < 0 || hit >= MENU_COUNT) return;

	pane.choice = (size_t)hit;
	enter(pane.choice);
}

void music_pane_draw(uint64_t held) {
	switch (pane.view) {
		case View_Card: media_pane_draw(&pane.card, T(STR_MUSIC_EYEBROW), held); return;
		case View_Online: music_net_draw(held); return;
		case View_Menu: break;
	}

	ui_header(T(STR_MUSIC_EYEBROW), T(STR_MUSIC), T(STR_PICK_SOURCE));

	UiRow rows[MENU_COUNT] = {
		[MENU_CARD] = {
			.name = T(STR_LISTEN_CARD),
			.detail = T(STR_LISTEN_CARD_NOTE),
			.kind = T(STR_KIND_SD),
			.accent = false,
		},
		[MENU_ONLINE] = {
			.name = T(STR_LISTEN_ONLINE),
			.detail = T(STR_LISTEN_ONLINE_NOTE),
			.kind = T(STR_KIND_ONLINE),
			.accent = true,
		},
	};

	ui_rows(rows, MENU_COUNT, pane.choice, MENU_COUNT, 0);

	HINTS[0].label = T(STR_OPEN);
	HINTS[1].label = T(STR_HOME);
	HINTS[2].label = T(STR_QUIT);
	ui_footer(HINTS, COUNT_OF(HINTS), held);
}
