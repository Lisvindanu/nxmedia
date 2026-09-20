#include "ui_internal.h"

#include <stdlib.h>
#include <string.h>

/*
 * Every row re-renders the same strings each frame, and rasterising glyphs is far
 * more expensive than blitting. The key includes the clip width because the cached
 * texture holds the already-truncated string.
 */
#define CACHE_SLOTS 512

typedef struct {
	char *text;
	int font;
	uint32_t color;
	int clip_w;
	SDL_Texture *texture;
	int w;
	int h;
} TextCache;

static TextCache cache[CACHE_SLOTS];
static size_t cache_used;

static uint32_t pack_color(SDL_Color c) {
	return ((uint32_t)c.r << 24) | ((uint32_t)c.g << 16) | ((uint32_t)c.b << 8) | c.a;
}

static uint32_t hash_key(const char *text, int font, uint32_t color, int clip_w) {
	uint32_t hash = 2166136261u;
	for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
		hash = (hash ^ *p) * 16777619u;
	}
	hash = (hash ^ (uint32_t)font) * 16777619u;
	hash = (hash ^ color) * 16777619u;
	hash = (hash ^ (uint32_t)clip_w) * 16777619u;
	return hash;
}

void ui_cache_clear(void) {
	for (size_t i = 0; i < CACHE_SLOTS; i++) {
		if (!cache[i].text) continue;
		SDL_DestroyTexture(cache[i].texture);
		free(cache[i].text);
		cache[i].text = NULL;
		cache[i].texture = NULL;
	}
	cache_used = 0;
}

/** Steps back to the start of the UTF-8 sequence at or before len. */
static size_t utf8_floor(const char *text, size_t len) {
	while (len > 0 && ((unsigned char)text[len] & 0xC0) == 0x80) len--;
	return len;
}

/** Shortens text with an ellipsis until it fits clip_w, leaving it alone if it already does. */
static char *fit_text(TTF_Font *font, const char *text, int clip_w, int *out_w, int *out_h) {
	int w = 0;
	int h = 0;
	TTF_SizeUTF8(font, text, &w, &h);
	if (clip_w <= 0 || w <= clip_w) {
		*out_w = w;
		*out_h = h;
		return strdup(text);
	}

	size_t len = strlen(text);
	char *buffer = malloc(len + 4);
	if (!buffer) return NULL;

	/* Scaling by the measured overshoot lands close on the first try, so the
	 * loop below normally trims only a character or two. */
	size_t guess = (size_t)((double)len * clip_w / (double)w);
	if (guess > len) guess = len;
	len = utf8_floor(text, guess);

	while (len > 0) {
		memcpy(buffer, text, len);
		memcpy(buffer + len, "...", 4);
		TTF_SizeUTF8(font, buffer, &w, &h);
		if (w <= clip_w) break;
		len = utf8_floor(text, len - 1);
	}

	if (len == 0) {
		buffer[0] = '\0';
		w = 0;
		TTF_SizeUTF8(font, "", NULL, &h);
	}

	*out_w = w;
	*out_h = h;
	return buffer;
}

/**
 * Returns the entry holding this key, or the first free slot if it is not cached
 * yet, or NULL only when the table is entirely full. The walk covers every slot:
 * a bounded probe window can find neither, and a string that fails to cache is
 * drawn as nothing at all, which reads as a row with no name on it.
 */
static TextCache *cache_find(const char *text, int font, uint32_t color, int clip_w, bool *found) {
	uint32_t home = hash_key(text, font, color, clip_w) % CACHE_SLOTS;
	*found = false;

	for (size_t probe = 0; probe < CACHE_SLOTS; probe++) {
		TextCache *entry = &cache[(home + probe) % CACHE_SLOTS];
		if (!entry->text) return entry;
		if (entry->font == font && entry->color == color && entry->clip_w == clip_w
				&& strcmp(entry->text, text) == 0) {
			*found = true;
			return entry;
		}
	}

	return NULL;
}

static const TextCache *cache_get(const char *text, int font, SDL_Color color, int clip_w) {
	if (!text || !text[0]) return NULL;

	uint32_t packed = pack_color(color);
	bool found = false;
	TextCache *entry = cache_find(text, font, packed, clip_w, &found);
	if (found) return entry;

	/* Counters and paths make new strings constantly, so the cache is treated as a
	 * frame-scale scratch pad: past half full, drop everything and start over. Half
	 * leaves ample room for the fifty-odd strings a frame draws, and keeps the runs
	 * of occupied slots short enough that a lookup stays cheap. */
	if (!entry || cache_used * 2 >= CACHE_SLOTS) {
		ui_cache_clear();
		entry = cache_find(text, font, packed, clip_w, &found);
	}

	int w = 0;
	int h = 0;
	char *fitted = fit_text(ui_fonts[font], text, clip_w, &w, &h);
	if (!fitted || !fitted[0]) {
		free(fitted);
		return NULL;
	}

	SDL_Surface *surface = TTF_RenderUTF8_Blended(ui_fonts[font], fitted, color);
	free(fitted);
	if (!surface) return NULL;

	SDL_Texture *texture = SDL_CreateTextureFromSurface(ui_renderer, surface);
	SDL_FreeSurface(surface);
	if (!texture) return NULL;

	entry->text = strdup(text);
	if (!entry->text) {
		SDL_DestroyTexture(texture);
		return NULL;
	}

	entry->font = font;
	entry->color = packed;
	entry->clip_w = clip_w;
	entry->texture = texture;
	entry->w = w;
	entry->h = h;
	cache_used++;
	return entry;
}

int ui_draw_text(int x, int mid_y, int font, SDL_Color color, const char *text, int clip_w) {
	const TextCache *entry = cache_get(text, font, color, clip_w);
	if (!entry) return 0;

	SDL_Rect dst = { x, mid_y - entry->h / 2, entry->w, entry->h };
	SDL_RenderCopy(ui_renderer, entry->texture, NULL, &dst);
	return entry->w;
}

void ui_draw_text_right(int right, int mid_y, int font, SDL_Color color, const char *text,
		int clip_w) {
	const TextCache *entry = cache_get(text, font, color, clip_w);
	if (!entry) return;

	SDL_Rect dst = { right - entry->w, mid_y - entry->h / 2, entry->w, entry->h };
	SDL_RenderCopy(ui_renderer, entry->texture, NULL, &dst);
}

int ui_measure_text(int font, const char *text) {
	const TextCache *entry = cache_get(text, font, COLOR_TEXT, 0);
	return entry ? entry->w : 0;
}
