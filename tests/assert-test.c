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

#include "config.h"

#include <stdlib.h>

#include "weston-test-assert.h"
#include "weston-test-runner.h"

static void
abort_if_not(bool cond)
{
	if (!cond)
		abort();
}

enum my_enum {
	MY_ENUM_A,
	MY_ENUM_B,
};

struct my_type {
	int x;
	float y;
};

/* Demonstration of custom type comparison */
static int
my_type_cmp(const struct my_type *a, const struct my_type *b)
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

#define assert_my_type_lt(a, b) \
	assert_fn_(my_type_cmp, a, b, const struct my_type *, "my_type %p", <)

TEST(asserts_custom)
{
	bool ret;

	struct my_type a = { 1, 2.0 };
	struct my_type b = { 0, 2.0 };

	ret = assert_my_type_lt(&b, &a);
	abort_if_not(ret);
	ret = assert_my_type_lt(&a, &b);
	abort_if_not(ret == false);

	/* If we reach that point, it's a success so reset the assert counter
	 * that's been incremented to check that assertions work. */
	weston_assert_counter_reset();

	return RESULT_OK;
}

TEST(asserts_boolean)
{
	bool ret;

	ret = assert_true(false);
	abort_if_not(ret == false);
	ret = assert_true(true);
	abort_if_not(ret);
	ret = assert_false(true);
	abort_if_not(ret == false);
	ret = assert_false(false);
	abort_if_not(ret);
	ret = assert_true(true && false);
	abort_if_not(ret == false);

	/* If we reach that point, it's a success so reset the assert counter
	 * that's been incremented to check that assertions work. */
	weston_assert_counter_reset();

	return RESULT_OK;
}

TEST(asserts_pointer)
{
	bool ret;

	ret = assert_ptr_not_null(&ret);
	abort_if_not(ret);
	ret = assert_ptr_not_null(NULL);
	abort_if_not(ret == false);

	ret = assert_ptr_null(NULL);
	abort_if_not(ret);
	ret = assert_ptr_null(&ret);
	abort_if_not(ret == false);

	ret = assert_ptr_eq(&ret, &ret);
	abort_if_not(ret);
	ret = assert_ptr_eq(&ret, &ret + 1);
	abort_if_not(ret == false);

	ret = assert_ptr_ne(&ret, &ret + 1);
	abort_if_not(ret);
	ret = assert_ptr_ne(&ret, &ret);
	abort_if_not(ret == false);

	/* If we reach that point, it's a success so reset the assert counter
	 * that's been incremented to check that assertions work. */
	weston_assert_counter_reset();

	return RESULT_OK;
}

TEST(asserts_string)
{
	bool ret;

	const char *nom = "bar";

	ret = assert_str_eq(nom, "bar");
	abort_if_not(ret);
	ret = assert_str_eq(nom, "baz");
	abort_if_not(ret == false);

	/* If we reach that point, it's a success so reset the assert counter
	 * that's been incremented to check that assertions work. */
	weston_assert_counter_reset();

	return RESULT_OK;
}

TEST(asserts_bitmask)
{
	bool ret;

	uint32_t bitfield = 0xffff;

	ret = assert_bit_set(bitfield, 1ull << 2);
	abort_if_not(ret);
	ret = assert_bit_set(bitfield, 1ull << 57);
	abort_if_not(ret == false);

	/* If we reach that point, it's a success so reset the assert counter
	 * that's been incremented to check that assertions work. */
	weston_assert_counter_reset();

	return RESULT_OK;
}

TEST(asserts_misc)
{
	bool ret;

	ret = assert_enum(MY_ENUM_A, MY_ENUM_A);
	abort_if_not(ret);
	ret = assert_enum(MY_ENUM_A, MY_ENUM_B);
	abort_if_not(ret == false);

	/* assert_not_reached is a bit awkward to test, so let's skip */

	/* If we reach that point, it's a success so reset the assert counter
	 * that's been incremented to check that assertions work. */
	weston_assert_counter_reset();

	return RESULT_OK;
}

TEST(asserts_floating_point)
{
	bool ret;

	/* Float asserts. */

	float sixteen = 16.0;
	ret = assert_f32_eq(sixteen, 16.000001);
	abort_if_not(ret == false);
	ret = assert_f32_eq(sixteen, 16);
	abort_if_not(ret);

	ret = assert_f32_ne(sixteen, 16.000001);
	abort_if_not(ret);
	ret = assert_f32_ne(sixteen, sixteen);
	abort_if_not(ret == false);

	ret = assert_f32_gt(16.000001, sixteen);
	abort_if_not(ret);
	ret = assert_f32_gt(sixteen, 16.000001);
	abort_if_not(ret == false);

	ret = assert_f32_ge(sixteen, sixteen);
	abort_if_not(ret);
	ret = assert_f32_ge(16.000001, sixteen);
	abort_if_not(ret);
	ret = assert_f32_ge(sixteen, 16.000001);
	abort_if_not(ret == false);

	ret = assert_f32_lt(sixteen, 16.000001);
	abort_if_not(ret);
	ret = assert_f32_lt(16.000001, sixteen);
	abort_if_not(ret == false);

	ret = assert_f32_le(sixteen, sixteen);
	abort_if_not(ret);
	ret = assert_f32_le(sixteen, 16.000001);
	abort_if_not(ret);
	ret = assert_f32_le(16.000001, sixteen);
	abort_if_not(ret == false);

	/* Double asserts. */

	double fifteen = 15.0;
	ret = assert_f64_eq(fifteen, 15.000001);
	abort_if_not(ret == false);
	ret = assert_f64_eq(fifteen, 15);
	abort_if_not(ret);

	ret = assert_f64_ne(fifteen, 15.000001);
	abort_if_not(ret);
	ret = assert_f64_ne(fifteen, fifteen);
	abort_if_not(ret == false);

	ret = assert_f64_gt(15.000001, fifteen);
	abort_if_not(ret);
	ret = assert_f64_gt(fifteen, 15.000001);
	abort_if_not(ret == false);

	ret = assert_f64_ge(fifteen, fifteen);
	abort_if_not(ret);
	ret = assert_f64_ge(15.000001, fifteen);
	abort_if_not(ret);
	ret = assert_f64_ge(fifteen, 15.000001);
	abort_if_not(ret == false);

	ret = assert_f64_lt(fifteen, 15.000001);
	abort_if_not(ret);
	ret = assert_f64_lt(15.000001, fifteen);
	abort_if_not(ret == false);

	ret = assert_f64_le(fifteen, fifteen);
	abort_if_not(ret);
	ret = assert_f64_le(fifteen, 15.000001);
	abort_if_not(ret);
	ret = assert_f64_le(15.000001, fifteen);
	abort_if_not(ret == false);

	/* If we reach that point, it's a success so reset the assert counter
	 * that's been incremented to check that assertions work. */
	weston_assert_counter_reset();

	return RESULT_OK;
}

TEST(asserts_unsigned_int)
{
	bool ret;

	/* uint8_t asserts. */

	ret = assert_u8_eq(5, 5);
	abort_if_not(ret);
	ret = assert_u8_eq(5, 6);
	abort_if_not(ret == false);

	ret = assert_u8_ne(5, 6);
	abort_if_not(ret);
	ret = assert_u8_ne(5, 5);
	abort_if_not(ret == false);

	ret = assert_u8_gt(6, 5);
	abort_if_not(ret);
	ret = assert_u8_gt(5, 6);
	abort_if_not(ret == false);

	ret = assert_u8_ge(6, 5);
	abort_if_not(ret);
	ret = assert_u8_ge(5, 5);
	abort_if_not(ret);
	ret = assert_u8_ge(5, 6);
	abort_if_not(ret == false);

	ret = assert_u8_lt(5, 6);
	abort_if_not(ret);
	ret = assert_u8_lt(6, 5);
	abort_if_not(ret == false);

	ret = assert_u8_le(5, 6);
	abort_if_not(ret);
	ret = assert_u8_le(5, 5);
	abort_if_not(ret);
	ret = assert_u8_le(6, 5);
	abort_if_not(ret == false);

	/* uint16_t asserts. */

	ret = assert_u16_eq(5, 5);
	abort_if_not(ret);
	ret = assert_u16_eq(5, 6);
	abort_if_not(ret == false);

	ret = assert_u16_ne(5, 6);
	abort_if_not(ret);
	ret = assert_u16_ne(5, 5);
	abort_if_not(ret == false);

	ret = assert_u16_gt(6, 5);
	abort_if_not(ret);
	ret = assert_u16_gt(5, 6);
	abort_if_not(ret == false);

	ret = assert_u16_ge(6, 5);
	abort_if_not(ret);
	ret = assert_u16_ge(5, 5);
	abort_if_not(ret);
	ret = assert_u16_ge(5, 6);
	abort_if_not(ret == false);

	ret = assert_u16_lt(5, 6);
	abort_if_not(ret);
	ret = assert_u16_lt(6, 5);
	abort_if_not(ret == false);

	ret = assert_u16_le(5, 6);
	abort_if_not(ret);
	ret = assert_u16_le(5, 5);
	abort_if_not(ret);
	ret = assert_u16_le(6, 5);
	abort_if_not(ret == false);

	/* uint32_t asserts. */

	ret = assert_u32_eq(5, 5);
	abort_if_not(ret);
	ret = assert_u32_eq(5, 6);
	abort_if_not(ret == false);

	ret = assert_u32_ne(5, 6);
	abort_if_not(ret);
	ret = assert_u32_ne(5, 5);
	abort_if_not(ret == false);

	ret = assert_u32_gt(6, 5);
	abort_if_not(ret);
	ret = assert_u32_gt(5, 6);
	abort_if_not(ret == false);

	ret = assert_u32_ge(6, 5);
	abort_if_not(ret);
	ret = assert_u32_ge(5, 5);
	abort_if_not(ret);
	ret = assert_u32_ge(5, 6);
	abort_if_not(ret == false);

	ret = assert_u32_lt(5, 6);
	abort_if_not(ret);
	ret = assert_u32_lt(6, 5);
	abort_if_not(ret == false);

	ret = assert_u32_le(5, 6);
	abort_if_not(ret);
	ret = assert_u32_le(5, 5);
	abort_if_not(ret);
	ret = assert_u32_le(6, 5);
	abort_if_not(ret == false);

	/* uint64_t asserts. */

	ret = assert_u64_eq(5, 5);
	abort_if_not(ret);
	ret = assert_u64_eq(5, 6);
	abort_if_not(ret == false);

	ret = assert_u64_ne(5, 6);
	abort_if_not(ret);
	ret = assert_u64_ne(5, 5);
	abort_if_not(ret == false);

	ret = assert_u64_gt(6, 5);
	abort_if_not(ret);
	ret = assert_u64_gt(5, 6);
	abort_if_not(ret == false);

	ret = assert_u64_ge(6, 5);
	abort_if_not(ret);
	ret = assert_u64_ge(5, 5);
	abort_if_not(ret);
	ret = assert_u64_ge(5, 6);
	abort_if_not(ret == false);

	ret = assert_u64_lt(5, 6);
	abort_if_not(ret);
	ret = assert_u64_lt(6, 5);
	abort_if_not(ret == false);

	ret = assert_u64_le(5, 6);
	abort_if_not(ret);
	ret = assert_u64_le(5, 5);
	abort_if_not(ret);
	ret = assert_u64_le(6, 5);
	abort_if_not(ret == false);

	/* unsigned int asserts. */

	ret = assert_uint_eq(5, 5);
	abort_if_not(ret);
	ret = assert_uint_eq(5, 6);
	abort_if_not(ret == false);

	ret = assert_uint_ne(5, 6);
	abort_if_not(ret);
	ret = assert_uint_ne(5, 5);
	abort_if_not(ret == false);

	ret = assert_uint_gt(6, 5);
	abort_if_not(ret);
	ret = assert_uint_gt(5, 6);
	abort_if_not(ret == false);

	ret = assert_uint_ge(6, 5);
	abort_if_not(ret);
	ret = assert_uint_ge(5, 5);
	abort_if_not(ret);
	ret = assert_uint_ge(5, 6);
	abort_if_not(ret == false);

	ret = assert_uint_lt(5, 6);
	abort_if_not(ret);
	ret = assert_uint_lt(6, 5);
	abort_if_not(ret == false);

	ret = assert_uint_le(5, 6);
	abort_if_not(ret);
	ret = assert_uint_le(5, 5);
	abort_if_not(ret);
	ret = assert_uint_le(6, 5);
	abort_if_not(ret == false);

	/* If we reach that point, it's a success so reset the assert counter
	 * that's been incremented to check that assertions work. */
	weston_assert_counter_reset();

	return RESULT_OK;
}

TEST(asserts_signed_int)
{
	bool ret;

	/* int8_t asserts. */

	ret = assert_s8_eq(-5, -5);
	abort_if_not(ret);
	ret = assert_s8_eq(-5, -6);
	abort_if_not(ret == false);

	ret = assert_s8_ne(-5, -6);
	abort_if_not(ret);
	ret = assert_s8_ne(-5, -5);
	abort_if_not(ret == false);

	ret = assert_s8_gt(-5, -6);
	abort_if_not(ret);
	ret = assert_s8_gt(-6, -5);
	abort_if_not(ret == false);

	ret = assert_s8_ge(-5, -6);
	abort_if_not(ret);
	ret = assert_s8_ge(-5, -5);
	abort_if_not(ret);
	ret = assert_s8_ge(-6, -5);
	abort_if_not(ret == false);

	ret = assert_s8_lt(-6, -5);
	abort_if_not(ret);
	ret = assert_s8_lt(-5, -6);
	abort_if_not(ret == false);

	ret = assert_s8_le(-6, -5);
	abort_if_not(ret);
	ret = assert_s8_le(-5, -5);
	abort_if_not(ret);
	ret = assert_s8_le(-5, -6);
	abort_if_not(ret == false);

	/* int16_t asserts. */

	ret = assert_s16_eq(-5, -5);
	abort_if_not(ret);
	ret = assert_s16_eq(-5, -6);
	abort_if_not(ret == false);

	ret = assert_s16_ne(-5, -6);
	abort_if_not(ret);
	ret = assert_s16_ne(-5, -5);
	abort_if_not(ret == false);

	ret = assert_s16_gt(-5, -6);
	abort_if_not(ret);
	ret = assert_s16_gt(-6, -5);
	abort_if_not(ret == false);

	ret = assert_s16_ge(-5, -6);
	abort_if_not(ret);
	ret = assert_s16_ge(-5, -5);
	abort_if_not(ret);
	ret = assert_s16_ge(-6, -5);
	abort_if_not(ret == false);

	ret = assert_s16_lt(-6, -5);
	abort_if_not(ret);
	ret = assert_s16_lt(-5, -6);
	abort_if_not(ret == false);

	ret = assert_s16_le(-6, -5);
	abort_if_not(ret);
	ret = assert_s16_le(-5, -5);
	abort_if_not(ret);
	ret = assert_s16_le(-5, -6);
	abort_if_not(ret == false);

	/* int32_t asserts. */

	ret = assert_s32_eq(-5, -5);
	abort_if_not(ret);
	ret = assert_s32_eq(-5, -6);
	abort_if_not(ret == false);

	ret = assert_s32_ne(-5, -6);
	abort_if_not(ret);
	ret = assert_s32_ne(-5, -5);
	abort_if_not(ret == false);

	ret = assert_s32_gt(-5, -6);
	abort_if_not(ret);
	ret = assert_s32_gt(-6, -5);
	abort_if_not(ret == false);

	ret = assert_s32_ge(-5, -6);
	abort_if_not(ret);
	ret = assert_s32_ge(-5, -5);
	abort_if_not(ret);
	ret = assert_s32_ge(-6, -5);
	abort_if_not(ret == false);

	ret = assert_s32_lt(-6, -5);
	abort_if_not(ret);
	ret = assert_s32_lt(-5, -6);
	abort_if_not(ret == false);

	ret = assert_s32_le(-6, -5);
	abort_if_not(ret);
	ret = assert_s32_le(-5, -5);
	abort_if_not(ret);
	ret = assert_s32_le(-5, -6);
	abort_if_not(ret == false);

	/* int64_t asserts. */

	ret = assert_s64_eq(-5, -5);
	abort_if_not(ret);
	ret = assert_s64_eq(-5, -6);
	abort_if_not(ret == false);

	ret = assert_s64_ne(-5, -6);
	abort_if_not(ret);
	ret = assert_s64_ne(-5, -5);
	abort_if_not(ret == false);

	ret = assert_s64_gt(-5, -6);
	abort_if_not(ret);
	ret = assert_s64_gt(-6, -5);
	abort_if_not(ret == false);

	ret = assert_s64_ge(-5, -6);
	abort_if_not(ret);
	ret = assert_s64_ge(-5, -5);
	abort_if_not(ret);
	ret = assert_s64_ge(-6, -5);
	abort_if_not(ret == false);

	ret = assert_s64_lt(-6, -5);
	abort_if_not(ret);
	ret = assert_s64_lt(-5, -6);
	abort_if_not(ret == false);

	ret = assert_s64_le(-6, -5);
	abort_if_not(ret);
	ret = assert_s64_le(-5, -5);
	abort_if_not(ret);
	ret = assert_s64_le(-5, -6);
	abort_if_not(ret == false);

	/* int asserts. */

	ret = assert_int_eq(-5, -5);
	abort_if_not(ret);
	ret = assert_int_eq(-5, -6);
	abort_if_not(ret == false);

	ret = assert_int_ne(-5, -6);
	abort_if_not(ret);
	ret = assert_int_ne(-5, -5);
	abort_if_not(ret == false);

	ret = assert_int_gt(-5, -6);
	abort_if_not(ret);
	ret = assert_int_gt(-6, -5);
	abort_if_not(ret == false);

	ret = assert_int_ge(-5, -6);
	abort_if_not(ret);
	ret = assert_int_ge(-5, -5);
	abort_if_not(ret);
	ret = assert_int_ge(-6, -5);
	abort_if_not(ret == false);

	ret = assert_int_lt(-6, -5);
	abort_if_not(ret);
	ret = assert_int_lt(-5, -6);
	abort_if_not(ret == false);

	ret = assert_int_le(-6, -5);
	abort_if_not(ret);
	ret = assert_int_le(-5, -5);
	abort_if_not(ret);
	ret = assert_int_le(-5, -6);
	abort_if_not(ret == false);

	/* If we reach that point, it's a success so reset the assert counter
	 * that's been incremented to check that assertions work. */
	weston_assert_counter_reset();

	return RESULT_OK;
}
