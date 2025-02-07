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

#define test_assert_true(a)  WESTON_ASSERT_TRUE(a)
#define test_assert_false(a) WESTON_ASSERT_FALSE(a)

/* String asserts. */

#define test_assert_str_eq(a, b) WESTON_ASSERT_STR_EQ(a, b)
#define test_assert_str_ne(a, b) WESTON_ASSERT_STR_NE(a, b)

/* Pointer asserts. */

#define test_assert_ptr_set(a)     WESTON_ASSERT_PTR_SET(a)
#define test_assert_ptr_not_set(a) WESTON_ASSERT_PTR_NOT_SET(a)
#define test_assert_ptr_eq(a, b)   WESTON_ASSERT_PTR_EQ(a, b)
#define test_assert_ptr_ne(a, b)   WESTON_ASSERT_PTR_NE(a, b)

/* Unsigned integer asserts. */

#define test_assert_u8_eq(a, b) WESTON_ASSERT_U8_EQ(a, b)
#define test_assert_u8_ne(a, b) WESTON_ASSERT_U8_NE(a, b)
#define test_assert_u8_gt(a, b) WESTON_ASSERT_U8_GT(a, b)
#define test_assert_u8_ge(a, b) WESTON_ASSERT_U8_GE(a, b)
#define test_assert_u8_lt(a, b) WESTON_ASSERT_U8_LT(a, b)
#define test_assert_u8_le(a, b) WESTON_ASSERT_U8_LE(a, b)

#define test_assert_u16_eq(a, b) WESTON_ASSERT_U16_EQ(a, b)
#define test_assert_u16_ne(a, b) WESTON_ASSERT_U16_NE(a, b)
#define test_assert_u16_gt(a, b) WESTON_ASSERT_U16_GT(a, b)
#define test_assert_u16_ge(a, b) WESTON_ASSERT_U16_GE(a, b)
#define test_assert_u16_lt(a, b) WESTON_ASSERT_U16_LT(a, b)
#define test_assert_u16_le(a, b) WESTON_ASSERT_U16_LE(a, b)

#define test_assert_u32_eq(a, b) WESTON_ASSERT_U32_EQ(a, b)
#define test_assert_u32_ne(a, b) WESTON_ASSERT_U32_NE(a, b)
#define test_assert_u32_gt(a, b) WESTON_ASSERT_U32_GT(a, b)
#define test_assert_u32_ge(a, b) WESTON_ASSERT_U32_GE(a, b)
#define test_assert_u32_lt(a, b) WESTON_ASSERT_U32_LT(a, b)
#define test_assert_u32_le(a, b) WESTON_ASSERT_U32_LE(a, b)

#define test_assert_u64_eq(a, b) WESTON_ASSERT_U64_EQ(a, b)
#define test_assert_u64_ne(a, b) WESTON_ASSERT_U64_NE(a, b)
#define test_assert_u64_gt(a, b) WESTON_ASSERT_U64_GT(a, b)
#define test_assert_u64_ge(a, b) WESTON_ASSERT_U64_GE(a, b)
#define test_assert_u64_lt(a, b) WESTON_ASSERT_U64_LT(a, b)
#define test_assert_u64_le(a, b) WESTON_ASSERT_U64_LE(a, b)

#define test_assert_uint_eq(a, b) WESTON_ASSERT_UINT_EQ(a, b)
#define test_assert_uint_ne(a, b) WESTON_ASSERT_UINT_NE(a, b)
#define test_assert_uint_gt(a, b) WESTON_ASSERT_UINT_GT(a, b)
#define test_assert_uint_ge(a, b) WESTON_ASSERT_UINT_GE(a, b)
#define test_assert_uint_lt(a, b) WESTON_ASSERT_UINT_LT(a, b)
#define test_assert_uint_le(a, b) WESTON_ASSERT_UINT_LE(a, b)

/* Signed integer asserts. */

#define test_assert_s8_eq(a, b) WESTON_ASSERT_S8_EQ(a, b)
#define test_assert_s8_ne(a, b) WESTON_ASSERT_S8_NE(a, b)
#define test_assert_s8_gt(a, b) WESTON_ASSERT_S8_GT(a, b)
#define test_assert_s8_ge(a, b) WESTON_ASSERT_S8_GE(a, b)
#define test_assert_s8_lt(a, b) WESTON_ASSERT_S8_LT(a, b)
#define test_assert_s8_le(a, b) WESTON_ASSERT_S8_LE(a, b)

#define test_assert_s16_eq(a, b) WESTON_ASSERT_S16_EQ(a, b)
#define test_assert_s16_ne(a, b) WESTON_ASSERT_S16_NE(a, b)
#define test_assert_s16_gt(a, b) WESTON_ASSERT_S16_GT(a, b)
#define test_assert_s16_ge(a, b) WESTON_ASSERT_S16_GE(a, b)
#define test_assert_s16_lt(a, b) WESTON_ASSERT_S16_LT(a, b)
#define test_assert_s16_le(a, b) WESTON_ASSERT_S16_LE(a, b)

#define test_assert_s32_eq(a, b) WESTON_ASSERT_S32_EQ(a, b)
#define test_assert_s32_ne(a, b) WESTON_ASSERT_S32_NE(a, b)
#define test_assert_s32_gt(a, b) WESTON_ASSERT_S32_GT(a, b)
#define test_assert_s32_ge(a, b) WESTON_ASSERT_S32_GE(a, b)
#define test_assert_s32_lt(a, b) WESTON_ASSERT_S32_LT(a, b)
#define test_assert_s32_le(a, b) WESTON_ASSERT_S32_LE(a, b)

#define test_assert_s64_eq(a, b) WESTON_ASSERT_S64_EQ(a, b)
#define test_assert_s64_ne(a, b) WESTON_ASSERT_S64_NE(a, b)
#define test_assert_s64_gt(a, b) WESTON_ASSERT_S64_GT(a, b)
#define test_assert_s64_ge(a, b) WESTON_ASSERT_S64_GE(a, b)
#define test_assert_s64_lt(a, b) WESTON_ASSERT_S64_LT(a, b)
#define test_assert_s64_le(a, b) WESTON_ASSERT_S64_LE(a, b)

#define test_assert_int_eq(a, b) WESTON_ASSERT_INT_EQ(a, b)
#define test_assert_int_ne(a, b) WESTON_ASSERT_INT_NE(a, b)
#define test_assert_int_gt(a, b) WESTON_ASSERT_INT_GT(a, b)
#define test_assert_int_ge(a, b) WESTON_ASSERT_INT_GE(a, b)
#define test_assert_int_lt(a, b) WESTON_ASSERT_INT_LT(a, b)
#define test_assert_int_le(a, b) WESTON_ASSERT_INT_LE(a, b)

/* Floating-point asserts. */

#define test_assert_f32_eq(a, b) WESTON_ASSERT_F32_EQ(a, b)
#define test_assert_f32_ne(a, b) WESTON_ASSERT_F32_NE(a, b)
#define test_assert_f32_gt(a, b) WESTON_ASSERT_F32_GT(a, b)
#define test_assert_f32_ge(a, b) WESTON_ASSERT_F32_GE(a, b)
#define test_assert_f32_lt(a, b) WESTON_ASSERT_F32_LT(a, b)
#define test_assert_f32_le(a, b) WESTON_ASSERT_F32_LE(a, b)

#define test_assert_f64_eq(a, b) WESTON_ASSERT_F64_EQ(a, b)
#define test_assert_f64_ne(a, b) WESTON_ASSERT_F64_NE(a, b)
#define test_assert_f64_gt(a, b) WESTON_ASSERT_F64_GT(a, b)
#define test_assert_f64_ge(a, b) WESTON_ASSERT_F64_GE(a, b)
#define test_assert_f64_lt(a, b) WESTON_ASSERT_F64_LT(a, b)
#define test_assert_f64_le(a, b) WESTON_ASSERT_F64_LE(a, b)

/* Bit asserts. */

#define test_assert_bit_set(value, bit)     WESTON_ASSERT_BIT_SET(value, bit)
#define test_assert_bit_not_set(value, bit) WESTON_ASSERT_BIT_NOT_SET(value, bit)
#define test_assert_legal_bits(value, mask) WESTON_ASSERT_LEGAL_BITS(value, mask)

/* Not reached asserts. Explicitly abort when reached. */

#define test_assert_not_reached(...) \
do { \
	WESTON_ASSERT_NOT_REACHED(__VA_ARGS__); \
	abort(); \
} while (0)

/* Helper asserts. */

#define test_assert_errno_eq(a) WESTON_ASSERT_ERRNO_EQ(a)
#define test_assert_errno_ne(a) WESTON_ASSERT_ERRNO_NE(a)

#define test_assert_enum_eq(a, b) WESTON_ASSERT_ENUM_EQ(a, b)
#define test_assert_enum_ne(a, b) WESTON_ASSERT_ENUM_NE(a, b)

#endif /* _WESTON_TEST_ASSERT_H_ */
