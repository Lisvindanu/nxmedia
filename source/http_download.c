#include "http_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include "util.h"

#define FILE_BUFFER_BYTES (512 * 1024)
#define CURL_BUFFER_BYTES (256 * 1024)
/* Matches tcp_rx_buf_max_size in init_socket: asking past that is silently clamped. */
#define SOCKET_RECV_BYTES (1024 * 1024)

static int receive_window_kb;

int http_receive_window_kb(void) {
	return receive_window_kb;
}

/*
 * Throughput on a long link is capped by how much data may be unacknowledged at
 * once -- the receive window -- not by the radio. Google is far enough away that
 * libnx's default window, not the WiFi, is what decides the speed. Asking for a
 * wider one raises that ceiling; the kernel may grant less than requested, so the
 * granted size is read back rather than assumed.
 */
static int widen_receive_buffer(void *user, curl_socket_t fd, curlsocktype purpose) {
	(void)user;
	if (purpose != CURLSOCKTYPE_IPCXN) return CURL_SOCKOPT_OK;

	int wanted = SOCKET_RECV_BYTES;
	setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &wanted, sizeof(wanted));

	int granted = 0;
	socklen_t len = sizeof(granted);
	if (getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &granted, &len) == 0) {
		receive_window_kb = granted / 1024;
	}

	return CURL_SOCKOPT_OK;
}

typedef struct {
	FILE *file;
	CURL *curl;
} DownloadSink;

static size_t write_to_file(char *ptr, size_t size, size_t nmemb, void *userdata) {
	DownloadSink *sink = userdata;
	size_t len = size * nmemb;

	long status = 0;
	curl_easy_getinfo(sink->curl, CURLINFO_RESPONSE_CODE, &status);

	/*
	 * A redirect page or an expired-token error is not file content. Letting it into
	 * the part file is how a resume silently corrupts: the next attempt appends real
	 * bytes behind the error text and the result is a file of the right size and the
	 * wrong content. Reporting it as written lets http_perform raise the real status
	 * instead of a misleading write failure.
	 */
	if (status < 200 || status > 299) return len;

	return fwrite(ptr, 1, len, sink->file);
}

bool http_download(const char *url, const char *bearer, const char *dest_path,
		const char *resume_tag, bool *resumed,
		HttpProgress on_progress, void *user, char *err, size_t err_len) {
	if (resumed) *resumed = false;

	char part_path[640];
	int part_len = (resume_tag && resume_tag[0])
			? snprintf(part_path, sizeof(part_path), "%s.%s.part", dest_path, resume_tag)
			: snprintf(part_path, sizeof(part_path), "%s.part", dest_path);

	/* A truncated name would drop the ".part" suffix and leave a half file looking
	 * like a finished one, so it is refused rather than shortened. */
	if (part_len < 0 || (size_t)part_len >= sizeof(part_path)) {
		set_err(err, err_len, "nama file sementara terlalu panjang");
		return false;
	}

	struct stat existing;
	curl_off_t resume_from = (stat(part_path, &existing) == 0 && existing.st_size > 0)
			? (curl_off_t)existing.st_size
			: 0;

	bool ok = false;
	bool restarted = false;

	for (;;) {
		FILE *file = fopen(part_path, resume_from > 0 ? "ab" : "wb");
		if (!file) {
			set_err(err, err_len, "tidak bisa membuka %s", part_path);
			return false;
		}

		/* The default stdio buffer is a few kilobytes, which turns a download into
		 * thousands of tiny FAT writes. Batching them is worth more than it costs. */
		char *io_buffer = malloc(FILE_BUFFER_BYTES);
		if (io_buffer) setvbuf(file, io_buffer, _IOFBF, FILE_BUFFER_BYTES);

		CURL *curl = curl_easy_init();
		if (!curl) {
			fclose(file);
			free(io_buffer);
			set_err(err, err_len, "curl_easy_init gagal");
			return false;
		}

		struct curl_slist *headers = http_bearer_header(bearer);
		ProgressState progress = { .callback = on_progress, .user = user, .resume_from = resume_from };
		DownloadSink sink = { .file = file, .curl = curl };

		http_apply_common(curl, url);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_file);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
		curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, (long)CURL_BUFFER_BYTES);
		curl_easy_setopt(curl, CURLOPT_SOCKOPTFUNCTION, widen_receive_buffer);
		if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
		if (resume_from > 0) curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, resume_from);
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
		curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, http_on_xferinfo);
		curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress);

		CURLcode rc = CURLE_OK;
		ok = http_perform(curl, "download", NULL, &rc, err, err_len);

		long status = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

		curl_slist_free_all(headers);
		curl_easy_cleanup(curl);
		fclose(file);
		free(io_buffer);

		if (ok) break;

		/*
		 * A 200 in answer to a Range request means the whole file arrived instead of
		 * the missing tail, so what we kept can never be joined to what was sent.
		 * curl refuses rather than splicing; dropping the old bytes and starting over
		 * is the only way out, or every later attempt fails the same way. The status
		 * has to be checked: an expired token trips curl's range check too, and
		 * discarding gigabytes over a token that only needs refreshing would be far
		 * worse than the failed attempt itself.
		 */
		if (rc == CURLE_RANGE_ERROR && status == 200 && resume_from > 0 && !restarted) {
			restarted = true;
			resume_from = 0;
			remove(part_path);
			continue;
		}

		/* curl reports its range check before the status, which would tell the user
		 * their download failed over a byte range when the token simply expired. */
		if (rc == CURLE_RANGE_ERROR && status != 0 && (status < 200 || status > 299)) {
			set_err(err, err_len, "download: HTTP %ld", status);
		}

		return false;
	}

	if (resumed) *resumed = resume_from > 0;

	remove(dest_path);
	if (rename(part_path, dest_path) != 0) {
		set_err(err, err_len, "gagal memindahkan %s ke %s", part_path, dest_path);
		return false;
	}

	return true;
}
