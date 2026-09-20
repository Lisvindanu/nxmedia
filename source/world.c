#include "world.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

#define SCALE 100.0f

static WorldCountry *countries;
static size_t country_count;
static WorldRing *rings;
static size_t ring_count;
static int16_t *points;
static size_t point_count;

/* --------------------------------------------------------------- reading */

typedef struct {
	const uint8_t *at;
	const uint8_t *end;
	bool bad;
} Reader;

static uint8_t take_u8(Reader *r) {
	if (r->at + 1 > r->end) { r->bad = true; return 0; }
	return *r->at++;
}

static uint16_t take_u16(Reader *r) {
	if (r->at + 2 > r->end) { r->bad = true; return 0; }
	uint16_t v = (uint16_t)r->at[0] | (uint16_t)r->at[1] << 8;
	r->at += 2;
	return v;
}

static int16_t take_i16(Reader *r) {
	return (int16_t)take_u16(r);
}

static void take_bytes(Reader *r, void *out, size_t n) {
	if (r->at + n > r->end) { r->bad = true; return; }
	memcpy(out, r->at, n);
	r->at += n;
}

/* --------------------------------------------------------------- loading */

static uint8_t *read_file(const char *path, size_t *out_len, char *err, size_t err_len) {
	FILE *file = fopen(path, "rb");
	if (!file) {
		set_err(err, err_len, "tidak bisa membuka %s", path);
		return NULL;
	}

	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	fseek(file, 0, SEEK_SET);

	if (size <= 6) {
		set_err(err, err_len, "%s kosong atau rusak", path);
		fclose(file);
		return NULL;
	}

	uint8_t *buffer = malloc((size_t)size);
	if (!buffer || fread(buffer, 1, (size_t)size, file) != (size_t)size) {
		set_err(err, err_len, "gagal membaca %s", path);
		free(buffer);
		fclose(file);
		return NULL;
	}

	fclose(file);
	*out_len = (size_t)size;
	return buffer;
}

/* The mean of a ring's vertices, which for these shapes lands inside the country
 * often enough to aim a camera with, and never needs the polygon area that a true
 * centroid would. */
static void ring_centre(size_t ring, float *lon, float *lat) {
	double sum_lon = 0;
	double sum_lat = 0;

	for (uint32_t i = 0; i < rings[ring].count; i++) {
		size_t at = (rings[ring].first + i) * 2;
		sum_lon += points[at];
		sum_lat += points[at + 1];
	}

	*lon = (float)(sum_lon / rings[ring].count / SCALE);
	*lat = (float)(sum_lat / rings[ring].count / SCALE);
}

bool world_load(const char *path, char *err, size_t err_len) {
	size_t len = 0;
	uint8_t *raw = read_file(path, &len, err, err_len);
	if (!raw) return false;

	Reader r = { raw, raw + len, false };

	char magic[4];
	take_bytes(&r, magic, 4);
	if (r.bad || memcmp(magic, "NXGL", 4) != 0) {
		set_err(err, err_len, "%s bukan file peta", path);
		free(raw);
		return false;
	}

	country_count = take_u16(&r);
	countries = calloc(country_count, sizeof(*countries));

	/* Two passes would mean parsing the file twice; growing instead keeps it to
	 * one, and the arrays settle after a handful of doublings. */
	size_t ring_cap = 512;
	size_t point_cap = 16384;
	rings = malloc(ring_cap * sizeof(*rings));
	points = malloc(point_cap * 2 * sizeof(*points));
	ring_count = 0;
	point_count = 0;

	if (!countries || !rings || !points) {
		set_err(err, err_len, "kehabisan memori memuat peta");
		free(raw);
		world_free();
		return false;
	}

	for (size_t c = 0; c < country_count && !r.bad; c++) {
		WorldCountry *country = &countries[c];

		take_bytes(&r, country->iso, 2);
		country->iso[2] = '\0';

		uint8_t name_len = take_u8(&r);
		if (name_len >= sizeof(country->name)) { r.bad = true; break; }
		take_bytes(&r, country->name, name_len);
		country->name[name_len] = '\0';

		country->first_ring = (uint32_t)ring_count;
		country->ring_count = take_u16(&r);

		for (uint32_t k = 0; k < country->ring_count && !r.bad; k++) {
			if (ring_count == ring_cap) {
				ring_cap *= 2;
				WorldRing *grown = realloc(rings, ring_cap * sizeof(*rings));
				if (!grown) { r.bad = true; break; }
				rings = grown;
			}

			uint16_t n = take_u16(&r);

			while (point_count + n > point_cap) {
				point_cap *= 2;
				int16_t *grown = realloc(points, point_cap * 2 * sizeof(*points));
				if (!grown) { r.bad = true; break; }
				points = grown;
			}
			if (r.bad) break;

			rings[ring_count].first = (uint32_t)point_count;
			rings[ring_count].count = n;

			for (uint16_t i = 0; i < n; i++) {
				points[(point_count + i) * 2] = take_i16(&r);
				points[(point_count + i) * 2 + 1] = take_i16(&r);
			}

			point_count += n;
			ring_count++;
		}

		/* Aim at the biggest landmass: Alaska should not drag the camera off the
		 * continental United States. */
		uint32_t widest = country->first_ring;
		for (uint32_t k = 1; k < country->ring_count; k++) {
			if (rings[country->first_ring + k].count > rings[widest].count) {
				widest = country->first_ring + k;
			}
		}
		if (country->ring_count) ring_centre(widest, &country->lon, &country->lat);
	}

	free(raw);

	if (r.bad) {
		set_err(err, err_len, "isi %s terpotong", path);
		world_free();
		return false;
	}

	return true;
}

void world_free(void) {
	free(countries);
	free(rings);
	free(points);
	countries = NULL;
	rings = NULL;
	points = NULL;
	country_count = 0;
	ring_count = 0;
	point_count = 0;
}

/* --------------------------------------------------------------- lookups */

size_t world_country_count(void) {
	return country_count;
}

const WorldCountry *world_country(size_t index) {
	return index < country_count ? &countries[index] : NULL;
}

const WorldRing *world_ring(size_t index) {
	return index < ring_count ? &rings[index] : NULL;
}

void world_ring_point(size_t ring, size_t index, float *lon, float *lat) {
	size_t at = (rings[ring].first + index) * 2;
	*lon = points[at] / SCALE;
	*lat = points[at + 1] / SCALE;
}

static bool ring_contains(size_t ring, int x, int y) {
	bool inside = false;
	uint32_t n = rings[ring].count;
	size_t base = rings[ring].first * 2;

	for (uint32_t i = 0, j = n - 1; i < n; j = i++) {
		int xi = points[base + i * 2], yi = points[base + i * 2 + 1];
		int xj = points[base + j * 2], yj = points[base + j * 2 + 1];

		if ((yi > y) == (yj > y)) continue;
		/* Cross-multiplied so the ray test stays in integers: the stored
		 * coordinates are exact, and a divide here would not be. */
		long long cross = (long long)(xj - xi) * (y - yi) - (long long)(x - xi) * (yj - yi);
		if ((cross > 0) == (yj > yi)) inside = !inside;
	}

	return inside;
}

size_t world_locate(float lon, float lat) {
	int x = (int)(lon * SCALE);
	int y = (int)(lat * SCALE);

	for (size_t c = 0; c < country_count; c++) {
		for (uint32_t k = 0; k < countries[c].ring_count; k++) {
			if (ring_contains(countries[c].first_ring + k, x, y)) return c;
		}
	}

	return SIZE_MAX;
}

size_t world_find_iso(const char *iso) {
	if (!iso || !iso[0] || !iso[1]) return SIZE_MAX;

	for (size_t c = 0; c < country_count; c++) {
		if (strncasecmp(countries[c].iso, iso, 2) == 0) return c;
	}

	return SIZE_MAX;
}
