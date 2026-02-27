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

#include "config.h"

#include "stream.h"
#include "shared/helpers.h"
#include "shared/xalloc.h"

#include <wayland-util.h>
#include <assert.h>

struct weston_stream {
	FILE *file;
	weston_stream_flush_func flush_cb;
	void *user_data;
};

static ssize_t
cookie_write(void *cookie, const char *buffer, size_t size)
{
	struct weston_stream *stream = (struct weston_stream *)cookie;

	return stream->flush_cb(buffer, size, stream->user_data);
}

static const cookie_io_functions_t cookie_funcs = {
	.read = NULL,
	.write = cookie_write,
	.seek = NULL,
	.close = NULL,
};

WL_EXPORT struct weston_stream *
weston_stream_create(size_t buffer_size, enum weston_stream_buffering buffering,
		     weston_stream_flush_func flush_cb, void *user_data)
{
	const int mode[] = { _IOFBF, _IOLBF, _IONBF };
	struct weston_stream *stream;
	FILE *file;
	int ret;

	assert(buffering == WESTON_STREAM_FULLY_BUFFERED ||
	       buffering == WESTON_STREAM_LINE_BUFFERED ||
	       buffering == WESTON_STREAM_UNBUFFERED);
	assert(flush_cb);

	stream = xmalloc((sizeof *stream + buffer_size));

	file = fopencookie(stream, "w", cookie_funcs);
	if (!file)
		goto error;

	ret = setvbuf(file, (uint8_t *)stream + sizeof *stream, mode[buffering],
		      buffer_size);
	if (ret)
		goto error;

	stream->file = file;
	stream->flush_cb = flush_cb;
	stream->user_data = user_data;

	return stream;

 error:
	free(stream);
	return NULL;
}

WL_EXPORT void
weston_stream_destroy(struct weston_stream *stream)
{
	fflush(stream->file);
	free(stream);
}

WL_EXPORT FILE *
weston_stream_get_file(struct weston_stream *stream)
{
	return stream->file;
}
