/*
 * Copyright © 2020 Collabora, Ltd.
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

#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include "weston-test-client-helper.h"
#include "weston-test-fixture-compositor.h"
#include "weston-test-assert.h"

#define TRANSFORM(x) WL_OUTPUT_TRANSFORM_ ## x, #x
#define RENDERERS(s, t)							\
	{								\
		.renderer = WESTON_RENDERER_PIXMAN,			\
		.scale = s,						\
		.transform = WL_OUTPUT_TRANSFORM_ ## t,			\
		.transform_name = #t,					\
		.meta.name = "pixman " #s " " #t,			\
	},								\
	{								\
		.renderer = WESTON_RENDERER_GL,				\
		.scale = s,						\
		.transform = WL_OUTPUT_TRANSFORM_ ## t,			\
		.transform_name = #t,					\
		.meta.name = "GL " #s " " #t,				\
	},								\
	{								\
		.renderer = WESTON_RENDERER_VULKAN,			\
		.scale = s,						\
		.transform = WL_OUTPUT_TRANSFORM_ ## t,			\
		.transform_name = #t,					\
		.meta.name = "Vulkan " #s " " #t,			\
	}

struct setup_args {
	struct fixture_metadata meta;
	enum weston_renderer_type renderer;
	int scale;
	enum wl_output_transform transform;
	const char *transform_name;
};

static const struct setup_args my_setup_args[] = {
	RENDERERS(1, NORMAL),
	RENDERERS(1, 90),
	RENDERERS(1, 180),
	RENDERERS(1, 270),
	RENDERERS(1, FLIPPED),
	RENDERERS(1, FLIPPED_90),
	RENDERERS(1, FLIPPED_180),
	RENDERERS(1, FLIPPED_270),
	RENDERERS(2, NORMAL),
	RENDERERS(3, NORMAL),
	RENDERERS(2, 90),
	RENDERERS(2, 180),
	RENDERERS(2, FLIPPED),
	RENDERERS(3, FLIPPED_270),
};

static enum test_result_code
fixture_setup(struct weston_test_harness *harness, const struct setup_args *arg)
{
	struct compositor_setup setup;

	/* The width and height are chosen to produce 324x240 framebuffer, to
	 * emulate keeping the video mode constant.
	 * This resolution is divisible by 2 and 3.
	 * Headless multiplies the given size by scale.
	 */

	compositor_setup_defaults(&setup);
	setup.renderer = arg->renderer;
	setup.width = 324 / arg->scale;
	setup.height = 240 / arg->scale;
	setup.scale = arg->scale;
	setup.transform = arg->transform;
	setup.shell = SHELL_TEST_DESKTOP;

	return weston_test_harness_execute_as_client(harness, &setup);
}
DECLARE_FIXTURE_SETUP_WITH_ARG(fixture_setup, my_setup_args, meta);

struct client_buffer_args {
	int scale;
	enum wl_output_transform transform;
	const char *transform_name;
};

static const struct client_buffer_args my_buffer_args[] = {
	{ 1, TRANSFORM(NORMAL) },
	{ 2, TRANSFORM(90) },
};

#define MAX_SCALE 3
struct global_data {
	/* indexed by scale */
	struct client_buffer *test_card[MAX_SCALE + 1];
	/* indexed by buffer scale, buffer transform, output scale, output transform */
	pixman_image_t *ref_image[MAX_SCALE + 1][WL_OUTPUT_TRANSFORM_FLIPPED_270 + 1][MAX_SCALE + 1][WL_OUTPUT_TRANSFORM_FLIPPED_270 + 1];
	char *ref_fname[MAX_SCALE + 1][WL_OUTPUT_TRANSFORM_FLIPPED_270 + 1][MAX_SCALE + 1][WL_OUTPUT_TRANSFORM_FLIPPED_270 + 1];
};

static void *
fixture_init(struct weston_test_harness *harness)
{
	struct global_data *global = zalloc(sizeof(*global));
	size_t b;

	test_assert_ptr_not_null(global);

	for (b = 0; b < ARRAY_LENGTH(my_buffer_args); b++) {
		int bscale = my_buffer_args[b].scale;
		enum wl_output_transform bxform = my_buffer_args[b].transform;
		size_t r;

		if (!global->test_card[bscale]) {
			global->test_card[bscale] =
				client_buffer_from_image_file("basic-test-card", bscale);
			test_assert_ptr_not_null(global->test_card[bscale]);
		}

		testlog("buffer scale: %d, xform %d\n", bscale, bxform);

		for (r = 0; r < ARRAY_LENGTH(my_setup_args); r++) {
			int oscale = my_setup_args[r].scale;
			enum wl_output_transform oxform = my_setup_args[r].transform;
			char *ref_fname;
			pixman_image_t *ref;
			int ret;

			if (global->ref_image[bscale][bxform][oscale][oxform])
				continue;

			ret = asprintf(&ref_fname, "%s/output_%d-%s_buffer_%d-%s-00.png",
				       reference_path(),
				       oscale, my_setup_args[r].transform_name,
				       bscale, my_buffer_args[b].transform_name);
			testlog("\toscale: %d, xform: %d; filename %s\n", oscale, oxform, ref_fname);
			test_assert_int_ne(ret, 0);
			ref = load_image_from_png(ref_fname);
			test_assert_ptr_not_null(ref);
			global->ref_image[bscale][bxform][oscale][oxform] = ref;
			global->ref_fname[bscale][bxform][oscale][oxform] = ref_fname;
		}
	}

	return global;
}

static void
fixture_teardown(struct weston_test_harness *harness, void *data_)
{
	struct global_data *global = data_;
	size_t b;

	for (b = 0; b < ARRAY_LENGTH(my_buffer_args); b++) {
		int bscale = my_buffer_args[b].scale;
		enum wl_output_transform bxform = my_buffer_args[b].transform;
		size_t r;

		if (global->test_card[bscale]) {
			client_buffer_util_destroy_buffer(global->test_card[bscale]);
			global->test_card[bscale] = NULL;
		}

		for (r = 0; r < ARRAY_LENGTH(my_setup_args); r++) {
			int oscale = my_setup_args[r].scale;
			enum wl_output_transform oxform = my_setup_args[r].transform;

			if (!global->ref_image[bscale][bxform][oscale][oxform])
				continue;
			pixman_image_unref(global->ref_image[bscale][bxform][oscale][oxform]);
			global->ref_image[bscale][bxform][oscale][oxform] = NULL;
			free(global->ref_fname[bscale][bxform][oscale][oxform]);
		}
	}

	free(global);
}
DECLARE_FIXTURE_INIT(fixture_init, fixture_teardown);

TEST_P(output_transform, my_buffer_args)
{
	const struct global_data *global = _wet_suite_data->user_data;
	const struct client_buffer_args *bargs = data;
	const struct setup_args *oargs;
	struct client *client;
	struct client_buffer *shot;
	bool match;

	oargs = &my_setup_args[get_test_fixture_index()];

	testlog("%s: %s\n", get_test_name(), global->ref_fname[bargs->scale][bargs->transform][oargs->scale][oargs->transform]);

	/*
	 * NOTE! The transform set below is a lie.
	 * Take that into account when analyzing screenshots.
	 */

	client = create_client();
	client->surface = create_test_surface_with_buffer(client,
							  global->test_card[bargs->scale]);
	wl_surface_set_buffer_scale(client->surface->wl_surface, bargs->scale);
	wl_surface_set_buffer_transform(client->surface->wl_surface,
					bargs->transform);
	move_client(client, 19, 19);

	shot = capture_screenshot_of_output(client, NULL, NO_DECORATIONS);
	test_assert_ptr_not_null(shot);
	match = verify_image(shot,
			     global->ref_image[bargs->scale][bargs->transform][oargs->scale][oargs->transform],
			     global->ref_fname[bargs->scale][bargs->transform][oargs->scale][oargs->transform],
			     NULL, 0);
	test_assert_true(match);

	client_buffer_util_destroy_buffer(shot);
	client_destroy(client);

	return RESULT_OK;
}
