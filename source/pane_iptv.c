#include "pane_iptv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <switch.h>

#include "http.h"
#include "i18n.h"
#include "m3u.h"
#include "player.h"
#include "stage.h"
#include "ui.h"
#include "util.h"

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))
#define ROW_CAP 16

/*
 * A checked copy of iptv-org's Indonesian list, kept in the release repo rather than
 * in the NRO so it can be re-checked and replaced without shipping a build. Their
 * list is upstream of this one, but roughly a third of it is dead at any moment --
 * hosts vanish, paths move -- and a row that never opens is worse than no row.
 *
 * tools/probe-iptv.py is what regenerates it: it decode-probes every channel against
 * the exact codec and protocol set build-ffmpeg.sh enables.
 */
#define PLAYLIST_URL \
	"https://raw.githubusercontent.com/Lisvindanu/nxmedia-releases/main/iptv-id.m3u"

/* Several CDNs answer ffmpeg's default "Lavf/.." agent with a 403. */
#define BROWSER_AGENT \
	"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 " \
	"(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"

#define CACHE_DIR "sdmc:/switch/nxmedia"
#define CACHE_PATH CACHE_DIR "/iptv-id.m3u"

/* The whole playlist is a few tens of kilobytes. The ceiling is only here so a host
 * answering with something enormous cannot be turned into an allocation. */
#define CACHE_MAX (2 * 1024 * 1024)

static UiHint HINTS[] = {
	{ "A", NULL, HidNpadButton_A, true },
	{ "Y", NULL, HidNpadButton_Y, false },
	{ "B", NULL, HidNpadButton_B, false },
	{ "+", NULL, HidNpadButton_Plus, false },
};

static struct {
	M3uPlaylist list;
	size_t selected;
	size_t scroll;
	bool loaded;
	char note[160];
} pane;

/* --- the playlist ------------------------------------------------------- */

static void write_cache(const char *text, size_t len) {
	if (!mkdir_p(CACHE_DIR)) return;

	FILE *file = fopen(CACHE_PATH, "wb");
	if (!file) return;

	fwrite(text, 1, len, file);
	fclose(file);
}

static char *read_cache(void) {
	FILE *file = fopen(CACHE_PATH, "rb");
	if (!file) return NULL;

	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	rewind(file);

	if (size <= 0 || size > CACHE_MAX) {
		fclose(file);
		return NULL;
	}

	char *text = malloc((size_t)size + 1);
	if (!text) {
		fclose(file);
		return NULL;
	}

	size_t read = fread(text, 1, (size_t)size, file);
	fclose(file);

	text[read] = '\0';
	return text;
}

static bool load_from_cache(void) {
	char *text = read_cache();
	if (!text) return false;

	char err[160];
	bool ok = m3u_parse(text, &pane.list, err, sizeof(err));
	free(text);
	return ok;
}

static void load(void) {
	m3u_free(&pane.list);
	pane.selected = 0;
	pane.scroll = 0;
	pane.note[0] = '\0';
	pane.loaded = true;

	/* The fetch blocks the loop, so the screen has to say why before it starts. */
	ui_message(T(STR_LOADING_CHANNELS), NULL, NULL, 0);

	char err[160] = {0};
	HttpBuffer body = {0};

	if (http_get(PLAYLIST_URL, NULL, &body, err, sizeof(err)) && body.data) {
		if (m3u_parse(body.data, &pane.list, err, sizeof(err))) {
			write_cache(body.data, body.len);
			http_buffer_free(&body);
			return;
		}
	}
	http_buffer_free(&body);

	/* The card keeps the last list that worked, so losing the network costs the
	 * newest channels rather than the whole pane. */
	if (load_from_cache()) return;

	snprintf(pane.note, sizeof(pane.note), "%.150s", err[0] ? err : T(STR_NO_CHANNELS));
}

void iptv_pane_open(void) {
	if (!pane.loaded) load();
}

void iptv_pane_exit(void) {
	m3u_free(&pane.list);
}

/* --- state -------------------------------------------------------------- */

static void move(int delta) {
	size_t count = pane.list.count;
	if (count == 0) return;

	pane.selected = (pane.selected + count + (size_t)delta) % count;

	size_t rows = ui_rows_visible();
	if (pane.selected < pane.scroll) pane.scroll = pane.selected;
	if (pane.selected >= pane.scroll + rows) pane.scroll = pane.selected - rows + 1;
}

static void watch_selected(void) {
	if (pane.selected >= pane.list.count) return;
	const M3uEntry *channel = &pane.list.items[pane.selected];

	/* Plenty of these CDNs reject ffmpeg's own "Lavf/.." agent outright, so a channel
	 * that does not name one still gets something that looks like a browser. */
	PlayerHeaders headers = {
		.referer = channel->referer,
		.user_agent = channel->user_agent ? channel->user_agent : (char *)BROWSER_AGENT,
	};

	char err[160];
	if (!player_play(channel->url, &headers, err, sizeof(err))) {
		snprintf(pane.note, sizeof(pane.note), "%.150s", err);
		return;
	}

	pane.note[0] = '\0';
	stage_begin(channel->name);
}

bool iptv_pane_input(uint64_t down) {
	if (down & HidNpadButton_Down) move(1);
	if (down & HidNpadButton_Up) move(-1);
	if (down & HidNpadButton_A) watch_selected();
	if (down & HidNpadButton_Y) load();
	if (down & HidNpadButton_B) return false;

	return true;
}

void iptv_pane_touch(int x, int y) {
	int hit = ui_hit_row(x, y);
	if (hit < 0) return;

	size_t index = pane.scroll + (size_t)hit;
	if (index >= pane.list.count) return;

	pane.selected = index;
	watch_selected();
}

/* --- drawing ------------------------------------------------------------ */

void iptv_pane_draw(uint64_t held) {
	char title[64];
	snprintf(title, sizeof(title), "%s  (%zu)", T(STR_WATCH_LIVE), pane.list.count);

	ui_header(T(STR_LIVE_EYEBROW), pane.list.count ? title : T(STR_WATCH_LIVE),
			pane.note[0] ? pane.note : T(STR_LIVE_HINT));

	if (pane.list.count == 0) {
		ui_empty(T(STR_NO_CHANNELS));
	} else {
		size_t visible = ui_rows_visible();
		if (visible > ROW_CAP) visible = ROW_CAP;
		if (visible > pane.list.count - pane.scroll) visible = pane.list.count - pane.scroll;

		UiRow rows[ROW_CAP];
		for (size_t i = 0; i < visible; i++) {
			const M3uEntry *channel = &pane.list.items[pane.scroll + i];
			rows[i] = (UiRow){
				.name = channel->name,
				/* What the playlist filed it under -- news, sport, religious. */
				.detail = channel->group,
				.kind = T(STR_KIND_LIVE),
				.accent = true,
			};
		}

		ui_rows(rows, visible, pane.selected - pane.scroll, pane.list.count, pane.scroll);
	}

	HINTS[0].label = T(STR_WATCH);
	HINTS[1].label = T(STR_REFRESH);
	HINTS[2].label = T(STR_BACK);
	HINTS[3].label = T(STR_QUIT);
	ui_footer(HINTS, COUNT_OF(HINTS), held);
}
