#include "update.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <json-c/json.h>
#include <switch.h>

#include "util.h"

/* Binaries live in their own public repo so the updater needs no credentials; the
 * source repo stays private. One repo per app, because the releases/latest endpoint
 * only ever names a single newest release for the whole repo. */
#define RELEASE_API "https://api.github.com/repos/Lisvindanu/nxmedia-releases/releases/latest"
#define ASSET_NAME "nxmedia.nro"
#define FALLBACK_SELF_PATH "sdmc:/switch/nxmedia/nxmedia.nro"

#define PATH_MAX_LEN 512
#define NRO_MAGIC_OFFSET 0x10
#define NRO_HEADER_LEN 0x20
#define COPY_CHUNK_BYTES (64 * 1024)

static char SELF_PATH[PATH_MAX_LEN] = FALLBACK_SELF_PATH;

void update_init(const char *self_path) {
	if (!self_path || !*self_path) return;
	if (!strstr(self_path, ".nro")) return;

	/* A path without a device prefix resolves against whichever device happens to be
	 * current, which is not necessarily the SD card the app was launched from. */
	if (strchr(self_path, ':')) {
		snprintf(SELF_PATH, sizeof(SELF_PATH), "%s", self_path);
	} else {
		snprintf(SELF_PATH, sizeof(SELF_PATH), "sdmc:%s%s",
				*self_path == '/' ? "" : "/", self_path);
	}
}

const char *update_version(void) {
	return APP_VERSION;
}

/** Picks the asset named ASSET_NAME and copies its download URL and size out. */
static bool find_asset(json_object *release, UpdateInfo *out) {
	json_object *assets = NULL;
	if (!json_object_object_get_ex(release, "assets", &assets) ||
			!json_object_is_type(assets, json_type_array)) {
		return false;
	}

	size_t total = json_object_array_length(assets);
	for (size_t i = 0; i < total; i++) {
		json_object *asset = json_object_array_get_idx(assets, i);
		json_object *name = NULL, *url = NULL, *size = NULL;

		if (!json_object_object_get_ex(asset, "name", &name)) continue;
		if (strcmp(json_object_get_string(name), ASSET_NAME) != 0) continue;
		if (!json_object_object_get_ex(asset, "browser_download_url", &url)) continue;

		snprintf(out->asset_url, sizeof(out->asset_url), "%s", json_object_get_string(url));
		out->asset_size = json_object_object_get_ex(asset, "size", &size)
				? json_object_get_int64(size) : 0;
		return true;
	}
	return false;
}

bool update_check(UpdateInfo *out, char *err, size_t err_len) {
	memset(out, 0, sizeof(*out));

	HttpBuffer body = {0};
	if (!http_get(RELEASE_API, NULL, &body, err, err_len)) {
		/* GitHub answers 404 both for a repo with no release yet and for one that is
		 * not there at all. Neither is worth showing a raw URL and a JSON blob over,
		 * and the raw message is mostly URL once it is cut to fit a row. */
		if (err && strstr(err, "HTTP 404")) set_err(err, err_len, "No release published yet");
		return false;
	}

	json_object *release = json_tokener_parse(body.data ? body.data : "");
	http_buffer_free(&body);
	if (!release) {
		set_err(err, err_len, "Release feed was not valid JSON");
		return false;
	}

	bool ok = false;
	json_object *tag = NULL;
	if (!json_object_object_get_ex(release, "tag_name", &tag)) {
		set_err(err, err_len, "No release published yet");
	} else {
		snprintf(out->latest, sizeof(out->latest), "%s", json_object_get_string(tag));

		if (!version_is_newer(out->latest, update_version())) {
			ok = true; /* Already current: a valid answer, just not an update. */
		} else if (!find_asset(release, out)) {
			set_err(err, err_len, "Release %s has no %s", out->latest, ASSET_NAME);
		} else {
			out->available = true;
			ok = true;
		}
	}

	json_object_put(release);
	return ok;
}

/** An NRO carries "NRO0" at 0x10; anything else is an error page or a truncated file. */
static bool looks_like_nro(const char *path, char *err, size_t err_len) {
	FILE *f = fopen(path, "rb");
	if (!f) {
		set_err(err, err_len, "Could not reopen the downloaded file");
		return false;
	}

	char header[NRO_HEADER_LEN];
	size_t read = fread(header, 1, sizeof(header), f);
	fclose(f);

	if (read != sizeof(header) || memcmp(header + NRO_MAGIC_OFFSET, "NRO0", 4) != 0) {
		set_err(err, err_len, "Downloaded file is not a Switch app");
		return false;
	}
	return true;
}

/*
 * Writes `src` over `dst`, creating it if needed. This exists because Horizon
 * refuses to rename a file that some other handle still has open, and depending on
 * how the app was launched the running NRO is one of those. Writing over the top is
 * allowed where the rename is not: the loader reads the whole NRO into memory before
 * the app starts, so the bytes being rewritten are not the ones being executed.
 *
 * Unlike a rename this is not atomic, which is why it is only ever the fallback.
 */
static bool copy_over(const char *src, const char *dst) {
	FILE *in = fopen(src, "rb");
	if (!in) return false;

	FILE *out = fopen(dst, "wb");
	if (!out) {
		fclose(in);
		return false;
	}

	char chunk[COPY_CHUNK_BYTES];
	bool ok = true;
	size_t got;

	while ((got = fread(chunk, 1, sizeof(chunk), in)) > 0) {
		if (fwrite(chunk, 1, got, out) != got) {
			ok = false;
			break;
		}
	}
	if (ferror(in)) ok = false;

	/* The close is where a full card finally reports itself, so a failure there
	 * matters as much as one from the writes above. */
	if (fclose(out) != 0) ok = false;
	fclose(in);
	return ok;
}

bool update_apply(const UpdateInfo *info, HttpProgress on_progress, void *user,
		char *err, size_t err_len) {
	if (!info->available || !info->asset_url[0]) {
		set_err(err, err_len, "No update to install");
		return false;
	}

	/* Checked before the transfer rather than after it: a wrong path cannot be fixed
	 * by downloading several megabytes first, and naming it here is what makes the
	 * mistake visible. */
	FILE *self = fopen(SELF_PATH, "rb");
	if (!self) {
		set_err(err, err_len, "Cannot find %s", SELF_PATH);
		return false;
	}
	fclose(self);

	char staged[PATH_MAX_LEN + 8], backup[PATH_MAX_LEN + 8];
	snprintf(staged, sizeof(staged), "%s.new", SELF_PATH);
	snprintf(backup, sizeof(backup), "%s.bak", SELF_PATH);

	/* Tagging the partial file with the version stops a half-finished download of an
	 * older release from being resumed into a newer one. */
	if (!http_download(info->asset_url, NULL, staged, info->latest, NULL,
			on_progress, user, err, err_len)) {
		return false;
	}

	if (!looks_like_nro(staged, err, err_len)) {
		remove(staged);
		return false;
	}

	/* romfsMountSelf holds the running NRO open for the whole session, and Horizon
	 * refuses to rename a file that is open. Unmounting first is what gives the
	 * rename below a chance; the mount is restored either way once the swap is
	 * settled, so the rest of the session still has its data. */
	romfsExit();

	remove(backup);

	/*
	 * The running build is moved aside rather than deleted, so a swap that fails
	 * halfway still leaves an app on the card to launch.
	 *
	 * Renaming is tried first because it is atomic: there is no instant where the
	 * app on the card is half-written. Only when Horizon refuses it -- which happens
	 * when something still holds the NRO open -- does this fall back to copying,
	 * which works in that case but has to be unwound by hand if it breaks.
	 */
	int swap_errno = 0;
	unsigned swap_res = 0;
	bool swapped = false;

	if (rename(SELF_PATH, backup) == 0) {
		if (rename(staged, SELF_PATH) == 0) {
			swapped = true;
		} else {
			swap_errno = errno;
			swap_res = fsdevGetLastResult();
			rename(backup, SELF_PATH);
		}
	} else {
		swap_errno = errno;
		swap_res = fsdevGetLastResult();
		printf("[update] rename ditolak (%s, fs 0x%x), coba tulis di tempat\n",
				strerror(swap_errno), swap_res);

		if (copy_over(SELF_PATH, backup)) {
			if (copy_over(staged, SELF_PATH)) {
				swapped = true;
			} else {
				/* The app on the card is half-written now, so the backup goes back
				 * before anyone is told the update failed. */
				copy_over(backup, SELF_PATH);
			}
		}
	}

	if (!swapped) {
		romfsInit();
		remove(staged);
		set_err(err, err_len, "Could not swap the app: %s (fs 0x%x)",
				strerror(swap_errno), swap_res);
		printf("[update] gagal menukar %s\n", SELF_PATH);
		return false;
	}

	remove(backup);
	romfsInit();

	/*
	 * Everything above only reached Horizon's cache of the directory. Without this
	 * the new build runs for the rest of the session and is gone after a reboot --
	 * the same trap that used to lose finished downloads in nxdrive.
	 */
	Result commit = fsdevCommitDevice("sdmc");
	if (R_FAILED(commit)) {
		set_err(err, err_len, "Installed but not saved to the card (fs 0x%x)",
				(unsigned)commit);
		printf("[update] commit sdmc gagal 0x%x\n", (unsigned)commit);
		return false;
	}

	printf("[update] terpasang ke %s\n", SELF_PATH);
	if (envHasNextLoad()) envSetNextLoad(SELF_PATH, SELF_PATH);
	return true;
}
