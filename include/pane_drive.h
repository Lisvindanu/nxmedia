#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "drive.h"

/*
 * Browsing Google Drive for something to play.
 *
 * Folders are walked by id rather than by path -- Drive has no paths -- so the
 * way back up is a stack of the folders entered rather than a string to trim.
 * Only the names are kept alongside, for the header to say where this is.
 */

#define DRIVE_STACK_MAX 16
#define DRIVE_NAME_MAX 96

typedef struct {
	char id[DRIVE_ID_MAX];
	char name[DRIVE_NAME_MAX];
} DriveCrumb;

typedef struct {
	/* Depth zero is My Drive, which is the one folder with no crumb of its own. */
	DriveCrumb stack[DRIVE_STACK_MAX];
	size_t depth;

	DriveListing listing;
	size_t selected;
	size_t scroll;
	bool opened;

	/*
	 * A search replaces what the list shows without disturbing the folder stack, so
	 * leaving the results puts the user back exactly where they were browsing.
	 */
	bool searching;
	char query[DRIVE_SEARCH_MAX];

	/** What is playing, for the header to keep naming after the list moves on. */
	char title[128];
	/** Whatever went wrong last, shown in place of the hint line. */
	char note[192];
} DrivePane;

void drive_pane_open(DrivePane *pane);
void drive_pane_exit(DrivePane *pane);

/** Handles this frame's presses. False means B went unused: leave the section. */
bool drive_pane_input(DrivePane *pane, uint64_t down);

void drive_pane_touch(DrivePane *pane, int x, int y);

void drive_pane_draw(DrivePane *pane, const char *eyebrow, uint64_t held);
