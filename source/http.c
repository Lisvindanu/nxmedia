#include "http_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

#define USER_AGENT "nxdrive/0.1 (Nintendo Switch)"
#define STALL_BYTES_PER_SEC 512
#define STALL_SECONDS 30

size_t http_write_to_buffer(char *ptr, size_t size, size_t nmemb, void *userdata) {
	HttpBuffer *buf = userdata;
	size_t added = size * nmemb;

	char *grown = realloc(buf->data, buf->len + added + 1);
	if (!grown) return 0;

	buf->data = grown;
	memcpy(buf->data + buf->len, ptr, added);
	buf->len += added;
	buf->data[buf->len] = '\0';
	return added;
}

int http_on_xferinfo(void *userdata, curl_off_t dl_total, curl_off_t dl_done,
		curl_off_t ul_total, curl_off_t ul_done) {
	ProgressState *state = userdata;
	if (!state->callback) return 0;

	curl_off_t total = state->uploading ? ul_total : dl_total;
	curl_off_t done = state->uploading ? ul_done : dl_done;

	int64_t absolute_done = (int64_t)(state->resume_from + done);
	int64_t absolute_total = total > 0 ? (int64_t)(state->resume_from + total) : -1;

	return state->callback(state->user, absolute_done, absolute_total) ? 0 : 1;
}

bool http_init(char *err, size_t err_len) {
	CURLcode rc = curl_global_init(CURL_GLOBAL_DEFAULT);
	if (rc != CURLE_OK) {
		set_err(err, err_len, "curl_global_init: %s", curl_easy_strerror(rc));
		return false;
	}
	return true;
}

void http_exit(void) {
	curl_global_cleanup();
}

void http_buffer_free(HttpBuffer *buf) {
	free(buf->data);
	buf->data = NULL;
	buf->len = 0;
}

char *http_escape(const char *value) {
	return curl_easy_escape(NULL, value, 0);
}

void http_free_escaped(char *escaped) {
	curl_free(escaped);
}

struct curl_slist *http_bearer_header(const char *bearer) {
	if (!bearer) return NULL;

	char line[2048];
	snprintf(line, sizeof(line), "Authorization: Bearer %s", bearer);
	return curl_slist_append(NULL, line);
}

void http_apply_common(CURL *curl, const char *url) {
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, (long)STALL_BYTES_PER_SEC);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, (long)STALL_SECONDS);
}

bool http_perform(CURL *curl, const char *what, const HttpBuffer *body,
		CURLcode *out_rc, char *err, size_t err_len) {
	CURLcode rc = curl_easy_perform(curl);
	if (out_rc) *out_rc = rc;

	if (rc != CURLE_OK) {
		set_err(err, err_len, "%s: %s", what, curl_easy_strerror(rc));
		return false;
	}

	long status = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
	if (status < 200 || status > 299) {
		const char *detail = (body && body->data) ? body->data : "";
		set_err(err, err_len, "%s: HTTP %ld %.400s", what, status, detail);
		return false;
	}

	return true;
}

static bool request_to_buffer(const char *url, const char *bearer, const char *post_body,
		HttpBuffer *out, char *err, size_t err_len) {
	*out = (HttpBuffer){0};

	CURL *curl = curl_easy_init();
	if (!curl) {
		set_err(err, err_len, "curl_easy_init gagal");
		return false;
	}

	struct curl_slist *headers = http_bearer_header(bearer);

	http_apply_common(curl, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_write_to_buffer);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
	if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	if (post_body) curl_easy_setopt(curl, CURLOPT_COPYPOSTFIELDS, post_body);

	bool ok = http_perform(curl, post_body ? "POST" : "GET", out, NULL, err, err_len);

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	if (!ok) http_buffer_free(out);
	return ok;
}

bool http_get(const char *url, const char *bearer, HttpBuffer *out, char *err, size_t err_len) {
	return request_to_buffer(url, bearer, NULL, out, err, err_len);
}

bool http_post_form(const char *url, const char *body, HttpBuffer *out, char *err, size_t err_len) {
	return request_to_buffer(url, NULL, body, out, err, err_len);
}

/** Throws away whatever arrives; a warm-up wants the server's work, not its bytes. */
static size_t discard(char *ptr, size_t size, size_t nmemb, void *userdata) {
	(void)ptr;
	(void)userdata;
	return size * nmemb;
}

bool http_touch(const char *url, int timeout_seconds, char *err, size_t err_len) {
	CURL *curl = curl_easy_init();
	if (!curl) {
		set_err(err, err_len, "curl_easy_init gagal");
		return false;
	}

	http_apply_common(curl, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard);

	/* One byte is enough: the cost being paid is the server resolving the media,
	 * and it has to finish that before it can answer with any byte at all. */
	curl_easy_setopt(curl, CURLOPT_RANGE, "0-0");

	/* The stall detector would fire long before a slow resolve answers, and that is
	 * the whole thing being waited out here. */
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 0L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)timeout_seconds);

	bool ok = http_perform(curl, "menyiapkan", NULL, NULL, err, err_len);
	curl_easy_cleanup(curl);
	return ok;
}
