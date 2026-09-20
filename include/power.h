#pragma once

#include <stdbool.h>

/*
 * Keeping audio alive while the console looks idle. Three separate system flags
 * have to agree before the OS stops interfering: media playback state, auto sleep,
 * and focus handling. Turning off the backlight is then our own choice on top.
 */

void power_init(void);
void power_exit(void);

/**
 * Tells the OS a stream is playing, which suppresses screen dimming and auto
 * sleep, and asks not to be suspended when the HOME menu takes focus. Safe to
 * call repeatedly with the same value.
 */
void power_set_playing(bool playing);

/**
 * Turns the handheld backlight off while the app keeps running. Only meaningful
 * while playing; the panel comes back on any button press. Docked output is not
 * affected.
 */
void power_set_screen_off(bool off);

bool power_is_screen_off(void);

/** True while the HOME menu or sleep has taken focus away from us. */
bool power_is_backgrounded(void);
