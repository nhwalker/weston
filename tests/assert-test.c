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

#include "config.h"

#include <stdlib.h>
#include <unistd.h>

#include "weston-test-runner.h"
#include "weston-test-assert.h"

static void
abort_if_not(bool cond)
{
	if (!cond)
		abort();
}

TEST(boolean_asserts)
{
	bool ret;

	ret = WESTON_ASSERT_TRUE(false);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_TRUE(true);
	abort_if_not(ret);

	ret = WESTON_ASSERT_FALSE(true);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_FALSE(false);
	abort_if_not(ret);

	ret = WESTON_ASSERT_TRUE(true && false);
	abort_if_not(ret == false);

	/* If we reach that point, it's a success so reset the assert counter
	 * that's been incremented to check that assertions work.*/
	weston_assert_counter_reset();
}

TEST(string_asserts)
{
	bool ret;

	ret = WESTON_ASSERT_STR_EQ("Hello world", "Bonjour le monde");
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_STR_NE("Hello world", "Bonjour le monde");
	abort_if_not(ret == true);

	ret = WESTON_ASSERT_STR_EQ("Hello world", "Hello world");
	abort_if_not(ret == true);

	ret = WESTON_ASSERT_STR_NE("Hello world", "Hello world");
	abort_if_not(ret == false);

	weston_assert_counter_reset();
}

TEST(pointer_asserts)
{
	bool ret;

	ret = WESTON_ASSERT_PTR_SET(&ret);
	abort_if_not(ret);

	ret = WESTON_ASSERT_PTR_SET(NULL);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_PTR_NOT_SET(NULL);
	abort_if_not(ret);

	ret = WESTON_ASSERT_PTR_NOT_SET(&ret);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_PTR_EQ(&ret, &ret);
	abort_if_not(ret);

	ret = WESTON_ASSERT_PTR_EQ(&ret, &ret + 1);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_PTR_NE(&ret, &ret);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_PTR_NE(&ret, &ret + 1);
	abort_if_not(ret == true);

	weston_assert_counter_reset();
}

TEST(u8_asserts)
{
	uint8_t a = 1 << 7;
	bool ret;

	ret = WESTON_ASSERT_U8_EQ(a, 0);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_U8_EQ(a, a);
	abort_if_not(ret == true);

	ret = WESTON_ASSERT_U8_NE(a, 0);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_U8_NE(a, a);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_U8_GT(a, UINT8_MAX);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_U8_GT(a, a);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_U8_GT(a, 0);
	abort_if_not(ret == true);

	ret = WESTON_ASSERT_U8_GE(a, UINT8_MAX);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_U8_GE(a, a);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_U8_GE(a, 0);
	abort_if_not(ret == true);

	ret = WESTON_ASSERT_U8_LT(a, UINT8_MAX);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_U8_LT(a, a);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_U8_LT(a, 0);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_U8_LE(a, UINT8_MAX);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_U8_LE(a, a);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_U8_LE(a, 0);
	abort_if_not(ret == false);

	weston_assert_counter_reset();
}

TEST(u16_asserts)
{
	uint16_t a = 1 << 15;
	bool ret;

	ret = WESTON_ASSERT_U16_EQ(a, 0);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_U16_EQ(a, a);
	abort_if_not(ret == true);

	ret = WESTON_ASSERT_U16_NE(a, 0);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_U16_NE(a, a);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_U16_GT(a, UINT16_MAX);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_U16_GT(a, a);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_U16_GT(a, 0);
	abort_if_not(ret == true);

	ret = WESTON_ASSERT_U16_GE(a, UINT16_MAX);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_U16_GE(a, a);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_U16_GE(a, 0);
	abort_if_not(ret == true);

	ret = WESTON_ASSERT_U16_LT(a, UINT16_MAX);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_U16_LT(a, a);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_U16_LT(a, 0);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_U16_LE(a, UINT16_MAX);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_U16_LE(a, a);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_U16_LE(a, 0);
	abort_if_not(ret == false);

	weston_assert_counter_reset();
}

TEST(u32_asserts)
{
	uint32_t a = 1 << 31;
	bool ret;

	ret = WESTON_ASSERT_U32_EQ(a, 0);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_U32_EQ(a, a);
	abort_if_not(ret == true);

	ret = WESTON_ASSERT_U32_NE(a, 0);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_U32_NE(a, a);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_U32_GT(a, UINT32_MAX);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_U32_GT(a, a);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_U32_GT(a, 0);
	abort_if_not(ret == true);

	ret = WESTON_ASSERT_U32_GE(a, UINT32_MAX);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_U32_GE(a, a);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_U32_GE(a, 0);
	abort_if_not(ret == true);

	ret = WESTON_ASSERT_U32_LT(a, UINT32_MAX);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_U32_LT(a, a);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_U32_LT(a, 0);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_U32_LE(a, UINT32_MAX);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_U32_LE(a, a);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_U32_LE(a, 0);
	abort_if_not(ret == false);

	weston_assert_counter_reset();
}

TEST(u64_asserts)
{
	uint64_t a = 1ull << 63;
	bool ret;

	ret = WESTON_ASSERT_U64_EQ(a, 0);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_U64_EQ(a, a);
	abort_if_not(ret == true);

	ret = WESTON_ASSERT_U64_NE(a, 0);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_U64_NE(a, a);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_U64_GT(a, UINT64_MAX);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_U64_GT(a, a);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_U64_GT(a, 0);
	abort_if_not(ret == true);

	ret = WESTON_ASSERT_U64_GE(a, UINT64_MAX);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_U64_GE(a, a);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_U64_GE(a, 0);
	abort_if_not(ret == true);

	ret = WESTON_ASSERT_U64_LT(a, UINT64_MAX);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_U64_LT(a, a);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_U64_LT(a, 0);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_U64_LE(a, UINT64_MAX);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_U64_LE(a, a);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_U64_LE(a, 0);
	abort_if_not(ret == false);

	weston_assert_counter_reset();
}

TEST(uint_asserts)
{
	unsigned int a = 42;
	bool ret;

	ret = WESTON_ASSERT_UINT_EQ(a, 0);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_UINT_EQ(a, a);
	abort_if_not(ret == true);

	ret = WESTON_ASSERT_UINT_NE(a, 0);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_UINT_NE(a, a);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_UINT_GT(a, a + 1);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_UINT_GT(a, a);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_UINT_GT(a, 0);
	abort_if_not(ret == true);

	ret = WESTON_ASSERT_UINT_GE(a, a + 1);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_UINT_GE(a, a);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_UINT_GE(a, 0);
	abort_if_not(ret == true);

	ret = WESTON_ASSERT_UINT_LT(a, a + 1);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_UINT_LT(a, a);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_UINT_LT(a, 0);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_UINT_LE(a, a + 1);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_UINT_LE(a, a);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_UINT_LE(a, 0);
	abort_if_not(ret == false);

	weston_assert_counter_reset();
}

TEST(s8_asserts)
{
	int8_t a = -1;
	bool ret;

	ret = WESTON_ASSERT_S8_EQ(a, 0);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_S8_EQ(a, a);
	abort_if_not(ret == true);

	ret = WESTON_ASSERT_S8_NE(a, 0);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_S8_NE(a, a);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_S8_GT(a, 0);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_S8_GT(a, a);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_S8_GT(a, 0);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_S8_GE(a, 0);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_S8_GE(a, a);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_S8_GE(a, 0);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_S8_LT(a, 0);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_S8_LT(a, a);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_S8_LT(a, 0);
	abort_if_not(ret == true);

	ret = WESTON_ASSERT_S8_LE(a, 0);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_S8_LE(a, a);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_S8_LE(a, 0);
	abort_if_not(ret == true);

	weston_assert_counter_reset();
}

TEST(s16_asserts)
{
	int16_t a = -1;
	bool ret;

	ret = WESTON_ASSERT_S16_EQ(a, 0);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_S16_EQ(a, a);
	abort_if_not(ret == true);

	ret = WESTON_ASSERT_S16_NE(a, 0);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_S16_NE(a, a);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_S16_GT(a, 0);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_S16_GT(a, a);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_S16_GT(a, 0);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_S16_GE(a, 0);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_S16_GE(a, a);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_S16_GE(a, 0);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_S16_LT(a, 0);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_S16_LT(a, a);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_S16_LT(a, 0);
	abort_if_not(ret == true);

	ret = WESTON_ASSERT_S16_LE(a, 0);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_S16_LE(a, a);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_S16_LE(a, 0);
	abort_if_not(ret == true);

	weston_assert_counter_reset();
}

TEST(s32_asserts)
{
	int32_t a = -1;
	bool ret;

	ret = WESTON_ASSERT_S32_EQ(a, 0);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_S32_EQ(a, a);
	abort_if_not(ret == true);

	ret = WESTON_ASSERT_S32_NE(a, 0);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_S32_NE(a, a);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_S32_GT(a, 0);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_S32_GT(a, a);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_S32_GT(a, 0);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_S32_GE(a, 0);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_S32_GE(a, a);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_S32_GE(a, 0);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_S32_LT(a, 0);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_S32_LT(a, a);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_S32_LT(a, 0);
	abort_if_not(ret == true);

	ret = WESTON_ASSERT_S32_LE(a, 0);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_S32_LE(a, a);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_S32_LE(a, 0);
	abort_if_not(ret == true);

	weston_assert_counter_reset();
}

TEST(s64_asserts)
{
	int64_t a = -1;
	bool ret;

	ret = WESTON_ASSERT_S64_EQ(a, 0);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_S64_EQ(a, a);
	abort_if_not(ret == true);

	ret = WESTON_ASSERT_S64_NE(a, 0);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_S64_NE(a, a);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_S64_GT(a, 0);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_S64_GT(a, a);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_S64_GT(a, 0);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_S64_GE(a, 0);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_S64_GE(a, a);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_S64_GE(a, 0);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_S64_LT(a, 0);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_S64_LT(a, a);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_S64_LT(a, 0);
	abort_if_not(ret == true);

	ret = WESTON_ASSERT_S64_LE(a, 0);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_S64_LE(a, a);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_S64_LE(a, 0);
	abort_if_not(ret == true);

	weston_assert_counter_reset();
}

TEST(int_asserts)
{
	int a = -1;
	bool ret;

	ret = WESTON_ASSERT_INT_EQ(a, 0);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_INT_EQ(a, a);
	abort_if_not(ret == true);

	ret = WESTON_ASSERT_INT_NE(a, 0);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_INT_NE(a, a);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_INT_GT(a, 0);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_INT_GT(a, a);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_INT_GT(a, 0);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_INT_GE(a, 0);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_INT_GE(a, a);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_INT_GE(a, 0);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_INT_LT(a, 0);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_INT_LT(a, a);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_INT_LT(a, 0);
	abort_if_not(ret == true);

	ret = WESTON_ASSERT_INT_LE(a, 0);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_INT_LE(a, a);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_INT_LE(a, 0);
	abort_if_not(ret == true);

	weston_assert_counter_reset();
}

TEST(f32_asserts)
{
	float a = -1.23456789;
	bool ret;

	ret = WESTON_ASSERT_F32_EQ(a, 0);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_F32_EQ(a, a);
	abort_if_not(ret == true);

	ret = WESTON_ASSERT_F32_NE(a, 0);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_F32_NE(a, a);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_F32_GT(a, 0);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_F32_GT(a, a);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_F32_GT(a, 0);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_F32_GE(a, 0);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_F32_GE(a, a);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_F32_GE(a, 0);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_F32_LT(a, 0);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_F32_LT(a, a);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_F32_LT(a, 0);
	abort_if_not(ret == true);

	ret = WESTON_ASSERT_F32_LE(a, 0);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_F32_LE(a, a);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_F32_LE(a, 0);
	abort_if_not(ret == true);

	weston_assert_counter_reset();
}

TEST(f64_asserts)
{
	float a = -1.23456789;
	bool ret;

	ret = WESTON_ASSERT_F64_EQ(a, 0);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_F64_EQ(a, a);
	abort_if_not(ret == true);

	ret = WESTON_ASSERT_F64_NE(a, 0);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_F64_NE(a, a);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_F64_GT(a, 0);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_F64_GT(a, a);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_F64_GT(a, 0);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_F64_GE(a, 0);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_F64_GE(a, a);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_F64_GE(a, 0);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_F64_LT(a, 0);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_F64_LT(a, a);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_F64_LT(a, 0);
	abort_if_not(ret == true);

	ret = WESTON_ASSERT_F64_LE(a, 0);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_F64_LE(a, a);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_F64_LE(a, 0);
	abort_if_not(ret == true);

	weston_assert_counter_reset();
}

TEST(bit_asserts)
{
	uint64_t bitfield = 1ull << 42;
	uint64_t val = 0x200010001000ffff;
	uint64_t msk = 0x00000000fffffff3;
	bool ret;

	ret = WESTON_ASSERT_BIT_SET(bitfield, 1ull << 42);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_BIT_SET(bitfield, 1ull << 43);
	abort_if_not(ret == false);

	ret = WESTON_ASSERT_BIT_NOT_SET(bitfield, 1ull << 42);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_BIT_NOT_SET(bitfield, 1ull << 43);
	abort_if_not(ret == true);

	ret = WESTON_ASSERT_LEGAL_BITS(val, msk);
	abort_if_not(ret == false);
	ret = WESTON_ASSERT_LEGAL_BITS(val, UINT64_MAX);
	abort_if_not(ret == true);

	weston_assert_counter_reset();
}

TEST(errno_asserts)
{
	bool ret;

	lseek(0xbadfd, SEEK_CUR, 0);

	ret = WESTON_ASSERT_ERRNO_EQ(EBADF);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_ERRNO_EQ(EAGAIN);
	abort_if_not(ret == false);

	weston_assert_counter_reset();
}

TEST(enum_asserts)
{
	enum EnumAsserts {
		ENUM_ASSERTS_FOO = 0,
		ENUM_ASSERTS_BAR,
		ENUM_ASSERTS_BAZ,
	};

	enum EnumAsserts a = ENUM_ASSERTS_BAZ;
	bool ret;

	ret = WESTON_ASSERT_ENUM_EQ(a, ENUM_ASSERTS_BAZ);
	abort_if_not(ret == true);
	ret = WESTON_ASSERT_ENUM_NE(a, ENUM_ASSERTS_BAZ);
	abort_if_not(ret == false);

	weston_assert_counter_reset();
}

struct custom_type {
	int x;
	float y;
};

static int
custom_type_cmp(const struct custom_type *a,
		const struct custom_type *b)
{
	if (a->x < b->x)
		return -1;
	if (a->x > b->x)
		return 1;
	if (a->y < b->y)
		return -1;
	if (a->y > b->y)
		return 1;

	return 0;
}

#define weston_assert_custom_type_lt(a, b) \
	WESTON_ASSERT_FN_(custom_type_cmp, a, b, const struct custom_type *, \
			  "custom_type %p", <)

TEST(custom_type_asserts)
{
	struct custom_type a = { 1, 2.0 };
	struct custom_type b = { 0, 2.0 };
	bool ret;

	ret = weston_assert_custom_type_lt(&b, &a);
	abort_if_not(ret == true);
	ret = weston_assert_custom_type_lt(&a, &b);
	abort_if_not(ret == false);

	weston_assert_counter_reset();
}
