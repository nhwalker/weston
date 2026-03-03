/*
 * Copyright © 2026 Collabora
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _WESTON_STREAM_H
#define _WESTON_STREAM_H

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

enum weston_stream_buffering {
	WESTON_STREAM_FULLY_BUFFERED = 0,
	WESTON_STREAM_LINE_BUFFERED,
	WESTON_STREAM_UNBUFFERED
};

struct weston_stream;

typedef ssize_t (*weston_stream_flush_func)(const char *buffer, size_t size,
					    void *user_data);

struct weston_stream *
weston_stream_create(size_t buffer_size,
		     enum weston_stream_buffering buffering,
		     weston_stream_flush_func flush_cb, void *user_data);

void
weston_stream_destroy(struct weston_stream *stream);

FILE *
weston_stream_get_file(struct weston_stream *stream);

#endif
