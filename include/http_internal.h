#pragma once

#include <curl/curl.h>

#include "http.h"

/*
 * The curl plumbing every request shares, whether it ends up in a buffer, in a
 * file, or on its way to Drive. Private to the http_*.c files.
 */

typedef struct {
	HttpProgress callback;
	void *user;
	/* Bytes already transferred before this handle started, so a resumed transfer
	 * reports its position in the whole file rather than in the remaining tail. */
	curl_off_t resume_from;
	bool uploading;
} ProgressState;

/** CURLOPT_WRITEFUNCTION that appends into an HttpBuffer. */
size_t http_write_to_buffer(char *ptr, size_t size, size_t nmemb, void *userdata);

/** CURLOPT_XFERINFOFUNCTION that reports through a ProgressState. */
int http_on_xferinfo(void *userdata, curl_off_t dl_total, curl_off_t dl_done,
		curl_off_t ul_total, curl_off_t ul_done);

/** Builds an Authorization list, or NULL when there is no bearer to send. */
struct curl_slist *http_bearer_header(const char *bearer);

/** Sets the URL and the timeouts every request wants. */
void http_apply_common(CURL *curl, const char *url);

/**
 * Runs the handle and turns a non-2xx status into an error carrying the response
 * body. out_rc, when given, receives the transport-level result so a caller can
 * tell one kind of failure from another.
 */
bool http_perform(CURL *curl, const char *what, const HttpBuffer *body,
		CURLcode *out_rc, char *err, size_t err_len);
