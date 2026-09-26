#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <json-c/json.h>

/* The instance this app was built against. Overridable with "mediavault_url" in
 * the settings file, which is how a different server is used without a rebuild. */
#define DEFAULT_MEDIAVAULT_URL "https://mediavault.project-n.site/api/public"

/* Searching here instead of on the server is worth roughly twelve times the speed
 * -- 0.6 seconds against 7.4 -- because it answers from a JavaScript runtime rather
 * than starting a Python process for every request. */
#define DEFAULT_MEDIAVAULT_EDGE_URL "https://mediavault-browse.lisvindanu015.workers.dev"

/* A settings file is a few hundred bytes. Anything vastly larger is not one, and
 * reading it whole into memory would be the wrong response either way. */
#define SETTINGS_MAX_BYTES (64 * 1024)

static char *read_file(const char *path) {
	FILE *file = fopen(path, "rb");
	if (!file) return NULL;

	if (fseek(file, 0, SEEK_END) != 0) {
		fclose(file);
		return NULL;
	}

	long size = ftell(file);
	if (size < 0 || size > SETTINGS_MAX_BYTES) {
		fclose(file);
		return NULL;
	}
	rewind(file);

	char *text = malloc((size_t)size + 1);
	if (!text) {
		fclose(file);
		return NULL;
	}

	size_t read = fread(text, 1, (size_t)size, file);
	fclose(file);

	text[read] = '\0';
	return text;
}

static void copy_string_field(json_object *root, const char *key, char *out, size_t out_len) {
	json_object *value = NULL;
	if (!json_object_object_get_ex(root, key, &value)) return;
	if (!json_object_is_type(value, json_type_string)) return;

	const char *text = json_object_get_string(value);
	if (text && text[0]) snprintf(out, out_len, "%s", text);
}

void settings_load(Settings *out) {
	snprintf(out->mediavault_url, sizeof(out->mediavault_url), "%s", DEFAULT_MEDIAVAULT_URL);
	snprintf(out->mediavault_edge_url, sizeof(out->mediavault_edge_url), "%s",
			DEFAULT_MEDIAVAULT_EDGE_URL);

	char *text = read_file(SETTINGS_PATH);
	if (!text) return;

	json_object *root = json_tokener_parse(text);
	free(text);

	/* A malformed file is treated as an absent one. There is nothing useful to say
	 * about it at this point in startup, and the defaults are all still valid. */
	if (!root) return;

	if (json_object_is_type(root, json_type_object)) {
		copy_string_field(root, "mediavault_url", out->mediavault_url,
				sizeof(out->mediavault_url));
		copy_string_field(root, "mediavault_edge_url", out->mediavault_edge_url,
				sizeof(out->mediavault_edge_url));
	}

	json_object_put(root);
}
