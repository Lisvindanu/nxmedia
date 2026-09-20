#pragma once

typedef enum {
	TOUCH_NONE,
	TOUCH_TAP,
	TOUCH_DRAG,
} TouchKind;

typedef struct {
	TouchKind kind;
	int x;
	int y;
	/** Pixels the finger has travelled since the last poll. Drag only. */
	int dx;
	int dy;
} TouchEvent;

void touch_init(void);

/** Reads the panel once and reports at most one gesture. Call once per frame. */
TouchEvent touch_poll(void);
