#pragma once

#include <stdbool.h>

/*
 * Orthographic sphere-to-screen, and back again. Pure arithmetic with no SDL in
 * sight, so a sign error surfaces in a host test instead of as a globe that spins
 * the wrong way on the console.
 */

typedef struct {
	/** Degrees. The point on the sphere facing the viewer. */
	float lat;
	float lon;
	int centre_x;
	int centre_y;
	/** Radius at rest. What actually gets drawn is this times the zoom. */
	int radius;
	float zoom;
	/** The panel the globe lives in. Zoomed in, the disc runs well past it. */
	int view_x;
	int view_y;
	int view_w;
	int view_h;
} Globe;

/** One frame's worth of held trigger. Small enough that a tap nudges. */
#define GLOBE_ZOOM_STEP 1.04f

/** The trigonometry of one viewpoint, worked out once per frame. */
typedef struct {
	float sin_lat0;
	float cos_lat0;
	float lon0;
	float radius;
	float cx;
	float cy;
} Camera;

Camera camera_of(const Globe *globe);

/** False when the coordinate is on the far side of the sphere. */
bool camera_project(const Camera *cam, float lon, float lat, float *sx, float *sy);

/** Spins the globe by a drag in pixels, clamping the tilt so it never flips. */
void globe_drag(Globe *globe, int dx, int dy);

/**
 * Multiplies the zoom and clamps it. Zooming in also makes dragging finer, so the
 * land under a finger keeps moving at roughly the same speed on screen however far
 * in the view is pushed.
 */
void globe_zoom(Globe *globe, float factor);

/**
 * Eases the facing point towards a target, taking the short way round the globe.
 * Returns false once it has arrived.
 */
bool globe_glide(Globe *globe, float target_lat, float target_lon);

/**
 * Turns a screen point back into a coordinate. False when the point missed the
 * disc entirely, which is the difference between "ocean" and "not the globe".
 */
bool globe_pick(const Globe *globe, int x, int y, float *lat, float *lon);
