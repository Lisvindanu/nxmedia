#include <stdio.h>
#include <string.h>

#include <switch.h>

#include "globe.h"
#include "http.h"
#include "i18n.h"
#include "pane_media.h"
#include "pane_video.h"
#include "player.h"
#include "power.h"
#include "radio.h"
#include "stage.h"
#include "touch.h"
#include "ui.h"
#include "update.h"
#include "world.h"

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

#define SECTION_MUSIC 1
#define SECTION_VIDEO 2
#define SECTION_SETTINGS 3

#define SETTING_LANGUAGE 0
#define SETTING_UPDATE 1
#define SETTING_COUNT 2

/* How far the stick has to lean before it counts as a push, and how fast a full
 * lean spins the globe. */
#define STICK_DEAD 6000
#define STICK_SPAN 32000
#define STICK_PX 9

/* Text is filled in by refresh_strings() rather than written here, so a language
 * change is one pass over these tables instead of a rebuild of every screen. */
static UiTile TILES[] = {
	{ NULL, NULL, true },
	{ NULL, NULL, true },
	{ NULL, NULL, true },
	{ NULL, NULL, true },
};

/* The bento is irregular, so each move is spelled out rather than derived from a
 * grid that does not exist. */
static const size_t HOME_LEFT[] = { 0, 0, 0, 2 };
static const size_t HOME_RIGHT[] = { 1, 1, 3, 3 };
static const size_t HOME_UP[] = { 0, 1, 1, 1 };
static const size_t HOME_DOWN[] = { 0, 2, 2, 3 };

static UiHint HINT_HOME[] = {
	{ "A", NULL, HidNpadButton_A, true },
	{ "X", NULL, HidNpadButton_X, false },
	{ "+", NULL, HidNpadButton_Plus, false },
};

static UiHint HINT_RADIO[] = {
	{ "A", NULL, HidNpadButton_A, true },
	{ "Y", NULL, HidNpadButton_Y, false },
	{ "X", NULL, HidNpadButton_X, false },
	{ "B", NULL, HidNpadButton_B, false },
	{ "+", NULL, HidNpadButton_Plus, false },
};

static UiHint HINT_SETTINGS[] = {
	{ "A", NULL, HidNpadButton_A, true },
	{ "B", NULL, HidNpadButton_B, false },
	{ "+", NULL, HidNpadButton_Plus, false },
};

/* The inputs the hint bar has no room for and nobody finds by accident. Buttons
 * stay out of it: those are already spelled out along the bottom. */
static UiHint RADIO_KEYS[] = {
	{ NULL, NULL, 0, false },
	{ NULL, NULL, 0, false },
	{ NULL, NULL, 0, false },
	{ NULL, NULL, 0, false },
};

static UiHint HINT_FATAL[] = {
	{ "+", NULL, HidNpadButton_Plus, true },
};

static void refresh_strings(void) {
	TILES[0].title = T(STR_RADIO);
	TILES[0].blurb = T(STR_RADIO_BLURB);
	TILES[1].title = T(STR_MUSIC);
	TILES[1].blurb = T(STR_MUSIC_BLURB);
	TILES[2].title = T(STR_VIDEO);
	TILES[2].blurb = T(STR_VIDEO_BLURB);
	TILES[3].title = T(STR_SETTINGS);
	TILES[3].blurb = T(STR_SETTINGS_BLURB);

	HINT_HOME[0].label = T(STR_OPEN);
	HINT_HOME[1].label = T(STR_SCREEN_OFF);
	HINT_HOME[2].label = T(STR_QUIT);

	HINT_RADIO[0].label = T(STR_LISTEN);
	HINT_RADIO[1].label = T(STR_STOP);
	HINT_RADIO[2].label = T(STR_SCREEN_OFF);
	HINT_RADIO[3].label = T(STR_HOME);
	HINT_RADIO[4].label = T(STR_QUIT);

	HINT_SETTINGS[1].label = T(STR_HOME);
	HINT_SETTINGS[2].label = T(STR_QUIT);

	RADIO_KEYS[0].button = T(STR_KEY_STICK);
	RADIO_KEYS[0].label = T(STR_DO_SPIN);
	RADIO_KEYS[1].button = T(STR_KEY_TOUCH);
	RADIO_KEYS[1].label = T(STR_DO_PICK);
	RADIO_KEYS[2].button = T(STR_KEY_ZOOM);
	RADIO_KEYS[2].label = T(STR_DO_ZOOM);
	RADIO_KEYS[3].button = T(STR_KEY_DPAD);
	RADIO_KEYS[3].label = T(STR_DO_STATION);

	HINT_FATAL[0].label = T(STR_QUIT);
}

typedef struct {
	/** True while the launcher is up; `section` is then the tile under the cursor. */
	bool home;
	size_t section;
	Globe globe;
	size_t picked;
	/** Set while the camera is flying to a country the user chose. */
	bool gliding;
	size_t station;
	/** The card browser behind the Music tile. Video keeps its own, inside its pane. */
	MediaPane music;
	size_t setting;
	UpdateInfo update;
	/* What the Update row reads right now: a stage, an error, or a new version. */
	char update_note[96];
	int update_percent;
	char status[160];
	/** This frame's button mask, so the hint bar can show what is under a thumb. */
	uint64_t held;
	bool dirty;
} App;

/* libnx allows 1-4 concurrent ssl sessions; 3 is the stock recommendation and
 * leaves room for a download running alongside a stream. */
#define SSL_SESSIONS 3

/* libnx's stock TCP window is small, and a stream that cannot keep the ring fed
 * stutters no matter how good the decoder is. */
static bool init_socket(void) {
	SocketInitConfig config = *socketGetDefaultInitConfig();
	config.tcp_rx_buf_size = 0x20000;
	config.tcp_rx_buf_max_size = 0x100000;
	config.sb_efficiency = 8;

	if (R_SUCCEEDED(socketInitialize(&config))) return true;

	/* A wider window needs a larger transfer memory, which the system may refuse. */
	return R_SUCCEEDED(socketInitializeDefault());
}

static void draw_home(const App *app) {
	ui_home(TILES, COUNT_OF(TILES), app->section);

	/* The hero tile is the only preview of what Radio is, so the globe in it is the
	 * real one, still facing wherever the user left it. */
	UiRect slot = ui_home_globe_slot();
	Globe mini = app->globe;
	mini.zoom = 1.0f;
	globe_place(&mini, slot.x, slot.y, slot.w, slot.h, 132);
	globe_draw(&mini, app->picked);

	const WorldCountry *country = world_country(app->picked);
	if (country) globe_mark(&mini, country->lat, country->lon, true);

	ui_footer(HINT_HOME, COUNT_OF(HINT_HOME), app->held);
	ui_present();
}

static void draw(App *app) {
	if (app->home) {
		draw_home(app);
		return;
	}

	ui_begin();

	/* A picture takes the whole screen wherever it was started from, so it is checked
	 * before the section it belongs to rather than inside each one. */
	if (stage_active()) {
		stage_draw(app->held);
	} else if (app->section == 0) {
		const WorldCountry *country = world_country(app->picked);
		ui_header(T(STR_RADIO_EYEBROW), country ? country->name : T(STR_PICK_COUNTRY),
				power_is_screen_off() ? T(STR_SCREEN_OFF_NOTE)
				: app->status[0] ? app->status
				: T(STR_RADIO_PROMPT));

		globe_draw(&app->globe, app->picked);
		if (country) globe_mark(&app->globe, country->lat, country->lon, true);

		/* Drawn over the globe, not beside it, so zooming in never buries it. The
		 * left gutter is empty at rest anyway. */
		int legend_h = ui_legend_height(COUNT_OF(RADIO_KEYS));
		ui_legend(RADIO_KEYS, COUNT_OF(RADIO_KEYS), app->globe.view_x,
				app->globe.view_y + (app->globe.view_h - legend_h) / 2);

		ui_footer(HINT_RADIO, COUNT_OF(HINT_RADIO), app->held);
	} else if (app->section == SECTION_MUSIC) {
		media_pane_draw(&app->music, T(STR_MUSIC_EYEBROW), app->held);
	} else if (app->section == SECTION_VIDEO) {
		video_pane_draw(app->held);
	} else {
		char version[64];
		snprintf(version, sizeof(version), "v%s", update_version());

		ui_header(T(STR_SETTINGS_EYEBROW), T(STR_SETTINGS), T(STR_SETTINGS_HINT));
		ui_setting_row(SETTING_LANGUAGE, T(STR_LANGUAGE), lang_name(lang_get()),
				app->setting == SETTING_LANGUAGE);
		ui_setting_row(SETTING_UPDATE, T(STR_UPDATE),
				app->update_note[0] ? app->update_note : version,
				app->setting == SETTING_UPDATE);

		/* A does a different thing on each row, so the bar has to name the one the
		 * cursor is actually on. */
		HINT_SETTINGS[0].label = app->setting == SETTING_LANGUAGE ? T(STR_SWITCH)
				: app->update.available ? T(STR_INSTALL) : T(STR_CHECK_UPDATE);

		ui_footer(HINT_SETTINGS, COUNT_OF(HINT_SETTINGS), app->held);
	}

	ui_present();
}

/* The download blocks the loop, so the frame has to be pushed from in here or the
 * console looks hung for the length of a several-megabyte transfer. */
static bool update_progress(void *user, int64_t done, int64_t total) {
	App *app = user;
	int percent = total > 0 ? (int)((done * 100) / total) : 0;

	if (percent != app->update_percent) {
		app->update_percent = percent;
		snprintf(app->update_note, sizeof(app->update_note), "%s %d%%",
				T(STR_DOWNLOADING), percent);
		draw(app);
	}

	/* Quitting mid-transfer leaves the .part file, which the next attempt resumes. */
	return appletMainLoop();
}

/* One button drives the whole flow: the first press asks, the second installs. */
static void run_update_step(App *app) {
	char err[96] = {0};

	if (app->update.available) {
		app->update_percent = -1;
		if (update_apply(&app->update, update_progress, app, err, sizeof(err))) {
			app->update.available = false;
			snprintf(app->update_note, sizeof(app->update_note), "%s", T(STR_UPDATE_DONE));
		} else {
			snprintf(app->update_note, sizeof(app->update_note), "%s", err);
		}
		return;
	}

	snprintf(app->update_note, sizeof(app->update_note), "%s", T(STR_CHECKING));
	draw(app);

	if (!update_check(&app->update, err, sizeof(err))) {
		snprintf(app->update_note, sizeof(app->update_note), "%s", err);
	} else if (app->update.available) {
		snprintf(app->update_note, sizeof(app->update_note), "%s  %s",
				app->update.latest, T(STR_UPDATE_READY));
	} else {
		snprintf(app->update_note, sizeof(app->update_note), "%s", T(STR_UP_TO_DATE));
	}
}

/* Pulls the analog stick into the same pixel-delta shape a finger drag arrives as,
 * so the globe only ever learns about one kind of push. */
static bool stick_drag(const PadState *pad, int *dx, int *dy) {
	HidAnalogStickState stick = padGetStickPos((PadState *)pad, 0);

	*dx = 0;
	*dy = 0;

	if (stick.x > STICK_DEAD || stick.x < -STICK_DEAD) {
		*dx = stick.x * STICK_PX / STICK_SPAN;
	}
	if (stick.y > STICK_DEAD || stick.y < -STICK_DEAD) {
		*dy = stick.y * STICK_PX / STICK_SPAN;
	}

	return *dx || *dy;
}

static void refresh_status(App *app) {
	const RadioStation *station = radio_station(app->station);
	if (!station) {
		snprintf(app->status, sizeof(app->status), "%s", T(STR_NO_STATIONS));
		return;
	}

	const char *state = T(STR_READY);
	switch (player_state()) {
		case PlayerState_Connecting: state = T(STR_CONNECTING); break;
		case PlayerState_Playing: state = T(STR_PLAYING); break;
		case PlayerState_Error: state = player_error(); break;
		case PlayerState_Ended:
		case PlayerState_Idle: break;
	}

	snprintf(app->status, sizeof(app->status), "%zu/%zu  %s  %d kbps  %s",
			app->station + 1, radio_count(), station->name, station->bitrate, state);
}

/* The fetch is synchronous, so the globe freezes for as long as it takes. It is a
 * single small request and the loading frame makes the pause legible; moving it
 * off-thread only earns its keep once there is a list to scroll while it runs. */
static void select_country(App *app, size_t index) {
	if (index == SIZE_MAX) return;
	app->picked = index;
	app->gliding = true;
	app->station = 0;

	const WorldCountry *country = world_country(index);
	ui_message(T(STR_LOADING_STATIONS), country->name, NULL, 0);

	char err[256];
	if (!radio_fetch(country->iso, err, sizeof(err))) {
		snprintf(app->status, sizeof(app->status), "%.150s", err);
		return;
	}

	refresh_status(app);
}

static void play_current(App *app) {
	const RadioStation *station = radio_station(app->station);
	if (!station) return;

	char err[256];
	if (!player_play(station->url, NULL, err, sizeof(err))) {
		snprintf(app->status, sizeof(app->status), "%.150s", err);
		return;
	}

	refresh_status(app);
}

static void step_station(App *app, int delta) {
	size_t count = radio_count();
	if (count == 0) return;

	app->station = (app->station + count + (size_t)delta) % count;
	refresh_status(app);
}

int main(int argc, char **argv) {
	char err[256] = {0};

	/* hbloader passes the path this build was launched from, which is where a
	 * downloaded build has to be written back over. */
	update_init(argc > 0 ? argv[0] : NULL);

	lang_load();
	refresh_strings();

	if (!ui_init(err, sizeof(err))) {
		consoleInit(NULL);
		printf("ui_init: %s\n", err);
		consoleUpdate(NULL);
		svcSleepThread(4000000000ULL);
		return 1;
	}

	if (R_FAILED(romfsInit())) {
		ui_message(T(STR_ROMFS_FAIL), NULL, HINT_FATAL, COUNT_OF(HINT_FATAL));
		svcSleepThread(4000000000ULL);
		ui_exit();
		return 1;
	}

	ui_message(T(STR_LOADING_MAP), NULL, NULL, 0);
	if (!world_load("romfs:/world.bin", err, sizeof(err))) {
		ui_message(T(STR_MAP_FAIL), err, HINT_FATAL, COUNT_OF(HINT_FATAL));
		svcSleepThread(5000000000ULL);
		romfsExit();
		ui_exit();
		return 1;
	}

	if (!init_socket()) {
		ui_message(T(STR_NET_FAIL), T(STR_NET_FAIL_WHY), HINT_FATAL, COUNT_OF(HINT_FATAL));
		svcSleepThread(5000000000ULL);
		world_free();
		romfsExit();
		ui_exit();
		return 1;
	}

	/* ffmpeg's TLS backend is the ssl service, and it only ever calls
	 * sslCreateContext -- bringing the service up is left to the app. Without this
	 * every https:// station fails; plain http:// ones still play, so a refusal here
	 * is not worth blocking startup over. */
	sslInitialize(SSL_SESSIONS);

	/* The app owns the screen, so printf is the only channel left for watching the
	 * stream worker. Harmlessly does nothing when launched from the SD card. */
	nxlinkStdio();

	if (!http_init(err, sizeof(err)) || !player_init(err, sizeof(err))) {
		ui_message(T(STR_AUDIO_FAIL), err, HINT_FATAL, COUNT_OF(HINT_FATAL));
		svcSleepThread(5000000000ULL);
		sslExit();
		socketExit();
		world_free();
		romfsExit();
		ui_exit();
		return 1;
	}

	power_init();
	touch_init();

	PadState pad;
	padConfigureInput(1, HidNpadStyleSet_NpadStandard);
	padInitializeDefault(&pad);

	App app = { .home = true, .picked = SIZE_MAX, .dirty = true };
	PlayerState last_state = PlayerState_Idle;
	globe_init(&app.globe);

	while (appletMainLoop()) {
		padUpdate(&pad);
		uint64_t down = padGetButtonsDown(&pad);

		/* The hint bar lights the button being held, so a change in the mask is a
		 * change on screen even when nothing else moved. */
		uint64_t held = padGetButtons(&pad);
		if (held != app.held) {
			app.held = held;
			app.dirty = true;
		}

		TouchEvent touch = touch_poll();

		/* Any input is a sign someone is looking again, so the panel comes back
		 * before the press is acted on. */
		if ((down || touch.kind != TOUCH_NONE) && power_is_screen_off()) {
			power_set_screen_off(false);
			app.dirty = true;
			continue;
		}

		if (touch.kind == TOUCH_TAP) {
			if (stage_active()) {
				/* Nothing under the picture is reachable, but the bar along the
				 * bottom still is: it is drawn over it, and a tap anywhere else
				 * brings it back once it has hidden itself. */
				stage_wake();
				app.dirty = true;
			} else if (app.home) {
				int hit = ui_hit_home(touch.x, touch.y);
				if (hit >= 0) {
					/* A tap picks and opens in one go; the cursor still moves so the
					 * ring is where it should be if the user comes back. */
					app.section = (size_t)hit;
					down |= HidNpadButton_A;
					app.dirty = true;
				}
			} else if (app.section == 0) {
				float lat, lon;
				if (globe_pick(&app.globe, touch.x, touch.y, &lat, &lon)) {
					select_country(&app, world_locate(lon, lat));
					app.dirty = true;
				}
			} else if (app.section == SECTION_MUSIC) {
				media_pane_touch(&app.music, touch.x, touch.y);
				app.dirty = true;
			} else if (app.section == SECTION_VIDEO) {
				video_pane_touch(touch.x, touch.y);
				app.dirty = true;
			}
			down |= ui_hit_footer(touch.x, touch.y);
		}

		if (touch.kind == TOUCH_DRAG && !app.home && app.section == 0) {
			globe_drag(&app.globe, touch.dx, touch.dy);
			app.gliding = false;
			app.dirty = true;
		}

		if (down & HidNpadButton_Plus) break;

		if (down & HidNpadButton_X) {
			/* A dark screen stops the frame queue being drained, which corners the
			 * decoder and takes the sound down with it. Sound on its own is fine. */
			if (!player_has_video()) power_set_screen_off(true);
			app.dirty = true;
		}

		if (stage_active()) {
			/* The picture swallows everything while it is up, so the pane underneath
			 * never sees the press that leaves it. */
			stage_input(down);
			if (down) app.dirty = true;
		} else if (app.home) {
			if (down & HidNpadButton_Left) app.section = HOME_LEFT[app.section];
			if (down & HidNpadButton_Right) app.section = HOME_RIGHT[app.section];
			if (down & HidNpadButton_Up) app.section = HOME_UP[app.section];
			if (down & HidNpadButton_Down) app.section = HOME_DOWN[app.section];
			if (down & HidNpadButton_A) {
				app.home = false;
				if (app.section == SECTION_MUSIC) media_pane_open(&app.music, LibraryKind_Audio);
				if (app.section == SECTION_VIDEO) video_pane_open();
			}
			if (down) app.dirty = true;
		} else if (app.section == SECTION_MUSIC) {
			/* B walks back up the card first and only gives up the section once it is
			 * standing at the root. */
			if (!media_pane_input(&app.music, down)) app.home = true;
			if (down) app.dirty = true;
		} else if (app.section == SECTION_VIDEO) {
			/* The pane spends B walking back up the card, so it only reaches the home
			 * screen once there is nowhere further to go. */
			if (!video_pane_input(down)) app.home = true;
			if (down) app.dirty = true;
		} else {
			if (down & HidNpadButton_B) {
				app.home = true;
				app.dirty = true;
			}

			if (app.section == 0) {
				if (down & HidNpadButton_A) {
					play_current(&app);
					app.dirty = true;
				}

				if (down & HidNpadButton_Y) {
					player_stop();
					refresh_status(&app);
					app.dirty = true;
				}

				if (down & HidNpadButton_Down) {
					step_station(&app, 1);
					app.dirty = true;
				}
				if (down & HidNpadButton_Up) {
					step_station(&app, -1);
					app.dirty = true;
				}
			} else if (app.section == SECTION_SETTINGS) {
				if (down & (HidNpadButton_Down | HidNpadButton_Up)) {
					app.setting = (down & HidNpadButton_Down)
							? (app.setting + 1) % SETTING_COUNT
							: (app.setting + SETTING_COUNT - 1) % SETTING_COUNT;
					app.dirty = true;
				}

				if (down & HidNpadButton_A) {
					if (app.setting == SETTING_LANGUAGE) {
						lang_set(lang_get() == LANG_EN ? LANG_ID : LANG_EN);
						refresh_strings();
						/* The status line was rendered in the old language and is held
						 * as a formatted string, so it has to be built again rather
						 * than repointed. */
						if (radio_count()) refresh_status(&app);
					} else {
						run_update_step(&app);
					}
					app.dirty = true;
				}
			}
		}

		/* The worker moves through connecting, playing and error on its own, so the
		 * header has to follow it rather than only reacting to button presses. */
		PlayerState state = player_state();
		if (state != last_state) {
			last_state = state;
			/* Holding off auto-sleep and dimming is what keeps a stream alive once
			 * the user stops touching anything, so it tracks the stream, not the UI. */
			power_set_playing(state == PlayerState_Connecting || state == PlayerState_Playing);
			refresh_status(&app);
			app.dirty = true;
		}

		if (!app.home && app.section == 0) {
			/* Held, not tapped: zoom reads as a continuous push rather than steps. */
			if (held & HidNpadButton_ZR) {
				globe_zoom(&app.globe, GLOBE_ZOOM_STEP);
				app.dirty = true;
			}
			if (held & HidNpadButton_ZL) {
				globe_zoom(&app.globe, 1.0f / GLOBE_ZOOM_STEP);
				app.dirty = true;
			}

			int dx, dy;
			if (stick_drag(&pad, &dx, &dy)) {
				globe_drag(&app.globe, dx, dy);
				app.gliding = false;
				app.dirty = true;
			}

			if (app.gliding) {
				const WorldCountry *country = world_country(app.picked);
				app.gliding = globe_glide(&app.globe, country->lat, country->lon);
				app.dirty = true;
			}
		}

		/* A moving picture has to be pushed frame after frame, so it overrides the
		 * loop's usual rule of only drawing when something was pressed. */
		if (stage_animating()) app.dirty = true;

		if (app.dirty && !power_is_screen_off()) {
			draw(&app);
			app.dirty = false;
		} else {
			svcSleepThread(16000000ULL);
		}
	}

	power_exit();
	player_exit();
	video_pane_exit();
	media_pane_exit(&app.music);
	radio_free();
	http_exit();
	sslExit();
	socketExit();
	world_free();
	romfsExit();
	ui_exit();
	return 0;
}
