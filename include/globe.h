#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "projection.h"

/** Places the globe in the content area, facing the Indonesian archipelago. */
void globe_init(Globe *globe);

/** Centres the globe in a rectangle and clips its drawing to it. */
void globe_place(Globe *globe, int x, int y, int w, int h, int radius);

/** Draws the ocean disc and every country outline, accenting one of them. */
void globe_draw(const Globe *globe, size_t highlight);

/** Draws a station marker at a coordinate, skipping it if it faces away. */
void globe_mark(const Globe *globe, float lat, float lon, bool active);
