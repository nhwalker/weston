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

#pragma once

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>

#ifndef custom_assert_fail_
#error "You need to define custom_assert_fail_ before including this header, maybe in a header of your own."
#endif

/**
 * Implementation of assert macros that do *not* have a compositor context
 * parameter. See shared/common-assert.h, you probably want to import that
 * header in your code. This should only be imported by those that want to
 * implement their own custom_assert_fail_.
 *
 *  DO NOT USE WITHIN LIBWESTON!
 */

#define assert_(a, b, val_type, val_fmt, cmp)					\
({										\
	val_type a_ = (a);							\
	val_type b_ = (b);							\
	bool cond = a_ cmp b_;							\
	if (!cond)								\
		custom_assert_fail_("%s:%u: Assertion %s %s %s (" val_fmt " %s " val_fmt ") failed!\n",	\
				    __FILE__, __LINE__, #a, #cmp, #b, a_, #cmp, b_);			\
	cond;									\
})

#define assert_fn_(fn, a, b, val_type, val_fmt, cmp)				\
({										\
	val_type a_ = (a);							\
	val_type b_ = (b);							\
	bool cond = fn(a_, b_) cmp 0;						\
	if (!cond)								\
		custom_assert_fail_("%s:%u: Assertion %s %s %s (" val_fmt " %s " val_fmt ") failed!\n",	\
				    __FILE__, __LINE__, #a, #cmp, #b, a_, #cmp, b_);			\
	cond;									\
})

/* Boolean asserts. */

#define assert_true(a)  assert_(a, true,  bool, "%d", ==)
#define assert_false(a) assert_(a, false, bool, "%d", ==)

/* String asserts. */

#define assert_str_eq(a, b) assert_fn_(strcmp, a, b, const char *, "%s", ==)

/* Pointer asserts. */

#define assert_ptr_null(a)     assert_(a, NULL, const void *, "%p", ==)
#define assert_ptr_not_null(a) assert_(a, NULL, const void *, "%p", !=)
#define assert_ptr_eq(a, b)    assert_(a, b,    const void *, "%p", ==)
#define assert_ptr_ne(a, b)    assert_(a, b,    const void *, "%p", !=)

/* Unsigned integer asserts. */

#define assert_u8_eq(a, b) assert_(a, b, uint8_t, "%" PRIu8, ==)
#define assert_u8_ne(a, b) assert_(a, b, uint8_t, "%" PRIu8, !=)
#define assert_u8_gt(a, b) assert_(a, b, uint8_t, "%" PRIu8, >)
#define assert_u8_ge(a, b) assert_(a, b, uint8_t, "%" PRIu8, >=)
#define assert_u8_lt(a, b) assert_(a, b, uint8_t, "%" PRIu8, <)
#define assert_u8_le(a, b) assert_(a, b, uint8_t, "%" PRIu8, <=)

#define assert_u16_eq(a, b) assert_(a, b, uint16_t, "%" PRIu16, ==)
#define assert_u16_ne(a, b) assert_(a, b, uint16_t, "%" PRIu16, !=)
#define assert_u16_gt(a, b) assert_(a, b, uint16_t, "%" PRIu16, >)
#define assert_u16_ge(a, b) assert_(a, b, uint16_t, "%" PRIu16, >=)
#define assert_u16_lt(a, b) assert_(a, b, uint16_t, "%" PRIu16, <)
#define assert_u16_le(a, b) assert_(a, b, uint16_t, "%" PRIu16, <=)

#define assert_u32_eq(a, b) assert_(a, b, uint32_t, "%" PRIu32, ==)
#define assert_u32_ne(a, b) assert_(a, b, uint32_t, "%" PRIu32, !=)
#define assert_u32_gt(a, b) assert_(a, b, uint32_t, "%" PRIu32, >)
#define assert_u32_ge(a, b) assert_(a, b, uint32_t, "%" PRIu32, >=)
#define assert_u32_lt(a, b) assert_(a, b, uint32_t, "%" PRIu32, <)
#define assert_u32_le(a, b) assert_(a, b, uint32_t, "%" PRIu32, <=)

#define assert_u64_eq(a, b) assert_(a, b, uint64_t, "%" PRIu64, ==)
#define assert_u64_ne(a, b) assert_(a, b, uint64_t, "%" PRIu64, !=)
#define assert_u64_gt(a, b) assert_(a, b, uint64_t, "%" PRIu64, >)
#define assert_u64_ge(a, b) assert_(a, b, uint64_t, "%" PRIu64, >=)
#define assert_u64_lt(a, b) assert_(a, b, uint64_t, "%" PRIu64, <)
#define assert_u64_le(a, b) assert_(a, b, uint64_t, "%" PRIu64, <=)

#define assert_uint_eq(a, b) assert_(a, b, unsigned int, "%u", ==)
#define assert_uint_ne(a, b) assert_(a, b, unsigned int, "%u", !=)
#define assert_uint_gt(a, b) assert_(a, b, unsigned int, "%u", >)
#define assert_uint_ge(a, b) assert_(a, b, unsigned int, "%u", >=)
#define assert_uint_lt(a, b) assert_(a, b, unsigned int, "%u", <)
#define assert_uint_le(a, b) assert_(a, b, unsigned int, "%u", <=)

/* Signed integer asserts. */

#define assert_s8_eq(a, b) assert_(a, b, int8_t, "%" PRId8, ==)
#define assert_s8_ne(a, b) assert_(a, b, int8_t, "%" PRId8, !=)
#define assert_s8_gt(a, b) assert_(a, b, int8_t, "%" PRId8, >)
#define assert_s8_ge(a, b) assert_(a, b, int8_t, "%" PRId8, >=)
#define assert_s8_lt(a, b) assert_(a, b, int8_t, "%" PRId8, <)
#define assert_s8_le(a, b) assert_(a, b, int8_t, "%" PRId8, <=)

#define assert_s16_eq(a, b) assert_(a, b, int16_t, "%" PRId16, ==)
#define assert_s16_ne(a, b) assert_(a, b, int16_t, "%" PRId16, !=)
#define assert_s16_gt(a, b) assert_(a, b, int16_t, "%" PRId16, >)
#define assert_s16_ge(a, b) assert_(a, b, int16_t, "%" PRId16, >=)
#define assert_s16_lt(a, b) assert_(a, b, int16_t, "%" PRId16, <)
#define assert_s16_le(a, b) assert_(a, b, int16_t, "%" PRId16, <=)

#define assert_s32_eq(a, b) assert_(a, b, int32_t, "%" PRId32, ==)
#define assert_s32_ne(a, b) assert_(a, b, int32_t, "%" PRId32, !=)
#define assert_s32_gt(a, b) assert_(a, b, int32_t, "%" PRId32, >)
#define assert_s32_ge(a, b) assert_(a, b, int32_t, "%" PRId32, >=)
#define assert_s32_lt(a, b) assert_(a, b, int32_t, "%" PRId32, <)
#define assert_s32_le(a, b) assert_(a, b, int32_t, "%" PRId32, <=)

#define assert_s64_eq(a, b) assert_(a, b, int64_t, "%" PRId64, ==)
#define assert_s64_ne(a, b) assert_(a, b, int64_t, "%" PRId64, !=)
#define assert_s64_gt(a, b) assert_(a, b, int64_t, "%" PRId64, >)
#define assert_s64_ge(a, b) assert_(a, b, int64_t, "%" PRId64, >=)
#define assert_s64_lt(a, b) assert_(a, b, int64_t, "%" PRId64, <)
#define assert_s64_le(a, b) assert_(a, b, int64_t, "%" PRId64, <=)

#define assert_int_eq(a, b) assert_(a, b, int, "%d", ==)
#define assert_int_ne(a, b) assert_(a, b, int, "%d", !=)
#define assert_int_gt(a, b) assert_(a, b, int, "%d", >)
#define assert_int_ge(a, b) assert_(a, b, int, "%d", >=)
#define assert_int_lt(a, b) assert_(a, b, int, "%d", <)
#define assert_int_le(a, b) assert_(a, b, int, "%d", <=)

/* Floating-point asserts. */

#define assert_f32_eq(a, b) assert_(a, b, float, "%.10g", ==)
#define assert_f32_ne(a, b) assert_(a, b, float, "%.10g", !=)
#define assert_f32_gt(a, b) assert_(a, b, float, "%.10g", >)
#define assert_f32_ge(a, b) assert_(a, b, float, "%.10g", >=)
#define assert_f32_lt(a, b) assert_(a, b, float, "%.10g", <)
#define assert_f32_le(a, b) assert_(a, b, float, "%.10g", <=)

#define assert_f32_absdiff_lt(a, b, tol)					\
({										\
	float a_ = (a);								\
	float b_ = (b);								\
	float tol_ = (tol);							\
	float absdiff = fabsf(a_ - b_);						\
	bool cond = absdiff < tol_;						\
	if (!cond)								\
		custom_assert_fail_("%s:%u: Assertion %s ≈≈ %s (|%.10g - %.10g| < %.10g) failed!\n",	\
				    __FILE__, __LINE__, #a, #b, a_, b_, tol_);	\
	cond;									\
})

#define assert_f64_eq(a, b) assert_(a, b, double, "%.10g", ==)
#define assert_f64_ne(a, b) assert_(a, b, double, "%.10g", !=)
#define assert_f64_gt(a, b) assert_(a, b, double, "%.10g", >)
#define assert_f64_ge(a, b) assert_(a, b, double, "%.10g", >=)
#define assert_f64_lt(a, b) assert_(a, b, double, "%.10g", <)
#define assert_f64_le(a, b) assert_(a, b, double, "%.10g", <=)

#define assert_f64_absdiff_lt(a, b, tol)					\
({										\
	double a_ = (a);							\
	double b_ = (b);							\
	double tol_ = (tol);							\
	double absdiff = fabs(a_ - b_);						\
	bool cond = absdiff < tol_;						\
	if (!cond)								\
		custom_assert_fail_("%s:%u: Assertion %s ≈≈ %s (|%.10g - %.10g| < %.10g) failed!\n",	\
				    __FILE__, __LINE__, #a, #b, a_, b_, tol_);	\
	cond;									\
})

/* Bitmask asserts. */

#define assert_bit_set(value, bit)						\
({										\
	uint64_t v = (value);							\
	uint64_t b = (bit);							\
	bool cond = (v & b) == b;						\
	assert_true(is_pow2_64(bit));						\
	if (!cond)								\
		custom_assert_fail_("%s:%u: Assertion failed! Bit \"%s\" (%" PRIu64 ") of \"%s\" (0x%" PRIx64 ") is not set.\n",	\
				    __FILE__, __LINE__, #bit, b, #value, v);	\
	cond;									\
})

#define assert_bit_not_set(value, bit)						\
({										\
	uint64_t v = (value);							\
	uint64_t b = (bit);							\
	bool cond = (v & b) == 0;						\
	assert_true(is_pow2_64(bit));						\
	if (!cond)								\
		custom_assert_fail_("%s:%u: Assertion failed! Bit \"%s\" (%" PRIu64 ") of \"%s\" (0x%" PRIx64 ") is set.\n",	\
				    __FILE__, __LINE__, #bit, b, #value, v);	\
	cond;									\
})

#define assert_legal_bits(value, mask)						\
({										\
	uint64_t v_ = (value);							\
	uint64_t m_ = (mask);							\
	uint64_t ill = v_ & ~m_;						\
	bool cond = ill == 0;							\
	if (!cond)								\
		custom_assert_fail_("%s:%u: Assertion failed! "			\
				    "Value %s (0x%" PRIx64 ") contains illegal bits 0x%" PRIx64 ". " \
				    "Legal mask is %s (0x%" PRIx64 ").\n",		\
				    __FILE__, __LINE__, #value, v_, ill, #mask, m_); 	\
	cond;									\
})

/* Various helpers. */

#define assert_errno(a)            assert_int_eq(a, errno)
#define assert_enum(a, b)          assert_u64_eq(a, b)

/* Explicitly abort when reached. */
#define assert_not_reached(reason)						\
do {										\
	custom_assert_fail_("%s:%u: Assertion failed! This should not be reached: %s\n",	\
			    __FILE__, __LINE__, reason);			\
	abort();								\
} while (0)
