/*
 * Copyright © 2025 Collabora, Ltd.
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

#ifndef _WESTON_CLIENT_ASSERT_H_
#define _WESTON_CLIENT_ASSERT_H_

#include "shared/weston-assert.h"

/**
 * Macro for assert-based error handling. If the condition passed as 1st
 * argument isn't true, the process is aborted after printing out the assertion
 * and the optional error message given as 2nd argument.
 *
 * This macro isn't compiled out in release builds.
 *
 * @param a the asserted condition.
 * @param ... the optional error message.
 */
#define CLIENT_ASSERT(a, ...) \
	WESTON_ASSERT_IF_(WESTON_ASSERT_HAS_ARG_(__VA_ARGS__), \
			  WESTON_ASSERT_STR_(a, true,  bool, "%d", ==, \
					     __VA_ARGS__), \
			  WESTON_ASSERT_TRUE(a))

#endif /* _WESTON_CLIENT_ASSERT_H_ */
