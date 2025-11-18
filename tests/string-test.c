/*
 * Copyright © 2016 Samsung Electronics Co., Ltd
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
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

#include "shared/string-helpers.h"

#include "weston-test-client-helper.h"
#include "weston-test-assert.h"

TEST(strtol_conversions)
{
	bool ret;
	int32_t val = -1;
	char *str = NULL;

	str = ""; val = -1;
	ret = safe_strtoint(str, &val);
	assert_false(ret);
	assert_s32_eq(val, -1);

	str = "."; val = -1;
	ret = safe_strtoint(str, &val);
	assert_false(ret);
	assert_s32_eq(val, -1);

	str = "42"; val = -1;
	ret = safe_strtoint(str, &val);
	assert_true(ret);
	assert_s32_eq(val, 42);

	str = "-42"; val = -1;
	ret = safe_strtoint(str, &val);
	assert_true(ret);
	assert_s32_eq(val, -42);

	str = "0042"; val = -1;
	ret = safe_strtoint(str, &val);
	assert_true(ret);
	assert_s32_eq(val, 42);

	str = "x42"; val = -1;
	ret = safe_strtoint(str, &val);
	assert_false(ret);
	assert_s32_eq(val, -1);

	str = "42x"; val = -1;
	ret = safe_strtoint(str, &val);
	assert_false(ret);
	assert_s32_eq(val, -1);

	str = "0x42424242"; val = -1;
	ret = safe_strtoint(str, &val);
	assert_false(ret);
	assert_s32_eq(val, -1);

	str = "424748364789L"; val = -1;
	ret = safe_strtoint(str, &val);
	assert_false(ret);
	assert_s32_eq(val, -1);

	return RESULT_OK;
}

TEST(strtof_conversions)
{
	float val;

	assert_true(safe_strtofloat("0.0", &val));
	assert_f32_eq(val, 0.0);

	assert_true(safe_strtofloat("-0.25", &val));
	assert_f32_eq(val, -0.25);

	assert_true(safe_strtofloat("  10", &val));
	assert_f32_eq(val, 10.0);

	assert_true(safe_strtofloat("+2.2e-4", &val));
	assert_f32_eq(val, 2.2e-4);

	assert_true(safe_strtofloat("3.3e3", &val));
	assert_f32_eq(val, 3.3e3);

	assert_true(safe_strtofloat("nan", &val));
	assert_true(isnan(val));

	assert_true(safe_strtofloat("inf", &val));
	assert_f32_eq(val, HUGE_VALF);

	assert_true(safe_strtofloat("-inf", &val));
	assert_f32_eq(val, -HUGE_VALF);


	assert_false(safe_strtofloat("", &val));
	assert_int_eq(errno, EINVAL);

	assert_false(safe_strtofloat("x", &val));
	assert_int_eq(errno, EINVAL);

	assert_false(safe_strtofloat("15k", &val));
	assert_int_eq(errno, EINVAL);

	assert_false(safe_strtofloat("b2.2", &val));
	assert_int_eq(errno, EINVAL);

	assert_false(safe_strtofloat("1.3f", &val));
	assert_int_eq(errno, EINVAL);

	assert_false(safe_strtofloat("1e-500", &val));
	assert_int_eq(errno, ERANGE);

	assert_false(safe_strtofloat("1e+500", &val));
	assert_int_eq(errno, ERANGE);

	return RESULT_OK;
}
