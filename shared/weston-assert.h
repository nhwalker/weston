/*
 * Copyright 2022 Collabora, Ltd.
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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>
#include <errno.h>

__attribute__((noreturn, format(printf, 1, 2)))
static inline void
weston_assert_fail_(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	abort();
}

#ifndef custom_assert_fail_
#define custom_assert_fail_ weston_assert_fail_
#endif

#define WESTON_ASSERT_(a, b, val_type, val_fmt, cmp)				\
({										\
	val_type a_ = (a);							\
	val_type b_ = (b);							\
	bool cond = a_ cmp b_;							\
	if (!cond)								\
		custom_assert_fail_("%s:%u: Assertion %s %s %s (" val_fmt " %s " val_fmt ") failed!\n",	\
				    __FILE__, __LINE__, #a, #cmp, #b, a_, #cmp, b_);				\
	cond;									\
})

#define WESTON_ASSERT_FN_(fn, a, b, val_type, val_fmt, cmp)			\
({										\
	val_type a_ = (a);							\
	val_type b_ = (b);							\
	bool cond = fn(a_, b_) cmp 0;						\
	if (!cond)								\
		custom_assert_fail_("%s:%u: Assertion %s %s %s (" val_fmt " %s " val_fmt ") failed!\n",	\
				    __FILE__, __LINE__, #a, #cmp, #b, a_, #cmp, b_);				\
	cond;									\
})

/* Boolean asserts. */

#define WESTON_ASSERT_TRUE(a)  WESTON_ASSERT_(a, true,  bool, "%d", ==)
#define WESTON_ASSERT_FALSE(a) WESTON_ASSERT_(a, false, bool, "%d", ==)

/* String asserts. */

#define WESTON_ASSERT_STR_EQ(a, b) WESTON_ASSERT_FN_(strcmp, a, b, const char *, "%s", ==)
#define WESTON_ASSERT_STR_NE(a, b) WESTON_ASSERT_FN_(strcmp, a, b, const char *, "%s", !=)

/* Pointer asserts. */

#define WESTON_ASSERT_PTR_SET(a)     WESTON_ASSERT_(a, NULL, const void *, "%p", !=)
#define WESTON_ASSERT_PTR_NOT_SET(a) WESTON_ASSERT_(a, NULL, const void *, "%p", ==)
#define WESTON_ASSERT_PTR_EQ(a, b)   WESTON_ASSERT_(a, b, const void *, "%p", ==)
#define WESTON_ASSERT_PTR_NE(a, b)   WESTON_ASSERT_(a, b, const void *, "%p", !=)

/* Unsigned integer asserts. */

#define WESTON_ASSERT_U8_EQ(a, b) WESTON_ASSERT_(a, b, uint8_t, "%" PRIu8, ==)
#define WESTON_ASSERT_U8_NE(a, b) WESTON_ASSERT_(a, b, uint8_t, "%" PRIu8, !=)
#define WESTON_ASSERT_U8_GT(a, b) WESTON_ASSERT_(a, b, uint8_t, "%" PRIu8, >)
#define WESTON_ASSERT_U8_GE(a, b) WESTON_ASSERT_(a, b, uint8_t, "%" PRIu8, >=)
#define WESTON_ASSERT_U8_LT(a, b) WESTON_ASSERT_(a, b, uint8_t, "%" PRIu8, <)
#define WESTON_ASSERT_U8_LE(a, b) WESTON_ASSERT_(a, b, uint8_t, "%" PRIu8, <=)

#define WESTON_ASSERT_U16_EQ(a, b) WESTON_ASSERT_(a, b, uint16_t, "%" PRIu16, ==)
#define WESTON_ASSERT_U16_NE(a, b) WESTON_ASSERT_(a, b, uint16_t, "%" PRIu16, !=)
#define WESTON_ASSERT_U16_GT(a, b) WESTON_ASSERT_(a, b, uint16_t, "%" PRIu16, >)
#define WESTON_ASSERT_U16_GE(a, b) WESTON_ASSERT_(a, b, uint16_t, "%" PRIu16, >=)
#define WESTON_ASSERT_U16_LT(a, b) WESTON_ASSERT_(a, b, uint16_t, "%" PRIu16, <)
#define WESTON_ASSERT_U16_LE(a, b) WESTON_ASSERT_(a, b, uint16_t, "%" PRIu16, <=)

#define WESTON_ASSERT_U32_EQ(a, b) WESTON_ASSERT_(a, b, uint32_t, "%" PRIu32, ==)
#define WESTON_ASSERT_U32_NE(a, b) WESTON_ASSERT_(a, b, uint32_t, "%" PRIu32, !=)
#define WESTON_ASSERT_U32_GT(a, b) WESTON_ASSERT_(a, b, uint32_t, "%" PRIu32, >)
#define WESTON_ASSERT_U32_GE(a, b) WESTON_ASSERT_(a, b, uint32_t, "%" PRIu32, >=)
#define WESTON_ASSERT_U32_LT(a, b) WESTON_ASSERT_(a, b, uint32_t, "%" PRIu32, <)
#define WESTON_ASSERT_U32_LE(a, b) WESTON_ASSERT_(a, b, uint32_t, "%" PRIu32, <=)

#define WESTON_ASSERT_U64_EQ(a, b) WESTON_ASSERT_(a, b, uint64_t, "%" PRIu64, ==)
#define WESTON_ASSERT_U64_NE(a, b) WESTON_ASSERT_(a, b, uint64_t, "%" PRIu64, !=)
#define WESTON_ASSERT_U64_GT(a, b) WESTON_ASSERT_(a, b, uint64_t, "%" PRIu64, >)
#define WESTON_ASSERT_U64_GE(a, b) WESTON_ASSERT_(a, b, uint64_t, "%" PRIu64, >=)
#define WESTON_ASSERT_U64_LT(a, b) WESTON_ASSERT_(a, b, uint64_t, "%" PRIu64, <)
#define WESTON_ASSERT_U64_LE(a, b) WESTON_ASSERT_(a, b, uint64_t, "%" PRIu64, <=)

#define WESTON_ASSERT_UINT_EQ(a, b) WESTON_ASSERT_(a, b, unsigned int, "%u", ==)
#define WESTON_ASSERT_UINT_NE(a, b) WESTON_ASSERT_(a, b, unsigned int, "%u", !=)
#define WESTON_ASSERT_UINT_GT(a, b) WESTON_ASSERT_(a, b, unsigned int, "%u", >)
#define WESTON_ASSERT_UINT_GE(a, b) WESTON_ASSERT_(a, b, unsigned int, "%u", >=)
#define WESTON_ASSERT_UINT_LT(a, b) WESTON_ASSERT_(a, b, unsigned int, "%u", <)
#define WESTON_ASSERT_UINT_LE(a, b) WESTON_ASSERT_(a, b, unsigned int, "%u", <=)

/* Signed integer asserts. */

#define WESTON_ASSERT_S8_EQ(a, b) WESTON_ASSERT_(a, b, int8_t, "%" PRId8, ==)
#define WESTON_ASSERT_S8_NE(a, b) WESTON_ASSERT_(a, b, int8_t, "%" PRId8, !=)
#define WESTON_ASSERT_S8_GT(a, b) WESTON_ASSERT_(a, b, int8_t, "%" PRId8, >)
#define WESTON_ASSERT_S8_GE(a, b) WESTON_ASSERT_(a, b, int8_t, "%" PRId8, >=)
#define WESTON_ASSERT_S8_LT(a, b) WESTON_ASSERT_(a, b, int8_t, "%" PRId8, <)
#define WESTON_ASSERT_S8_LE(a, b) WESTON_ASSERT_(a, b, int8_t, "%" PRId8, <=)

#define WESTON_ASSERT_S16_EQ(a, b) WESTON_ASSERT_(a, b, int16_t, "%" PRId16, ==)
#define WESTON_ASSERT_S16_NE(a, b) WESTON_ASSERT_(a, b, int16_t, "%" PRId16, !=)
#define WESTON_ASSERT_S16_GT(a, b) WESTON_ASSERT_(a, b, int16_t, "%" PRId16, >)
#define WESTON_ASSERT_S16_GE(a, b) WESTON_ASSERT_(a, b, int16_t, "%" PRId16, >=)
#define WESTON_ASSERT_S16_LT(a, b) WESTON_ASSERT_(a, b, int16_t, "%" PRId16, <)
#define WESTON_ASSERT_S16_LE(a, b) WESTON_ASSERT_(a, b, int16_t, "%" PRId16, <=)

#define WESTON_ASSERT_S32_EQ(a, b) WESTON_ASSERT_(a, b, int32_t, "%" PRId32, ==)
#define WESTON_ASSERT_S32_NE(a, b) WESTON_ASSERT_(a, b, int32_t, "%" PRId32, !=)
#define WESTON_ASSERT_S32_GT(a, b) WESTON_ASSERT_(a, b, int32_t, "%" PRId32, >)
#define WESTON_ASSERT_S32_GE(a, b) WESTON_ASSERT_(a, b, int32_t, "%" PRId32, >=)
#define WESTON_ASSERT_S32_LT(a, b) WESTON_ASSERT_(a, b, int32_t, "%" PRId32, <)
#define WESTON_ASSERT_S32_LE(a, b) WESTON_ASSERT_(a, b, int32_t, "%" PRId32, <=)

#define WESTON_ASSERT_S64_EQ(a, b) WESTON_ASSERT_(a, b, int64_t, "%" PRId64, ==)
#define WESTON_ASSERT_S64_NE(a, b) WESTON_ASSERT_(a, b, int64_t, "%" PRId64, !=)
#define WESTON_ASSERT_S64_GT(a, b) WESTON_ASSERT_(a, b, int64_t, "%" PRId64, >)
#define WESTON_ASSERT_S64_GE(a, b) WESTON_ASSERT_(a, b, int64_t, "%" PRId64, >=)
#define WESTON_ASSERT_S64_LT(a, b) WESTON_ASSERT_(a, b, int64_t, "%" PRId64, <)
#define WESTON_ASSERT_S64_LE(a, b) WESTON_ASSERT_(a, b, int64_t, "%" PRId64, <=)

#define WESTON_ASSERT_INT_EQ(a, b) WESTON_ASSERT_(a, b, int, "%d", ==)
#define WESTON_ASSERT_INT_NE(a, b) WESTON_ASSERT_(a, b, int, "%d", !=)
#define WESTON_ASSERT_INT_GT(a, b) WESTON_ASSERT_(a, b, int, "%d", >)
#define WESTON_ASSERT_INT_GE(a, b) WESTON_ASSERT_(a, b, int, "%d", >=)
#define WESTON_ASSERT_INT_LT(a, b) WESTON_ASSERT_(a, b, int, "%d", <)
#define WESTON_ASSERT_INT_LE(a, b) WESTON_ASSERT_(a, b, int, "%d", <=)

/* Floating-point asserts. */

#define WESTON_ASSERT_F32_EQ(a, b) WESTON_ASSERT_(a, b, float, "%.10g", ==)
#define WESTON_ASSERT_F32_NE(a, b) WESTON_ASSERT_(a, b, float, "%.10g", !=)
#define WESTON_ASSERT_F32_GT(a, b) WESTON_ASSERT_(a, b, float, "%.10g", >)
#define WESTON_ASSERT_F32_GE(a, b) WESTON_ASSERT_(a, b, float, "%.10g", >=)
#define WESTON_ASSERT_F32_LT(a, b) WESTON_ASSERT_(a, b, float, "%.10g", <)
#define WESTON_ASSERT_F32_LE(a, b) WESTON_ASSERT_(a, b, float, "%.10g", <=)

#define WESTON_ASSERT_F64_EQ(a, b) WESTON_ASSERT_(a, b, double, "%.10g", ==)
#define WESTON_ASSERT_F64_NE(a, b) WESTON_ASSERT_(a, b, double, "%.10g", !=)
#define WESTON_ASSERT_F64_GT(a, b) WESTON_ASSERT_(a, b, double, "%.10g", >)
#define WESTON_ASSERT_F64_GE(a, b) WESTON_ASSERT_(a, b, double, "%.10g", >=)
#define WESTON_ASSERT_F64_LT(a, b) WESTON_ASSERT_(a, b, double, "%.10g", <)
#define WESTON_ASSERT_F64_LE(a, b) WESTON_ASSERT_(a, b, double, "%.10g", <=)

/* Bit asserts. */

#define WESTON_ASSERT_BIT_SET(value, bit)					\
({										\
	uint64_t v = (value);							\
	uint64_t b = (bit);							\
	bool cond = (v & b) == b;						\
	WESTON_ASSERT_TRUE(is_pow2_64(bit));					\
	if (!cond)								\
		custom_assert_fail_("%s:%u: Assertion failed! Bit \"%s\" (%" PRIu64 ") of \"%s\" (0x%" PRIx64 ") is not set.\n",	\
				    __FILE__, __LINE__, #bit, b, #value, v);	\
	cond;									\
})

#define WESTON_ASSERT_BIT_NOT_SET(value, bit)					\
({										\
	uint64_t v = (value);							\
	uint64_t b = (bit);							\
	bool cond = (v & b) == 0;						\
	WESTON_ASSERT_TRUE(is_pow2_64(bit));					\
	if (!cond)								\
		custom_assert_fail_("%s:%u: Assertion failed! Bit \"%s\" (%" PRIu64 ") of \"%s\" (0x%" PRIx64 ") is set.\n",	\
				    __FILE__, __LINE__, #bit, b, #value, v);	\
	cond;									\
})

#define WESTON_ASSERT_LEGAL_BITS(value, mask)					\
({										\
	uint64_t v_ = (value);							\
	uint64_t m_ = (mask);							\
	uint64_t ill = v_ & ~m_;						\
	bool cond = ill == 0;							\
	if (!cond)								\
		custom_assert_fail_("%s:%u: Assertion failed! "		\
				    "Value %s (0x%" PRIx64 ") contains illegal bits 0x%" PRIx64 ". " \
				    "Legal mask is %s (0x%" PRIx64 ").\n",	\
				    __FILE__, __LINE__, #value, v_, ill, #mask, m_); \
	cond;									\
})

/* Not reached asserts. */

#define WESTON_ASSERT_HAS_ARG____(a, b, c, ...) c
#define WESTON_ASSERT_HAS_ARG___(...) \
	WESTON_ASSERT_HAS_ARG____(__VA_ARGS__, 0, 1)
#define WESTON_ASSERT_HAS_ARG__(...) \
	WESTON_ASSERT_HAS_ARG___(_, ## __VA_ARGS__)
#define WESTON_ASSERT_HAS_ARG_(...) WESTON_ASSERT_HAS_ARG__(__VA_ARGS__)
#define WESTON_ASSERT_CONCAT__(a, b) a ## b
#define WESTON_ASSERT_CONCAT_(a, b) WESTON_ASSERT_CONCAT__(a, b)
#define WESTON_ASSERT_IF_0(a, b) a
#define WESTON_ASSERT_IF_1(a, b) b
#define WESTON_ASSERT_IF_(cond, a, b) \
	WESTON_ASSERT_CONCAT_(WESTON_ASSERT_IF_, cond)(a, b)

#define WESTON_ASSERT_NOT_REACHED_STR_(str) \
	custom_assert_fail_("%s:%u: Assertion failed! Not reached: %s\n", \
			    __FILE__, __LINE__, str)

#define WESTON_ASSERT_NOT_REACHED_NO_STR_() \
	custom_assert_fail_("%s:%u: Assertion failed! Not reached.\n", \
			    __FILE__, __LINE__)

#define WESTON_ASSERT_NOT_REACHED(...) \
	WESTON_ASSERT_IF_(WESTON_ASSERT_HAS_ARG_(__VA_ARGS__), \
			  WESTON_ASSERT_NOT_REACHED_STR_(__VA_ARGS__), \
			  WESTON_ASSERT_NOT_REACHED_NO_STR_())

/* Helper asserts. */

#define WESTON_ASSERT_ERRNO_EQ(a) WESTON_ASSERT_INT_EQ(a, errno)
#define WESTON_ASSERT_ERRNO_NE(a) WESTON_ASSERT_INT_NE(a, errno)

#define WESTON_ASSERT_ENUM_EQ(a, b) WESTON_ASSERT_U64_EQ(a, b)
#define WESTON_ASSERT_ENUM_NE(a, b) WESTON_ASSERT_U64_NE(a, b)

/* Debug asserts. Compiled out in release builds. */

#if !defined(NDEBUG)

#define WESTON_DASSERT_TRUE(a) WESTON_ASSERT_TRUE(a)
#define WESTON_DASSERT_FALSE(a) WESTON_ASSERT_FALSE(a)
#define WESTON_DASSERT_STR_EQ(a, b) WESTON_ASSERT_STR_EQ(a, b)
#define WESTON_DASSERT_STR_NE(a, b) WESTON_ASSERT_STR_NE(a, b)
#define WESTON_DASSERT_PTR_SET(a) WESTON_ASSERT_PTR_SET(a)
#define WESTON_DASSERT_PTR_NOT_SET(a) WESTON_ASSERT_PTR_NOT_SET(a)
#define WESTON_DASSERT_PTR_EQ(a, b) WESTON_ASSERT_PTR_EQ(a, b)
#define WESTON_DASSERT_PTR_NE(a, b) WESTON_ASSERT_PTR_NE(a, b)
#define WESTON_DASSERT_U8_EQ(a, b) WESTON_ASSERT_U8_EQ(a, b)
#define WESTON_DASSERT_U8_NE(a, b) WESTON_ASSERT_U8_NE(a, b)
#define WESTON_DASSERT_U8_GT(a, b) WESTON_ASSERT_U8_GT(a, b)
#define WESTON_DASSERT_U8_GE(a, b) WESTON_ASSERT_U8_GE(a, b)
#define WESTON_DASSERT_U8_LT(a, b) WESTON_ASSERT_U8_LT(a, b)
#define WESTON_DASSERT_U8_LE(a, b) WESTON_ASSERT_U8_LE(a, b)
#define WESTON_DASSERT_U16_EQ(a, b) WESTON_ASSERT_U16_EQ(a, b)
#define WESTON_DASSERT_U16_NE(a, b) WESTON_ASSERT_U16_NE(a, b)
#define WESTON_DASSERT_U16_GT(a, b) WESTON_ASSERT_U16_GT(a, b)
#define WESTON_DASSERT_U16_GE(a, b) WESTON_ASSERT_U16_GE(a, b)
#define WESTON_DASSERT_U16_LT(a, b) WESTON_ASSERT_U16_LT(a, b)
#define WESTON_DASSERT_U16_LE(a, b) WESTON_ASSERT_U16_LE(a, b)
#define WESTON_DASSERT_U32_EQ(a, b) WESTON_ASSERT_U32_EQ(a, b)
#define WESTON_DASSERT_U32_NE(a, b) WESTON_ASSERT_U32_NE(a, b)
#define WESTON_DASSERT_U32_GT(a, b) WESTON_ASSERT_U32_GT(a, b)
#define WESTON_DASSERT_U32_GE(a, b) WESTON_ASSERT_U32_GE(a, b)
#define WESTON_DASSERT_U32_LT(a, b) WESTON_ASSERT_U32_LT(a, b)
#define WESTON_DASSERT_U32_LE(a, b) WESTON_ASSERT_U32_LE(a, b)
#define WESTON_DASSERT_U64_EQ(a, b) WESTON_ASSERT_U64_EQ(a, b)
#define WESTON_DASSERT_U64_NE(a, b) WESTON_ASSERT_U64_NE(a, b)
#define WESTON_DASSERT_U64_GT(a, b) WESTON_ASSERT_U64_GT(a, b)
#define WESTON_DASSERT_U64_GE(a, b) WESTON_ASSERT_U64_GE(a, b)
#define WESTON_DASSERT_U64_LT(a, b) WESTON_ASSERT_U64_LT(a, b)
#define WESTON_DASSERT_U64_LE(a, b) WESTON_ASSERT_U64_LE(a, b)
#define WESTON_DASSERT_UINT_EQ(a, b) WESTON_ASSERT_UINT_EQ(a, b)
#define WESTON_DASSERT_UINT_NE(a, b) WESTON_ASSERT_UINT_NE(a, b)
#define WESTON_DASSERT_UINT_GT(a, b) WESTON_ASSERT_UINT_GT(a, b)
#define WESTON_DASSERT_UINT_GE(a, b) WESTON_ASSERT_UINT_GE(a, b)
#define WESTON_DASSERT_UINT_LT(a, b) WESTON_ASSERT_UINT_LT(a, b)
#define WESTON_DASSERT_UINT_LE(a, b) WESTON_ASSERT_UINT_LE(a, b)
#define WESTON_DASSERT_S8_EQ(a, b) WESTON_ASSERT_S8_EQ(a, b)
#define WESTON_DASSERT_S8_NE(a, b) WESTON_ASSERT_S8_NE(a, b)
#define WESTON_DASSERT_S8_GT(a, b) WESTON_ASSERT_S8_GT(a, b)
#define WESTON_DASSERT_S8_GE(a, b) WESTON_ASSERT_S8_GE(a, b)
#define WESTON_DASSERT_S8_LT(a, b) WESTON_ASSERT_S8_LT(a, b)
#define WESTON_DASSERT_S8_LE(a, b) WESTON_ASSERT_S8_LE(a, b)
#define WESTON_DASSERT_S16_EQ(a, b) WESTON_ASSERT_S16_EQ(a, b)
#define WESTON_DASSERT_S16_NE(a, b) WESTON_ASSERT_S16_NE(a, b)
#define WESTON_DASSERT_S16_GT(a, b) WESTON_ASSERT_S16_GT(a, b)
#define WESTON_DASSERT_S16_GE(a, b) WESTON_ASSERT_S16_GE(a, b)
#define WESTON_DASSERT_S16_LT(a, b) WESTON_ASSERT_S16_LT(a, b)
#define WESTON_DASSERT_S16_LE(a, b) WESTON_ASSERT_S16_LE(a, b)
#define WESTON_DASSERT_S32_EQ(a, b) WESTON_ASSERT_S32_EQ(a, b)
#define WESTON_DASSERT_S32_NE(a, b) WESTON_ASSERT_S32_NE(a, b)
#define WESTON_DASSERT_S32_GT(a, b) WESTON_ASSERT_S32_GT(a, b)
#define WESTON_DASSERT_S32_GE(a, b) WESTON_ASSERT_S32_GE(a, b)
#define WESTON_DASSERT_S32_LT(a, b) WESTON_ASSERT_S32_LT(a, b)
#define WESTON_DASSERT_S32_LE(a, b) WESTON_ASSERT_S32_LE(a, b)
#define WESTON_DASSERT_S64_EQ(a, b) WESTON_ASSERT_S64_EQ(a, b)
#define WESTON_DASSERT_S64_NE(a, b) WESTON_ASSERT_S64_NE(a, b)
#define WESTON_DASSERT_S64_GT(a, b) WESTON_ASSERT_S64_GT(a, b)
#define WESTON_DASSERT_S64_GE(a, b) WESTON_ASSERT_S64_GE(a, b)
#define WESTON_DASSERT_S64_LT(a, b) WESTON_ASSERT_S64_LT(a, b)
#define WESTON_DASSERT_S64_LE(a, b) WESTON_ASSERT_S64_LE(a, b)
#define WESTON_DASSERT_INT_EQ(a, b) WESTON_ASSERT_INT_EQ(a, b)
#define WESTON_DASSERT_INT_NE(a, b) WESTON_ASSERT_INT_NE(a, b)
#define WESTON_DASSERT_INT_GT(a, b) WESTON_ASSERT_INT_GT(a, b)
#define WESTON_DASSERT_INT_GE(a, b) WESTON_ASSERT_INT_GE(a, b)
#define WESTON_DASSERT_INT_LT(a, b) WESTON_ASSERT_INT_LT(a, b)
#define WESTON_DASSERT_INT_LE(a, b) WESTON_ASSERT_INT_LE(a, b)
#define WESTON_DASSERT_F32_EQ(a, b) WESTON_ASSERT_F32_EQ(a, b)
#define WESTON_DASSERT_F32_NE(a, b) WESTON_ASSERT_F32_NE(a, b)
#define WESTON_DASSERT_F32_GT(a, b) WESTON_ASSERT_F32_GT(a, b)
#define WESTON_DASSERT_F32_GE(a, b) WESTON_ASSERT_F32_GE(a, b)
#define WESTON_DASSERT_F32_LT(a, b) WESTON_ASSERT_F32_LT(a, b)
#define WESTON_DASSERT_F32_LE(a, b) WESTON_ASSERT_F32_LE(a, b)
#define WESTON_DASSERT_F64_EQ(a, b) WESTON_ASSERT_F64_EQ(a, b)
#define WESTON_DASSERT_F64_NE(a, b) WESTON_ASSERT_F64_NE(a, b)
#define WESTON_DASSERT_F64_GT(a, b) WESTON_ASSERT_F64_GT(a, b)
#define WESTON_DASSERT_F64_GE(a, b) WESTON_ASSERT_F64_GE(a, b)
#define WESTON_DASSERT_F64_LT(a, b) WESTON_ASSERT_F64_LT(a, b)
#define WESTON_DASSERT_F64_LE(a, b) WESTON_ASSERT_F64_LE(a, b)
#define WESTON_DASSERT_BIT_SET(value, bit) WESTON_ASSERT_BIT_SET(value, bit)
#define WESTON_DASSERT_BIT_NOT_SET(value, bit) WESTON_ASSERT_BIT_NOT_SET(value, bit)
#define WESTON_DASSERT_LEGAL_BITS(value, mask) WESTON_ASSERT_LEGAL_BITS(value, mask)
#define WESTON_DASSERT_NOT_REACHED(reason) WESTON_ASSERT_NOT_REACHED(reason)
#define WESTON_DASSERT_ERRNO_EQ(a) WESTON_ASSERT_ERRNO_EQ(a)
#define WESTON_DASSERT_ERRNO_NE(a) WESTON_ASSERT_ERRNO_EQ(a)
#define WESTON_DASSERT_ENUM_EQ(a, b) WESTON_ASSERT_ENUM_EQ(a, b)
#define WESTON_DASSERT_ENUM_NE(a, b) WESTON_ASSERT_ENUM_NE(a, b)

#else

#define WESTON_DASSERT_TRUE(a)
#define WESTON_DASSERT_FALSE(a)
#define WESTON_DASSERT_STR_EQ(a, b)
#define WESTON_DASSERT_STR_NE(a, b)
#define WESTON_DASSERT_PTR_SET(a)
#define WESTON_DASSERT_PTR_NOT_SET(a)
#define WESTON_DASSERT_PTR_EQ(a, b)
#define WESTON_DASSERT_PTR_NE(a, b)
#define WESTON_DASSERT_U8_EQ(a, b)
#define WESTON_DASSERT_U8_NE(a, b)
#define WESTON_DASSERT_U8_GT(a, b)
#define WESTON_DASSERT_U8_GE(a, b)
#define WESTON_DASSERT_U8_LT(a, b)
#define WESTON_DASSERT_U8_LE(a, b)
#define WESTON_DASSERT_U16_EQ(a, b)
#define WESTON_DASSERT_U16_NE(a, b)
#define WESTON_DASSERT_U16_GT(a, b)
#define WESTON_DASSERT_U16_GE(a, b)
#define WESTON_DASSERT_U16_LT(a, b)
#define WESTON_DASSERT_U16_LE(a, b)
#define WESTON_DASSERT_U32_EQ(a, b)
#define WESTON_DASSERT_U32_NE(a, b)
#define WESTON_DASSERT_U32_GT(a, b)
#define WESTON_DASSERT_U32_GE(a, b)
#define WESTON_DASSERT_U32_LT(a, b)
#define WESTON_DASSERT_U32_LE(a, b)
#define WESTON_DASSERT_U64_EQ(a, b)
#define WESTON_DASSERT_U64_NE(a, b)
#define WESTON_DASSERT_U64_GT(a, b)
#define WESTON_DASSERT_U64_GE(a, b)
#define WESTON_DASSERT_U64_LT(a, b)
#define WESTON_DASSERT_U64_LE(a, b)
#define WESTON_DASSERT_UINT_EQ(a, b)
#define WESTON_DASSERT_UINT_NE(a, b)
#define WESTON_DASSERT_UINT_GT(a, b)
#define WESTON_DASSERT_UINT_GE(a, b)
#define WESTON_DASSERT_UINT_LT(a, b)
#define WESTON_DASSERT_UINT_LE(a, b)
#define WESTON_DASSERT_S8_EQ(a, b)
#define WESTON_DASSERT_S8_NE(a, b)
#define WESTON_DASSERT_S8_GT(a, b)
#define WESTON_DASSERT_S8_GE(a, b)
#define WESTON_DASSERT_S8_LT(a, b)
#define WESTON_DASSERT_S8_LE(a, b)
#define WESTON_DASSERT_S16_EQ(a, b)
#define WESTON_DASSERT_S16_NE(a, b)
#define WESTON_DASSERT_S16_GT(a, b)
#define WESTON_DASSERT_S16_GE(a, b)
#define WESTON_DASSERT_S16_LT(a, b)
#define WESTON_DASSERT_S16_LE(a, b)
#define WESTON_DASSERT_S32_EQ(a, b)
#define WESTON_DASSERT_S32_NE(a, b)
#define WESTON_DASSERT_S32_GT(a, b)
#define WESTON_DASSERT_S32_GE(a, b)
#define WESTON_DASSERT_S32_LT(a, b)
#define WESTON_DASSERT_S32_LE(a, b)
#define WESTON_DASSERT_S64_EQ(a, b)
#define WESTON_DASSERT_S64_NE(a, b)
#define WESTON_DASSERT_S64_GT(a, b)
#define WESTON_DASSERT_S64_GE(a, b)
#define WESTON_DASSERT_S64_LT(a, b)
#define WESTON_DASSERT_S64_LE(a, b)
#define WESTON_DASSERT_INT_EQ(a, b)
#define WESTON_DASSERT_INT_NE(a, b)
#define WESTON_DASSERT_INT_GT(a, b)
#define WESTON_DASSERT_INT_GE(a, b)
#define WESTON_DASSERT_INT_LT(a, b)
#define WESTON_DASSERT_INT_LE(a, b)
#define WESTON_DASSERT_F32_EQ(a, b)
#define WESTON_DASSERT_F32_NE(a, b)
#define WESTON_DASSERT_F32_GT(a, b)
#define WESTON_DASSERT_F32_GE(a, b)
#define WESTON_DASSERT_F32_LT(a, b)
#define WESTON_DASSERT_F32_LE(a, b)
#define WESTON_DASSERT_F64_EQ(a, b)
#define WESTON_DASSERT_F64_NE(a, b)
#define WESTON_DASSERT_F64_GT(a, b)
#define WESTON_DASSERT_F64_GE(a, b)
#define WESTON_DASSERT_F64_LT(a, b)
#define WESTON_DASSERT_F64_LE(a, b)
#define WESTON_DASSERT_BIT_SET(value, bit)
#define WESTON_DASSERT_BIT_NOT_SET(value, bit)
#define WESTON_DASSERT_LEGAL_BITS(value, mask)
#define WESTON_DASSERT_NOT_REACHED(reason)
#define WESTON_DASSERT_ERRNO_EQ(a)
#define WESTON_DASSERT_ERRNO_NE(a)
#define WESTON_DASSERT_ENUM_EQ(a, b)
#define WESTON_DASSERT_ENUM_NE(a, b)

#endif /* !defined(NDEBUG) */
