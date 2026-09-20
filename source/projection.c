#include "projection.h"

#include <math.h>

#define DEG (float)(M_PI / 180.0)
#define RAD (float)(180.0 / M_PI)

/* A drag across the whole globe should turn it about half a revolution, which is
 * what makes the sphere feel like it is being rolled rather than scrubbed. */
#define DRAG_DEG_PER_PX 0.35f
#define TILT_LIMIT 85.0f
#define GLIDE 0.12f

/* Far enough in that Singapore is a comfortable target, and no further: past this
 * the horizon closes in and there is nothing left to aim at. */
#define ZOOM_MIN 1.0f
#define ZOOM_MAX 6.0f

static float wrap_lon(float lon) {
	while (lon > 180.0f) lon -= 360.0f;
	while (lon < -180.0f) lon += 360.0f;
	return lon;
}

Camera camera_of(const Globe *globe) {
	float lat0 = globe->lat * DEG;
	return (Camera){
		.sin_lat0 = sinf(lat0),
		.cos_lat0 = cosf(lat0),
		.lon0 = globe->lon,
		.radius = (float)globe->radius * globe->zoom,
		.cx = (float)globe->centre_x,
		.cy = (float)globe->centre_y,
	};
}

bool camera_project(const Camera *cam, float lon, float lat, float *sx, float *sy) {
	float phi = lat * DEG;
	float delta = (lon - cam->lon0) * DEG;

	float sin_phi = sinf(phi);
	float cos_phi = cosf(phi);
	float cos_delta = cosf(delta);

	if (cam->sin_lat0 * sin_phi + cam->cos_lat0 * cos_phi * cos_delta < 0.0f) return false;

	*sx = cam->cx + cam->radius * cos_phi * sinf(delta);
	*sy = cam->cy - cam->radius * (cam->cos_lat0 * sin_phi - cam->sin_lat0 * cos_phi * cos_delta);
	return true;
}

void globe_drag(Globe *globe, int dx, int dy) {
	float per_px = DRAG_DEG_PER_PX / globe->zoom;

	globe->lon = wrap_lon(globe->lon - dx * per_px);
	globe->lat += dy * per_px;

	if (globe->lat > TILT_LIMIT) globe->lat = TILT_LIMIT;
	if (globe->lat < -TILT_LIMIT) globe->lat = -TILT_LIMIT;
}

void globe_zoom(Globe *globe, float factor) {
	globe->zoom *= factor;

	if (globe->zoom < ZOOM_MIN) globe->zoom = ZOOM_MIN;
	if (globe->zoom > ZOOM_MAX) globe->zoom = ZOOM_MAX;
}

bool globe_glide(Globe *globe, float target_lat, float target_lon) {
	/* Take the short way round: flying from Japan to California should cross the
	 * Pacific, not the whole of Asia and Europe. */
	float dlon = wrap_lon(target_lon - globe->lon);
	float dlat = target_lat - globe->lat;

	if (fabsf(dlon) < 0.25f && fabsf(dlat) < 0.25f) {
		globe->lat = target_lat;
		globe->lon = target_lon;
		return false;
	}

	globe->lat += dlat * GLIDE;
	globe->lon = wrap_lon(globe->lon + dlon * GLIDE);
	return true;
}

bool globe_pick(const Globe *globe, int x, int y, float *lat, float *lon) {
	Camera cam = camera_of(globe);

	float dx = (float)x - cam.cx;
	float dy = cam.cy - (float)y;
	float rho = sqrtf(dx * dx + dy * dy);
	if (rho > cam.radius) return false;

	if (rho < 1e-4f) {
		*lat = globe->lat;
		*lon = globe->lon;
		return true;
	}

	float c = asinf(rho / cam.radius);
	float sin_c = sinf(c);
	float cos_c = cosf(c);

	*lat = asinf(cos_c * cam.sin_lat0 + dy * sin_c * cam.cos_lat0 / rho) * RAD;
	*lon = wrap_lon(globe->lon + atan2f(dx * sin_c,
			rho * cos_c * cam.cos_lat0 - dy * sin_c * cam.sin_lat0) * RAD);
	return true;
}
