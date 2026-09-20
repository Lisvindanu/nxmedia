#include "power.h"

#include <switch.h>

static bool playing;
static bool screen_off;
static bool backgrounded;

static AppletHookCookie focus_cookie;

static void on_applet_hook(AppletHookType hook, void *user) {
	(void)user;
	if (hook != AppletHookType_OnFocusState) return;
	backgrounded = appletGetFocusState() == AppletFocusState_Background;
}

void power_init(void) {
	appletHook(&focus_cookie, on_applet_hook, NULL);

	/* NoSuspend is what lets the stream survive the HOME menu. Without it the
	 * process is frozen the moment focus is lost, and audio stops mid-buffer. */
	appletSetFocusHandlingMode(AppletFocusHandlingMode_NoSuspend);
}

void power_exit(void) {
	power_set_screen_off(false);
	power_set_playing(false);
	appletSetFocusHandlingMode(AppletFocusHandlingMode_SuspendHomeSleep);
	appletUnhook(&focus_cookie);
}

void power_set_playing(bool value) {
	if (value == playing) return;
	playing = value;

	appletSetMediaPlaybackState(playing);
	appletSetAutoSleepDisabled(playing);

	/* Without the extension the system still counts us as idle after a few
	 * minutes of no input, which is exactly what listening looks like. */
	appletSetIdleTimeDetectionExtension(playing ? AppletIdleTimeDetectionExtension_Extended
	                                            : AppletIdleTimeDetectionExtension_None);

	if (!playing) power_set_screen_off(false);
}

void power_set_screen_off(bool off) {
	if (off == screen_off) return;
	screen_off = off;
	appletSetLcdBacklightOffEnabled(screen_off);
}

bool power_is_screen_off(void) {
	return screen_off;
}

bool power_is_backgrounded(void) {
	return backgrounded;
}
