#include "pane_music_net.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include <switch.h>

#include "i18n.h"
#include "keyboard.h"
#include "mediavault.h"
#include "player.h"
#include "settings.h"
#include "ui.h"

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))
#define ROW_CAP 8
#define NOTE_MAX 192

/* The D-pad nudges and the triggers jump, the same as the video transport, so one
 * hand learns one thing. */
#define SEEK_SHORT 10.0
#define SEEK_LONG 60.0

static UiHint LIST_HINTS[] = {
	{ "A", NULL, HidNpadButton_A, true },
	{ "Y", NULL, HidNpadButton_Y, false },
	{ "X", NULL, HidNpadButton_X, false },
	{ "ZR", NULL, HidNpadButton_ZR, false },
	{ "B", NULL, HidNpadButton_B, false },
	{ "+", NULL, HidNpadButton_Plus, false },
};

static UiHint NOW_HINTS[] = {
	{ "A", NULL, HidNpadButton_A, true },
	{ "LR", NULL, HidNpadButton_R, false },
	{ "X", NULL, HidNpadButton_X, false },
	{ "B", NULL, HidNpadButton_B, false },
	{ "+", NULL, HidNpadButton_Plus, false },
};

static struct {
	Settings settings;
	MediaListing found;
	size_t selected;
	size_t scroll;
	char query[MEDIA_SEARCH_MAX];
	char label[MEDIA_SEARCH_MAX + 32];
	char note[NOTE_MAX];
	bool loaded;
	/* The full screen is a view rather than a pane: it is the same section, just
	 * given the whole band because nothing else is worth looking at while it is up. */
	bool expanded;

	/* A copy, not a pointer: the listing is freed and replaced by the next search,
	 * and what is playing has to outlive the list it was picked from. */
	char id[64];
	char title[192];
	char artist[128];
	bool playing;
	size_t playing_at;

	/*
	 * Shuffle picks the next one at random; repeat means repeat this one. Repeating
	 * the whole list is what happens anyway once the end wraps, so a third state
	 * would be a third thing to explain for no new behaviour.
	 */
	bool shuffle;
	bool repeat;
	bool seeded;
} pane;

static void note(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void note(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	vsnprintf(pane.note, sizeof(pane.note), fmt, args);
	va_end(args);

	printf("[music] %s\n", pane.note);
}

/* --- list ---------------------------------------------------------------- */

static void clamp_cursor(void) {
	if (pane.found.count == 0) {
		pane.selected = 0;
		pane.scroll = 0;
		return;
	}

	if (pane.selected >= pane.found.count) pane.selected = pane.found.count - 1;

	size_t rows = ui_tracks_visible();
	if (pane.selected < pane.scroll) pane.scroll = pane.selected;
	if (pane.selected >= pane.scroll + rows) pane.scroll = pane.selected - rows + 1;
}

void music_net_open(void) {
	if (pane.loaded) return;
	pane.loaded = true;

	settings_load(&pane.settings);
	snprintf(pane.label, sizeof(pane.label), "%s", T(STR_TRENDING));

	/* One request, so the section has something in it before anyone has thought of
	 * a search term. */
	ui_message(T(STR_SEARCHING), NULL, NULL, 0);

	char err[160];
	if (!media_trending(&pane.settings, &pane.found, err, sizeof(err))) {
		note("%.150s", err);
	}
}

void music_net_exit(void) {
	media_listing_free(&pane.found);
	pane.loaded = false;
}

static void search(void) {
	char typed[MEDIA_SEARCH_MAX];
	if (!keyboard_prompt(T(STR_SEARCH_HEADER), pane.query, typed, sizeof(typed))) return;

	snprintf(pane.query, sizeof(pane.query), "%s", typed);
	ui_message(T(STR_SEARCHING), pane.query, NULL, 0);

	MediaListing found = {0};
	char err[160];
	if (!media_search(&pane.settings, pane.query, &found, err, sizeof(err))) {
		note("%.150s", err);
		return;
	}

	media_listing_free(&pane.found);
	pane.found = found;
	pane.selected = 0;
	pane.scroll = 0;
	pane.note[0] = '\0';
	snprintf(pane.label, sizeof(pane.label), "%s", pane.query);
}

static bool play_index(size_t index) {
	if (index >= pane.found.count) return false;

	const MediaItem *item = &pane.found.items[index];

	/* A members-only or live entry has no length, and the server can neither proxy
	 * nor merge it. Refusing costs nothing; letting it through costs a wait. */
	if (item->duration <= 0) {
		note("%s", T(STR_LIVE_NO_SUPPORT));
		return false;
	}

	ui_message(T(STR_PREPARING), item->title, NULL, 0);

	char url[640];
	char err[160];
	if (!media_prepare_audio(&pane.settings, item, url, sizeof(url), err, sizeof(err))) {
		note("%.150s", err);
		return false;
	}

	if (!player_play(url, NULL, err, sizeof(err))) {
		note("%.150s", err);
		return false;
	}

	snprintf(pane.id, sizeof(pane.id), "%s", item->id);
	snprintf(pane.title, sizeof(pane.title), "%s", item->title);
	snprintf(pane.artist, sizeof(pane.artist), "%s", item->author ? item->author : "");
	pane.playing = true;
	pane.playing_at = index;
	pane.note[0] = '\0';
	return true;
}

static void play_selected(void) {
	/* Straight to the full screen, the way a music player does: having chosen, the
	 * list has nothing more to offer until the next choice. */
	if (play_index(pane.selected)) pane.expanded = true;
}

/*
 * The next thing to play when this one runs out, or when skip is pressed.
 *
 * A player that stops dead at the end of every song is the thing that makes an app
 * feel like a file browser with sound. This is the whole difference.
 */
static size_t next_index(int direction) {
	size_t count = pane.found.count;
	if (count == 0) return (size_t)-1;

	if (pane.repeat && direction > 0) return pane.playing_at;

	if (pane.shuffle) {
		if (!pane.seeded) {
			srand((unsigned)time(NULL));
			pane.seeded = true;
		}
		/* One track long is not a shuffle, and the loop below would never end. */
		if (count == 1) return 0;

		size_t pick;
		do { pick = (size_t)rand() % count; } while (pick == pane.playing_at);
		return pick;
	}

	if (direction > 0) return pane.playing_at + 1 < count ? pane.playing_at + 1 : (size_t)-1;
	return pane.playing_at > 0 ? pane.playing_at - 1 : (size_t)-1;
}

static void skip(int direction) {
	size_t index = next_index(direction);

	if (index == (size_t)-1) {
		note("%s", T(STR_LIST_END));
		pane.playing = false;
		player_stop();
		return;
	}

	/* Clearing first: a failure part-way leaves nothing claiming to be playing, and
	 * so nothing for the end-of-track check to act on again next frame. */
	pane.playing = false;
	if (play_index(index)) {
		pane.selected = index;
		clamp_cursor();
	}
}

/** Advances when the track has run out. Called every frame the pane is drawn. */
static void tick(void) {
	if (!pane.playing || player_state() != PlayerState_Ended) return;
	skip(1);
}

/* --- transport ------------------------------------------------------------ */

static void seek_by(double delta) {
	if (!player_can_seek()) return;

	double target = player_position() + delta;
	double total = player_duration();
	if (total > 0 && target > total - 1.0) target = total - 1.0;
	if (target < 0) target = 0;

	player_seek(target);
}

static void stop(void) {
	player_stop();
	pane.playing = false;
	pane.expanded = false;
}

/* --- input --------------------------------------------------------------- */

bool music_net_input(uint64_t down) {
	if (pane.expanded) {
		if (down & HidNpadButton_A) player_set_paused(!player_is_paused());
		if (down & HidNpadButton_X) stop();
		if (down & HidNpadButton_L) skip(-1);
		if (down & HidNpadButton_R) skip(1);
		if (down & HidNpadButton_Left) seek_by(-SEEK_SHORT);
		if (down & HidNpadButton_Right) seek_by(SEEK_SHORT);
		if (down & HidNpadButton_ZL) seek_by(-SEEK_LONG);
		if (down & HidNpadButton_ZR) seek_by(SEEK_LONG);
		if (down & HidNpadButton_Y) pane.shuffle = !pane.shuffle;
		if (down & HidNpadButton_Minus) pane.repeat = !pane.repeat;

		/* B goes back to the list without stopping: the point of a music player is
		 * that looking for the next song does not interrupt this one. */
		if (down & HidNpadButton_B) pane.expanded = false;
		return true;
	}

	if (down & HidNpadButton_Down) { pane.selected++; clamp_cursor(); }
	if (down & HidNpadButton_Up) {
		if (pane.selected == 0) {
			/* Up off the top of the list opens what is playing, which is the only
			 * thing above the list on screen. */
			if (pane.playing) pane.expanded = true;
		} else {
			pane.selected--;
			clamp_cursor();
		}
	}

	if (down & HidNpadButton_A) play_selected();
	if (down & HidNpadButton_Y) search();
	if (down & HidNpadButton_X) stop();
	if (down & HidNpadButton_ZR) { if (pane.playing) pane.expanded = true; }
	if (down & HidNpadButton_B) return false;

	return true;
}

void music_net_touch(int x, int y) {
	if (pane.expanded) {
		int control = ui_hit_transport(x, y);
		if (control == 0) { skip(-1); return; }
		if (control == 1) { player_set_paused(!player_is_paused()); return; }
		if (control == 2) { skip(1); return; }

		int chip = ui_hit_music_chip(x, y);
		if (chip == 0) { pane.shuffle = !pane.shuffle; return; }
		if (chip == 1) { pane.repeat = !pane.repeat; return; }

		UiRect bar = ui_music_scrub();
		double total = player_duration();

		if (total > 0 && player_can_seek() &&
				x >= bar.x && x < bar.x + bar.w && y >= bar.y && y < bar.y + bar.h) {
			double f = (double)(x - bar.x) / (double)bar.w;
			if (f < 0) f = 0;
			if (f > 1) f = 1;
			player_seek(f * total > total - 1.0 ? total - 1.0 : f * total);
		}
		return;
	}

	if (pane.playing && ui_hit_now_corner(x, y)) {
		pane.expanded = true;
		return;
	}

	int hit = ui_hit_track(x, y);
	if (hit < 0) return;

	size_t index = pane.scroll + (size_t)hit;
	if (index >= pane.found.count) return;

	pane.selected = index;
	play_selected();
}

bool music_net_animating(void) {
	/* The clock and the playhead move on their own, so the frame has to be redrawn
	 * even when nothing was pressed. */
	PlayerState state = player_state();
	/* Ended counts: that is the frame the next track has to be started on. */
	return state == PlayerState_Playing || state == PlayerState_Connecting ||
			(pane.playing && state == PlayerState_Ended);
}

/* --- drawing ------------------------------------------------------------- */

static const char *state_word(void) {
	switch (player_state()) {
		case PlayerState_Connecting: return T(STR_CONNECTING);
		case PlayerState_Error: return player_error();
		case PlayerState_Playing: return player_is_paused() ? T(STR_PAUSE) : T(STR_PLAYING);
		default: return "";
	}
}

static UiNowPlaying snapshot(void) {
	static char next_line[224];
	next_line[0] = '\0';

	if (pane.playing && !pane.repeat) {
		size_t at = pane.playing_at + 1;
		if (!pane.shuffle && at < pane.found.count) {
			snprintf(next_line, sizeof(next_line), T(STR_NEXT_UP),
					pane.found.items[at].title);
		}
	}

	return (UiNowPlaying){
		.id = pane.playing ? pane.id : NULL,
		.title = pane.playing ? pane.title : NULL,
		.artist = pane.playing ? pane.artist : NULL,
		.detail = pane.playing ? T(STR_LISTEN_ONLINE_NOTE) : NULL,
		.state = state_word(),
		.idle_label = T(STR_MUSIC_IDLE),
		.position = player_position() < 0 ? 0 : player_position(),
		.duration = player_duration(),
		/* The player does not report how much it has read ahead, so the buffered
		 * layer is left out rather than drawn at a number we made up. */
		.buffered = -1.0,
		.paused = player_is_paused(),
		.next = next_line[0] ? next_line : NULL,
		.shuffle = pane.shuffle,
		.repeat = pane.repeat,
		.shuffle_label = T(STR_SHUFFLE),
		.repeat_label = T(STR_REPEAT),
	};
}

void music_net_draw(uint64_t held) {
	tick();

	UiNowPlaying now = snapshot();

	if (pane.expanded) {
		/* No header: the art is the screen, and the 136px an eyebrow and a title
		 * would take comes straight out of the one element doing the work. */
		ui_now_playing(&now);

		NOW_HINTS[0].label = player_is_paused() ? T(STR_RESUME) : T(STR_PAUSE);
		NOW_HINTS[1].label = T(STR_TAB);
		NOW_HINTS[2].label = T(STR_STOP);
		NOW_HINTS[3].label = T(STR_BACK);
		NOW_HINTS[4].label = T(STR_QUIT);
		ui_footer(NOW_HINTS, COUNT_OF(NOW_HINTS), held);
		return;
	}

	/* Past the longest label, which is a search term plus its count. */
	char title[sizeof(pane.label) + 32];
	if (pane.found.count > 0) {
		snprintf(title, sizeof(title), "%s  (%zu)", pane.label, pane.found.count);
	} else {
		snprintf(title, sizeof(title), "%s", pane.label);
	}

	ui_header(T(STR_MUSIC_EYEBROW), title, pane.note);

	if (pane.found.count == 0) {
		ui_empty(T(STR_YT_EMPTY));
	} else {
		size_t visible = ui_tracks_visible();
		if (visible > ROW_CAP) visible = ROW_CAP;
		if (visible > pane.found.count - pane.scroll) visible = pane.found.count - pane.scroll;

		UiTrack rows[ROW_CAP];
		char length[ROW_CAP][16];

		for (size_t i = 0; i < visible; i++) {
			const MediaItem *item = &pane.found.items[pane.scroll + i];
			ui_format_time((double)item->duration, length[i], sizeof(length[i]));

			rows[i] = (UiTrack){
				.id = item->id,
				.title = item->title,
				.artist = item->author,
				.length = item->duration > 0 ? length[i] : "",
			};
		}

		ui_tracks(rows, visible, pane.selected - pane.scroll, pane.scroll, pane.found.count);
	}

	ui_now_corner(&now);

	LIST_HINTS[0].label = T(STR_LISTEN);
	LIST_HINTS[1].label = T(STR_SEARCH);
	LIST_HINTS[2].label = T(STR_STOP);
	LIST_HINTS[3].label = T(STR_NOW_PLAYING);
	LIST_HINTS[4].label = T(STR_BACK);
	LIST_HINTS[5].label = T(STR_QUIT);
	ui_footer(LIST_HINTS, COUNT_OF(LIST_HINTS), held);
}
