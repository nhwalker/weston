/*
 * Copyright © 2024 Collabora, Ltd.
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

#ifndef _WESTON_TEST_ASSERT_H_
#define _WESTON_TEST_ASSERT_H_

#include <errno.h>

#include "libweston/libweston-internal.h"
#include "shared/weston-assert.h"

int
weston_assert_counter_get(void);

void
weston_assert_counter_inc(void);

void
weston_assert_counter_reset(void);

__attribute__((format(printf, 1, 2)))
static inline void
test_assert_fail(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	weston_assert_counter_inc();
}

#ifdef custom_assert_fail_
#undef custom_assert_fail_
#endif
#define custom_assert_fail_ test_assert_fail

/* Boolean asserts. */

#define TEST_ASSERT_TRUE(a)  WESTON_ASSERT_TRUE(a)
#define TEST_ASSERT_FALSE(a) WESTON_ASSERT_FALSE(a)

/* String asserts. */

#define TEST_ASSERT_STR_EQ(a, b) WESTON_ASSERT_STR_EQ(a, b)
#define TEST_ASSERT_STR_NE(a, b) WESTON_ASSERT_STR_NE(a, b)

/* Pointer asserts. */

#define TEST_ASSERT_PTR_SET(a)     WESTON_ASSERT_PTR_SET(a)
#define TEST_ASSERT_PTR_NOT_SET(a) WESTON_ASSERT_PTR_NOT_SET(a)
#define TEST_ASSERT_PTR_EQ(a, b)   WESTON_ASSERT_PTR_EQ(a, b)
#define TEST_ASSERT_PTR_NE(a, b)   WESTON_ASSERT_PTR_NE(a, b)

/* Unsigned integer asserts. */

#define TEST_ASSERT_U8_EQ(a, b) WESTON_ASSERT_U8_EQ(a, b)
#define TEST_ASSERT_U8_NE(a, b) WESTON_ASSERT_U8_NE(a, b)
#define TEST_ASSERT_U8_GT(a, b) WESTON_ASSERT_U8_GT(a, b)
#define TEST_ASSERT_U8_GE(a, b) WESTON_ASSERT_U8_GE(a, b)
#define TEST_ASSERT_U8_LT(a, b) WESTON_ASSERT_U8_LT(a, b)
#define TEST_ASSERT_U8_LE(a, b) WESTON_ASSERT_U8_LE(a, b)

#define TEST_ASSERT_U16_EQ(a, b) WESTON_ASSERT_U16_EQ(a, b)
#define TEST_ASSERT_U16_NE(a, b) WESTON_ASSERT_U16_NE(a, b)
#define TEST_ASSERT_U16_GT(a, b) WESTON_ASSERT_U16_GT(a, b)
#define TEST_ASSERT_U16_GE(a, b) WESTON_ASSERT_U16_GE(a, b)
#define TEST_ASSERT_U16_LT(a, b) WESTON_ASSERT_U16_LT(a, b)
#define TEST_ASSERT_U16_LE(a, b) WESTON_ASSERT_U16_LE(a, b)

#define TEST_ASSERT_U32_EQ(a, b) WESTON_ASSERT_U32_EQ(a, b)
#define TEST_ASSERT_U32_NE(a, b) WESTON_ASSERT_U32_NE(a, b)
#define TEST_ASSERT_U32_GT(a, b) WESTON_ASSERT_U32_GT(a, b)
#define TEST_ASSERT_U32_GE(a, b) WESTON_ASSERT_U32_GE(a, b)
#define TEST_ASSERT_U32_LT(a, b) WESTON_ASSERT_U32_LT(a, b)
#define TEST_ASSERT_U32_LE(a, b) WESTON_ASSERT_U32_LE(a, b)

#define TEST_ASSERT_U64_EQ(a, b) WESTON_ASSERT_U64_EQ(a, b)
#define TEST_ASSERT_U64_NE(a, b) WESTON_ASSERT_U64_NE(a, b)
#define TEST_ASSERT_U64_GT(a, b) WESTON_ASSERT_U64_GT(a, b)
#define TEST_ASSERT_U64_GE(a, b) WESTON_ASSERT_U64_GE(a, b)
#define TEST_ASSERT_U64_LT(a, b) WESTON_ASSERT_U64_LT(a, b)
#define TEST_ASSERT_U64_LE(a, b) WESTON_ASSERT_U64_LE(a, b)

#define TEST_ASSERT_UINT_EQ(a, b) WESTON_ASSERT_UINT_EQ(a, b)
#define TEST_ASSERT_UINT_NE(a, b) WESTON_ASSERT_UINT_NE(a, b)
#define TEST_ASSERT_UINT_GT(a, b) WESTON_ASSERT_UINT_GT(a, b)
#define TEST_ASSERT_UINT_GE(a, b) WESTON_ASSERT_UINT_GE(a, b)
#define TEST_ASSERT_UINT_LT(a, b) WESTON_ASSERT_UINT_LT(a, b)
#define TEST_ASSERT_UINT_LE(a, b) WESTON_ASSERT_UINT_LE(a, b)

/* Signed integer asserts. */

#define TEST_ASSERT_S8_EQ(a, b) WESTON_ASSERT_S8_EQ(a, b)
#define TEST_ASSERT_S8_NE(a, b) WESTON_ASSERT_S8_NE(a, b)
#define TEST_ASSERT_S8_GT(a, b) WESTON_ASSERT_S8_GT(a, b)
#define TEST_ASSERT_S8_GE(a, b) WESTON_ASSERT_S8_GE(a, b)
#define TEST_ASSERT_S8_LT(a, b) WESTON_ASSERT_S8_LT(a, b)
#define TEST_ASSERT_S8_LE(a, b) WESTON_ASSERT_S8_LE(a, b)

#define TEST_ASSERT_S16_EQ(a, b) WESTON_ASSERT_S16_EQ(a, b)
#define TEST_ASSERT_S16_NE(a, b) WESTON_ASSERT_S16_NE(a, b)
#define TEST_ASSERT_S16_GT(a, b) WESTON_ASSERT_S16_GT(a, b)
#define TEST_ASSERT_S16_GE(a, b) WESTON_ASSERT_S16_GE(a, b)
#define TEST_ASSERT_S16_LT(a, b) WESTON_ASSERT_S16_LT(a, b)
#define TEST_ASSERT_S16_LE(a, b) WESTON_ASSERT_S16_LE(a, b)

#define TEST_ASSERT_S32_EQ(a, b) WESTON_ASSERT_S32_EQ(a, b)
#define TEST_ASSERT_S32_NE(a, b) WESTON_ASSERT_S32_NE(a, b)
#define TEST_ASSERT_S32_GT(a, b) WESTON_ASSERT_S32_GT(a, b)
#define TEST_ASSERT_S32_GE(a, b) WESTON_ASSERT_S32_GE(a, b)
#define TEST_ASSERT_S32_LT(a, b) WESTON_ASSERT_S32_LT(a, b)
#define TEST_ASSERT_S32_LE(a, b) WESTON_ASSERT_S32_LE(a, b)

#define TEST_ASSERT_S64_EQ(a, b) WESTON_ASSERT_S64_EQ(a, b)
#define TEST_ASSERT_S64_NE(a, b) WESTON_ASSERT_S64_NE(a, b)
#define TEST_ASSERT_S64_GT(a, b) WESTON_ASSERT_S64_GT(a, b)
#define TEST_ASSERT_S64_GE(a, b) WESTON_ASSERT_S64_GE(a, b)
#define TEST_ASSERT_S64_LT(a, b) WESTON_ASSERT_S64_LT(a, b)
#define TEST_ASSERT_S64_LE(a, b) WESTON_ASSERT_S64_LE(a, b)

#define TEST_ASSERT_INT_EQ(a, b) WESTON_ASSERT_INT_EQ(a, b)
#define TEST_ASSERT_INT_NE(a, b) WESTON_ASSERT_INT_NE(a, b)
#define TEST_ASSERT_INT_GT(a, b) WESTON_ASSERT_INT_GT(a, b)
#define TEST_ASSERT_INT_GE(a, b) WESTON_ASSERT_INT_GE(a, b)
#define TEST_ASSERT_INT_LT(a, b) WESTON_ASSERT_INT_LT(a, b)
#define TEST_ASSERT_INT_LE(a, b) WESTON_ASSERT_INT_LE(a, b)

/* Floating-point asserts. */

#define TEST_ASSERT_F32_EQ(a, b) WESTON_ASSERT_F32_EQ(a, b)
#define TEST_ASSERT_F32_NE(a, b) WESTON_ASSERT_F32_NE(a, b)
#define TEST_ASSERT_F32_GT(a, b) WESTON_ASSERT_F32_GT(a, b)
#define TEST_ASSERT_F32_GE(a, b) WESTON_ASSERT_F32_GE(a, b)
#define TEST_ASSERT_F32_LT(a, b) WESTON_ASSERT_F32_LT(a, b)
#define TEST_ASSERT_F32_LE(a, b) WESTON_ASSERT_F32_LE(a, b)

#define TEST_ASSERT_F64_EQ(a, b) WESTON_ASSERT_F64_EQ(a, b)
#define TEST_ASSERT_F64_NE(a, b) WESTON_ASSERT_F64_NE(a, b)
#define TEST_ASSERT_F64_GT(a, b) WESTON_ASSERT_F64_GT(a, b)
#define TEST_ASSERT_F64_GE(a, b) WESTON_ASSERT_F64_GE(a, b)
#define TEST_ASSERT_F64_LT(a, b) WESTON_ASSERT_F64_LT(a, b)
#define TEST_ASSERT_F64_LE(a, b) WESTON_ASSERT_F64_LE(a, b)

/* Bit asserts. */

#define TEST_ASSERT_BIT_SET(a, bit)         WESTON_ASSERT_BIT_SET(a, bit)
#define TEST_ASSERT_BIT_NOT_SET(a, bit)     WESTON_ASSERT_BIT_NOT_SET(a, bit)
#define TEST_ASSERT_LEGAL_BITS(value, mask) WESTON_ASSERT_LEGAL_BITS(value, mask)

/* Not reached asserts. Explicitly abort when reached. */

#define TEST_ASSERT_NOT_REACHED(...) \
do { \
	WESTON_ASSERT_NOT_REACHED(__VA_ARGS__); \
	abort(); \
} while (0)

/* Helper asserts. */

#define TEST_ASSERT_ERRNO_EQ(a) WESTON_ASSERT_ERRNO_EQ(a)
#define TEST_ASSERT_ERRNO_NE(a) WESTON_ASSERT_ERRNO_NE(a)

#define TEST_ASSERT_ENUM_EQ(a, b) WESTON_ASSERT_ENUM_EQ(a, b)
#define TEST_ASSERT_ENUM_NE(a, b) WESTON_ASSERT_ENUM_NE(a, b)

#endif /* _WESTON_TEST_ASSERT_H_ */
