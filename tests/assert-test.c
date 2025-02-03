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

#include "weston-test-runner.h"
#include "weston-test-assert.h"

static void
abort_if_not(bool cond)
{
	if (!cond)
		abort();
}

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

#define weston_assert_my_type_lt(a, b) \
	weston_assert_fn_(my_type_cmp, a, b, const struct my_type *, "my_type %p", <)

TEST(asserts)
{
	bool ret;

	ret = weston_assert_true(false);
	abort_if_not(ret == false);

	ret = weston_assert_true(true);
	abort_if_not(ret);

	ret = weston_assert_false(true);
	abort_if_not(ret == false);

	ret = weston_assert_false(false);
	abort_if_not(ret);

	ret = weston_assert_true(true && false);
	abort_if_not(ret == false);

	ret = weston_assert_ptr_not_null(&ret);
	abort_if_not(ret);

	ret = weston_assert_ptr_not_null(NULL);
	abort_if_not(ret == false);

	ret = weston_assert_ptr_null(NULL);
	abort_if_not(ret);

	ret = weston_assert_ptr_null(&ret);
	abort_if_not(ret == false);

	ret = weston_assert_ptr_eq(&ret, &ret);
	abort_if_not(ret);

	ret = weston_assert_ptr_eq(&ret, &ret + 1);
	abort_if_not(ret == false);

	double fifteen = 15.0;
	ret = weston_assert_double_eq(fifteen, 15.000001);
	abort_if_not(ret == false);

	ret = weston_assert_double_eq(fifteen, 15);
	abort_if_not(ret);

	const char *nom = "bar";
	ret = weston_assert_str_eq(nom, "bar");
	abort_if_not(ret);
	ret = weston_assert_str_eq(nom, "baz");
	abort_if_not(ret == false);

	struct my_type a = { 1, 2.0 };
	struct my_type b = { 0, 2.0 };
	ret = weston_assert_my_type_lt(&b, &a);
	abort_if_not(ret);
	ret = weston_assert_my_type_lt(&a, &b);
	abort_if_not(ret == false);

	uint32_t bitfield = 0xffff;
	ret = weston_assert_bit_is_set(bitfield, 1ull << 2);
	abort_if_not(ret);
	ret = weston_assert_bit_is_set(bitfield, 1ull << 57);
	abort_if_not(ret == false);

	uint64_t max_uint64 = UINT64_MAX;
	ret = weston_assert_uint64_eq(max_uint64, 0);
	abort_if_not(ret == false);

	uint64_t val = 0x200010001000ffff;
	uint64_t msk = 0x00000000fffffff3;
	ret = weston_assert_legal_bits(val, msk);
	abort_if_not(ret == false);

	ret = weston_assert_legal_bits(val, UINT64_MAX);
	abort_if_not(ret);

	/* If we reach that point, it's a success so reset the assert counter
	 * that's been incremented to check that assertions work. */
	weston_assert_counter_reset();
}
