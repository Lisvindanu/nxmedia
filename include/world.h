#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Country outlines, loaded once from romfs. The whole world is 45 KB at this
 * resolution, so it stays resident: the alternative is a network round trip to
 * answer "what did the user just tap on", which is the one question that has to
 * be instant.
 */

typedef struct {
	/** Index of the first point of this ring in the shared point array. */
	uint32_t first;
	uint32_t count;
} WorldRing;

typedef struct {
	char iso[3];
	char name[64];
	uint32_t first_ring;
	uint32_t ring_count;
	/** Degrees, the centroid of the largest ring. Where the camera flies to. */
	float lat;
	float lon;
} WorldCountry;

bool world_load(const char *path, char *err, size_t err_len);
void world_free(void);

size_t world_country_count(void);
const WorldCountry *world_country(size_t index);
const WorldRing *world_ring(size_t index);

/** Points of a ring as lon,lat pairs in degrees. */
void world_ring_point(size_t ring, size_t index, float *lon, float *lat);

/** Country containing the point, or SIZE_MAX when it fell in open water. */
size_t world_locate(float lon, float lat);

/** Country with that ISO code, or SIZE_MAX. Case-insensitive. */
size_t world_find_iso(const char *iso);
