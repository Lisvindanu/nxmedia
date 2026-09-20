#include "radio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <json-c/json.h>

#include "http.h"

#define API_BASE "https://de1.api.radio-browser.info/json/stations/bycountrycodeexact/"
/* The directory has tens of thousands of entries; a country's most-clicked
 * hundred is already more than anyone scrolls through. */
#define STATION_LIMIT 100

static RadioStation *stations;
static size_t station_count;

static void copy_field(char *dst, size_t dst_len, json_object *obj, const char *key) {
	json_object *value = NULL;
	if (!json_object_object_get_ex(obj, key, &value) ||
			!json_object_is_type(value, json_type_string)) {
		dst[0] = '\0';
		return;
	}

	snprintf(dst, dst_len, "%s", json_object_get_string(value));
}

/* What the bundled ffmpeg can decode. AAC covers the "AAC+"/"AACP" spellings the
 * directory also uses, so it is matched on its prefix. Anything the directory
 * could not identify is kept: the codec column is user-submitted and often blank
 * for stations that play fine. */
static bool is_playable(json_object *entry) {
	static const char *const known[] = {"MP3", "OGG", "FLAC", "WAV", "ALAC"};

	json_object *codec = NULL;
	if (!json_object_object_get_ex(entry, "codec", &codec)) return true;

	const char *text = json_object_get_string(codec);
	if (!text || !*text || strcasecmp(text, "UNKNOWN") == 0) return true;
	if (strncasecmp(text, "AAC", 3) == 0) return true;

	for (size_t i = 0; i < sizeof(known) / sizeof(*known); i++) {
		if (strcasecmp(text, known[i]) == 0) return true;
	}
	return false;
}

static bool parse_stations(json_object *root, char *err, size_t err_len) {
	if (!json_object_is_type(root, json_type_array)) {
		snprintf(err, err_len, "balasan API bukan daftar");
		return false;
	}

	size_t total = json_object_array_length(root);
	if (total == 0) return true;

	stations = calloc(total, sizeof(*stations));
	if (!stations) {
		snprintf(err, err_len, "kehabisan memori");
		return false;
	}

	for (size_t i = 0; i < total; i++) {
		json_object *entry = json_object_array_get_idx(root, i);
		if (!is_playable(entry)) continue;

		RadioStation *out = &stations[station_count];

		/* url_resolved has already followed the playlist or redirect the station
		 * was registered with, which saves a hop and a failure mode. */
		copy_field(out->url, sizeof(out->url), entry, "url_resolved");
		if (out->url[0] == '\0') copy_field(out->url, sizeof(out->url), entry, "url");
		if (out->url[0] == '\0') continue;

		copy_field(out->name, sizeof(out->name), entry, "name");
		if (out->name[0] == '\0') snprintf(out->name, sizeof(out->name), "Tanpa nama");

		json_object *bitrate = NULL;
		if (json_object_object_get_ex(entry, "bitrate", &bitrate)) {
			out->bitrate = json_object_get_int(bitrate);
		}

		station_count++;
	}

	return true;
}

bool radio_fetch(const char *iso, char *err, size_t err_len) {
	radio_free();

	char url[256];
	snprintf(url, sizeof(url),
			API_BASE "%s?limit=%d&hidebroken=true&order=clickcount&reverse=true",
			iso, STATION_LIMIT);

	HttpBuffer body = {0};
	if (!http_get(url, NULL, &body, err, err_len)) return false;

	json_object *root = json_tokener_parse(body.data ? body.data : "");
	http_buffer_free(&body);

	if (!root) {
		snprintf(err, err_len, "balasan API tidak terbaca");
		return false;
	}

	bool ok = parse_stations(root, err, err_len);
	json_object_put(root);

	if (!ok) radio_free();
	return ok;
}

void radio_free(void) {
	free(stations);
	stations = NULL;
	station_count = 0;
}

size_t radio_count(void) {
	return station_count;
}

const RadioStation *radio_station(size_t index) {
	if (index >= station_count) return NULL;
	return &stations[index];
}
