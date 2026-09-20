#include "touch.h"

#include <stdbool.h>
#include <stdlib.h>
#include <switch.h>

/* How far a finger may wander and still count as a tap. The panel reports small
 * movements even from a finger held still, so without some slack every tap would
 * turn into a scroll. */
#define TAP_SLOP 14

typedef struct {
	bool down;
	bool dragging;
	int start_x;
	int start_y;
	int last_x;
	int last_y;
} TouchState;

static TouchState touch;

void touch_init(void) {
	hidInitializeTouchScreen();
	touch = (TouchState){0};
}

TouchEvent touch_poll(void) {
	TouchEvent event = { .kind = TOUCH_NONE };

	HidTouchScreenState screen = {0};
	bool held = hidGetTouchScreenStates(&screen, 1) > 0 && screen.count > 0;

	if (!held) {
		/* A tap is reported on release, and only if the finger never wandered, so a
		 * press that turns into a scroll does not also fire on the row it began on. */
		if (touch.down && !touch.dragging) {
			event.kind = TOUCH_TAP;
			event.x = touch.start_x;
			event.y = touch.start_y;
		}

		touch.down = false;
		return event;
	}

	int x = (int)screen.touches[0].x;
	int y = (int)screen.touches[0].y;

	if (!touch.down) {
		touch = (TouchState){ .down = true, .start_x = x, .start_y = y,
				.last_x = x, .last_y = y };
		return event;
	}

	if (!touch.dragging
			&& (abs(x - touch.start_x) > TAP_SLOP || abs(y - touch.start_y) > TAP_SLOP)) {
		touch.dragging = true;
	}

	if (touch.dragging && (x != touch.last_x || y != touch.last_y)) {
		event.kind = TOUCH_DRAG;
		event.x = x;
		event.y = y;
		event.dx = x - touch.last_x;
		event.dy = y - touch.last_y;
		touch.last_x = x;
		touch.last_y = y;
	}

	return event;
}
