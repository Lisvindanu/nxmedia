#ifndef NXMEDIA_RADIO_H
#define NXMEDIA_RADIO_H

#include <stdbool.h>
#include <stddef.h>

/* Station directory from the Radio Browser community API, fetched one country at
 * a time. Stations whose codec the decoder cannot handle are dropped. */

typedef struct {
	char name[96];
	char url[512];
	int bitrate;
} RadioStation;

/** Replaces the current list with the stations of `iso` (ISO 3166-1 alpha-2). */
bool radio_fetch(const char *iso, char *err, size_t err_len);
void radio_free(void);

size_t radio_count(void);
const RadioStation *radio_station(size_t index);

#endif
