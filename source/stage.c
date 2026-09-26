#include "stage.h"

#include <stdio.h>

#include <SDL2/SDL.h>
#include <switch.h>

#include "i18n.h"
#include "player.h"
#include "ui.h"

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

/* The D-pad nudges and the triggers jump: ten seconds is about one missed line of
 * dialogue, a minute is about one scene. */
#define SEEK_SHORT 10.0
#define SEEK_LONG 60.0

/* How long the title and the hint bar stay up before getting out of the picture's
 * way. Long enough to read which channel this is, short enough not to sit there. */
#define OVERLAY_MS 4000

/* No screen-off here. A picture needs the screen drawing to keep its frame queue
 * moving, so the option is left out rather than offered and quietly ignored. */
static UiHint HINTS[] = {
	{ "A", NULL, HidNpadButton_A, true },
	{ "B", NULL, HidNpadButton_B, false },
	{ "+", NULL, HidNpadButton_Plus, false },
};

static struct {
	bool active;
	uint32_t shown_at;
	char title[128];
} st;

void stage_wake(void) {
	st.shown_at = SDL_GetTicks();
}

void stage_begin(const char *title) {
	snprintf(st.title, sizeof(st.title), "%s", title);
	st.active = true;
	stage_wake();
}

void stage_end(void) {
	player_stop();
	st.active = false;
}

bool stage_active(void) {
	if (!st.active) return false;

	PlayerState state = player_state();
	if (state == PlayerState_Ended || state == PlayerState_Idle) st.active = false;

	return st.active;
}

bool stage_animating(void) {
	return stage_active() && player_state() == PlayerState_Playing && !player_is_paused();
}

static void seek_by(double delta) {
	if (!player_can_seek()) return;

	double target = player_position() + delta;
	double total = player_duration();
	/* Landing exactly on the end would read as a file that stopped by itself. */
	if (total > 0 && target > total - 1.0) target = total - 1.0;
	if (target < 0) target = 0;

	player_seek(target);
}

static bool overlay_up(void);

/**
 * Turns a touch on the scrub bar into a position. Returns false when the finger
 * was somewhere else, so the caller can fall back to simply waking the overlay.
 *
 * Dragging works the same as tapping, one seek per frame the finger moves, which
 * is what makes it feel like pulling the playhead rather than nudging it.
 */
bool stage_scrub(int x, int y) {
	if (!overlay_up() || !player_can_seek()) return false;

	double total = player_duration();
	if (total <= 0) return false;

	UiRect bar = ui_theater_scrub();
	if (x < bar.x || x > bar.x + bar.w || y < bar.y || y > bar.y + bar.h) return false;

	double fraction = (double)(x - bar.x) / (double)bar.w;
	if (fraction < 0) fraction = 0;
	if (fraction > 1.0) fraction = 1.0;

	/* Landing exactly on the end would read as a file that stopped by itself. */
	double target = fraction * total;
	if (target > total - 1.0) target = total - 1.0;

	stage_wake();
	player_seek(target);
	return true;
}

void stage_input(uint64_t down) {
	if (down) stage_wake();

	if (down & HidNpadButton_B) {
		stage_end();
		return;
	}

	if (down & HidNpadButton_A) player_set_paused(!player_is_paused());
	if (down & HidNpadButton_Left) seek_by(-SEEK_SHORT);
	if (down & HidNpadButton_Right) seek_by(SEEK_SHORT);
	if (down & HidNpadButton_ZL) seek_by(-SEEK_LONG);
	if (down & HidNpadButton_ZR) seek_by(SEEK_LONG);
}

static void status_line(char *out, size_t len) {
	switch (player_state()) {
		case PlayerState_Connecting:
			snprintf(out, len, "%s", T(STR_CONNECTING));
			return;
		case PlayerState_Error:
			snprintf(out, len, "%.150s", player_error());
			return;
		default:
			break;
	}

	char now[16];
	ui_format_time(player_position(), now, sizeof(now));
	const char *state = player_is_paused() ? T(STR_PAUSE) : T(STR_PLAYING);

	/* A live channel has no end to count towards, so only the elapsed side is shown. */
	if (player_duration() > 0) {
		char total[16];
		ui_format_time(player_duration(), total, sizeof(total));
		snprintf(out, len, "%s / %s  %s", now, total, state);
	} else {
		snprintf(out, len, "%s  %s", now, state);
	}
}

/** Whether anything is drawn over the picture at all. */
static bool overlay_up(void) {
	/* Anything other than a picture running by itself is something the user is owed
	 * an explanation for, so the words stay up until that stops being true. */
	if (player_state() != PlayerState_Playing || player_is_paused()) return true;

	return SDL_GetTicks() - st.shown_at < OVERLAY_MS;
}

void stage_draw(uint64_t held) {
	player_draw_video(ui_stage());

	if (!overlay_up()) {
		/* Still called with nothing, so the hints stop being tappable along with
		 * being visible. */
		ui_footer(NULL, 0, 0);
		return;
	}

	char status[192];
	status_line(status, sizeof(status));

	double total = player_duration();
	double progress = (total > 0 && player_can_seek()) ? player_position() / total : -1.0;
	ui_theater(st.title, status, progress);

	HINTS[0].label = player_is_paused() ? T(STR_RESUME) : T(STR_PAUSE);
	HINTS[1].label = T(STR_STOP);
	HINTS[2].label = T(STR_QUIT);
	ui_footer(HINTS, COUNT_OF(HINTS), held);
}
