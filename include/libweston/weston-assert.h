/*
 * Copyright 2022-2025 Collabora, Ltd.
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

#pragma once

#define custom_assert_fail_ weston_assert_fail_

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>

#include <libweston/weston-assert-implementation.h>
#include <libweston/libweston.h>
#include <libweston/weston-log.h>

/**
 * This is a set of assert macros intended for code that have a compositor
 * context, such as inside libweston and by users that can supply that.
 *
 * Main advantages:
 *
 * 1. Not disabled independently of the build type.
 * 2. When a weston-assert is hit, we print more meaningful messages.
 * 3. We log the messages using our log infrastructure.
 */

__attribute__((noreturn, format(printf, 2, 3)))
static inline void
weston_assert_fail_(const struct weston_compositor *compositor, const char *fmt, ...)
{
	struct weston_log_scope *scope;
	va_list ap;

	/**
	 * weston_log() is not safe when we have multiple compositors running in
	 * the same process, so we need to log using the default scope from the
	 * compositor log context. TODO: when weston_log() is fixed, we'd be
	 * able to use it, but it should also need compositor param anyway.
	 */
	scope = weston_log_ctx_get_default_log_scope(compositor->weston_log_ctx);

	va_start(ap, fmt);
	weston_log_scope_vprintf(scope, fmt, ap);
	va_end(ap);

	abort();
}
