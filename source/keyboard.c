#include "keyboard.h"

#include <switch.h>

bool keyboard_prompt(const char *header, const char *initial, char *out, size_t out_len) {
	out[0] = '\0';

	SwkbdConfig keyboard;
	if (R_FAILED(swkbdCreate(&keyboard, 0))) return false;

	swkbdConfigMakePresetDefault(&keyboard);
	swkbdConfigSetHeaderText(&keyboard, header);
	swkbdConfigSetInitialText(&keyboard, initial ? initial : "");

	/* The limit the keyboard enforces counts characters, while out_len counts bytes,
	 * and one character can be four of them. Budgeting for the worst case is what
	 * keeps a term of accented or CJK text from being cut mid-character on the way
	 * back, which would leave invalid UTF-8 in the query. */
	swkbdConfigSetStringLenMax(&keyboard, (u32)((out_len - 1) / 4));

	Result rc = swkbdShow(&keyboard, out, out_len);
	swkbdClose(&keyboard);

	/* Cancelling still succeeds, it just yields nothing, which is not a search term. */
	return R_SUCCEEDED(rc) && out[0] != '\0';
}
