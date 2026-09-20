#include <math.h>
#include <stdio.h>
#include "projection.h"
#include "world.h"

static int fails;

static void check(const char *label, int ok) {
	if (!ok) fails++;
	printf("%-34s %s\n", label, ok ? "ok" : "FAIL");
}

/* project -> pick must land back on the same coordinate, or a tap will select a
 * different country than the one drawn under the finger. */
static void roundtrip(const Globe *g, const char *label, float lon, float lat) {
	Camera cam = camera_of(g);
	float sx, sy;
	if (!camera_project(&cam, lon, lat, &sx, &sy)) {
		printf("%-34s tidak terlihat (dilewati)\n", label);
		return;
	}
	float blat, blon;
	int got = globe_pick(g, (int)(sx + 0.5f), (int)(sy + 0.5f), &blat, &blon);
	float dlat = fabsf(blat - lat), dlon = fabsf(blon - lon);
	if (dlon > 180.0f) dlon = 360.0f - dlon;
	printf("%-34s %s  (%.2f,%.2f -> %.2f,%.2f)\n", label,
			(got && dlat < 0.6f && dlon < 1.2f) ? "ok" : "FAIL", lat, lon, blat, blon);
	if (!(got && dlat < 0.6f && dlon < 1.2f)) fails++;
}

int main(void) {
	char err[256];
	if (!world_load("romfs/world.bin", err, sizeof(err))) { printf("%s\n", err); return 1; }

	Globe g = { .lat = -2.0f, .lon = 121.6f, .centre_x = 780, .centre_y = 380,
			.radius = 250, .zoom = 1.0f };

	roundtrip(&g, "pusat pandang", 121.6f, -2.0f);
	roundtrip(&g, "Jakarta", 106.85f, -6.21f);
	roundtrip(&g, "Tokyo", 139.69f, 35.69f);
	roundtrip(&g, "Sydney", 151.21f, -33.87f);

	/* Far side must be rejected, not wrapped round to a near-side point. */
	Camera cam = camera_of(&g);
	float sx, sy;
	check("Paris tersembunyi dari Sulawesi", !camera_project(&cam, 2.35f, 48.86f, &sx, &sy));

	float lat, lon;
	check("tap di luar cakram ditolak", !globe_pick(&g, 100, 100, &lat, &lon));
	check("tap di pusat diterima", globe_pick(&g, 780, 380, &lat, &lon));

	/* A tap on the globe must name the country whose outline is drawn there. */
	camera_project(&cam, 106.85f, -6.21f, &sx, &sy);
	globe_pick(&g, (int)sx, (int)sy, &lat, &lon);
	const WorldCountry *c = world_country(world_locate(lon, lat));
	printf("%-34s %s  (%s)\n", "tap Jakarta -> negara",
			(c && c->iso[0] == 'I' && c->iso[1] == 'D') ? "ok" : "FAIL", c ? c->name : "laut");
	if (!(c && c->iso[0] == 'I' && c->iso[1] == 'D')) fails++;

	/* Dragging a full globe width should turn it most of the way round. */
	Globe d = g;
	globe_drag(&d, 500, 0);
	printf("%-34s %.1f -> %.1f\n", "geser 500px", g.lon, d.lon);
	check("geser tidak melewati kutub", (globe_drag(&d, 0, 900), d.lat <= 85.0f));

	/* Zoom exists so small countries stop being a coin toss, so picking has to stay
	 * honest once the disc is magnified. */
	Globe z = g;
	globe_zoom(&z, 4.0f);
	roundtrip(&z, "Singapura saat 4x", 103.85f, 1.35f);
	roundtrip(&z, "Brunei saat 4x", 114.94f, 4.53f);

	globe_zoom(&z, 100.0f);
	check("zoom berhenti di batas atas", z.zoom <= 6.0f);
	globe_zoom(&z, 0.001f);
	check("zoom berhenti di batas bawah", z.zoom >= 1.0f);

	/* Dragging has to slow down with the zoom, or a finger throws the globe off
	 * screen the moment it is magnified. */
	Globe near = g, far = g;
	globe_zoom(&far, 4.0f);
	globe_drag(&near, 100, 0);
	globe_drag(&far, 100, 0);
	float near_turn = fabsf(near.lon - g.lon), far_turn = fabsf(far.lon - g.lon);
	printf("%-34s %.1f vs %.1f\n", "geser 100px: 1x vs 4x", near_turn, far_turn);
	check("geser saat zoom lebih halus", fabsf(near_turn / 4.0f - far_turn) < 0.5f);

	int steps = 0;
	Globe f = g;
	while (globe_glide(&f, 35.69f, 139.69f) && steps < 200) steps++;
	printf("%-34s %d frame\n", "terbang ke Tokyo", steps);
	check("terbang selesai", steps > 0 && steps < 100);

	world_free();
	printf("\n%s\n", fails ? "ADA YANG GAGAL" : "semua lolos");
	return fails != 0;
}
