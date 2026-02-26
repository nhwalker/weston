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

#define RENDERERS(s, t)							\
	{								\
		.renderer = WESTON_RENDERER_PIXMAN,			\
		.scale = s,						\
		.transform = WL_OUTPUT_TRANSFORM_ ## t,			\
		.transform_name = #t,					\
		.gl_shadow_fb = false,					\
		.meta.name = "pixman " #s " " #t,			\
	},								\
	{								\
		.renderer = WESTON_RENDERER_GL,				\
		.scale = s,						\
		.transform = WL_OUTPUT_TRANSFORM_ ## t,			\
		.transform_name = #t,					\
		.gl_shadow_fb = false,					\
		.meta.name = "GL no-shadow " #s " " #t,			\
	},								\
	{								\
		.renderer = WESTON_RENDERER_VULKAN,			\
		.scale = s,						\
		.transform = WL_OUTPUT_TRANSFORM_ ## t,			\
		.transform_name = #t,					\
		.gl_shadow_fb = false,					\
		.meta.name = "Vulkan " #s " " #t,			\
	}

struct setup_args {
	struct fixture_metadata meta;
	enum weston_renderer_type renderer;
	int scale;
	enum wl_output_transform transform;
	const char *transform_name;
	bool gl_shadow_fb;
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

	/*
	 * The width and height are chosen to produce 324x240 framebuffer, to
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
	setup.refresh = HIGHEST_OUTPUT_REFRESH;

	if (arg->gl_shadow_fb) {
		/* To skip instead of fail the test if shadow not available */
		setup.test_quirks.required_capabilities = WESTON_CAP_COLOR_OPS;
	}

	return weston_test_harness_execute_as_client(harness, &setup);
}
DECLARE_FIXTURE_SETUP_WITH_ARG(fixture_setup, my_setup_args, meta);

static void
verify_damage(struct client *client, struct wet_testsuite_data *suite_data,
	      struct rectangle expected, int offset_x, int offset_y)
{

	RUN_INSIDE_BREAKPOINT(client, suite_data) {
		struct weston_compositor *compositor = breakpoint->compositor;
		struct weston_output *output = next_output(compositor, NULL);
		pixman_region32_t *damage = breakpoint->data;
		pixman_box32_t *extents = pixman_region32_extents(damage);
		int x1 = MAX(expected.x + offset_x, 0);
		int y1 = MAX(expected.y + offset_y, 0);
		int x2 = MIN(x1 + expected.width, output->width);
		int y2 = MIN(y1 + expected.height, output->height);

		testlog("have (%d, %d) -> (%d, %d); wanted to see (%d, %d) -> (%d, %d)\n",
			extents->x1, extents->y1, extents->x2, extents->y2,
			expected.x + offset_x, expected.y + offset_y,
			expected.x + offset_x + expected.width,
			expected.y + offset_y + expected.height);

		test_assert_enum(breakpoint->template_->breakpoint,
				 WESTON_TEST_BREAKPOINT_POST_REPAINT);
		assert_output_matches(suite_data, output, client->output);
		test_assert_int_eq(pixman_region32_n_rects(damage), 1);
		test_assert_int_eq(extents->x1, x1);
		test_assert_int_eq(extents->y1, y1);
		test_assert_int_eq(extents->x2, x2);
		test_assert_int_eq(extents->y2, y2);
	}
}

static void
commit_buffer_with_damage(struct surface *surface,
			  struct buffer *buffer,
			  struct rectangle damage)
{
	wl_surface_attach(surface->wl_surface, buffer->proxy, 0, 0);
	wl_surface_damage(surface->wl_surface, damage.x, damage.y,
			  damage.width, damage.height);
	wl_surface_commit(surface->wl_surface);
}

/*
 * Test that Weston repaints exactly the damage a client sends to it.
 *
 * NOTE: This relies on the Weston implementation detail that Weston actually
 * will repaint exactly the client's damage and nothing more. This is not
 * generally true of Wayland compositors.
 */
TEST(output_damage)
{
#define COUNT_BUFS 3
	struct wet_testsuite_data *suite_data = TEST_GET_SUITE_DATA();
	const struct setup_args *oargs;
	struct client *client;
	bool match = true;
	char *refname;
	int ret;
	struct buffer *buf[COUNT_BUFS];
	pixman_color_t colors[COUNT_BUFS];
	static const struct rectangle damages[COUNT_BUFS] = {
		{ 0 /* full damage */ },
		{ .x = 10, .y = 10, .width = 20, .height = 10 },
		{ .x = 43, .y = 47, .width = 5, .height = 50 },
	};
	int i;
	const int width = 140;
	const int height = 110;

	color_rgb888(&colors[0], 100, 100, 100); /* grey */
	color_rgb888(&colors[1],   0, 255, 255); /* cyan */
	color_rgb888(&colors[2],   0, 255,   0); /* green */

	oargs = &my_setup_args[get_test_fixture_index()];

	ret = asprintf(&refname, "output-damage_%d-%s",
		       oargs->scale, oargs->transform_name);
	test_assert_int_ne(ret, 0);

	testlog("%s: %s\n", get_test_name(), refname);

	client = create_client();
	client->surface = create_test_surface(client);
	client->surface->width = width;
	client->surface->height = height;

	for (i = 0; i < COUNT_BUFS; i++)
		buf[i] = create_shm_buffer_solid(client, width, height, &colors[i]);

	client->surface->buffer = buf[0];
	move_client_frame_sync(client, 19, 19);

	/*
	 * Each time we commit a buffer with a different color, the damage box
	 * should color just the box on the output.
	 */
	for (i = 1; i < COUNT_BUFS; i++) {
		client_push_breakpoint(client, suite_data,
				       WESTON_TEST_BREAKPOINT_POST_REPAINT,
				       (struct wl_proxy *) client->output->wl_output);
		commit_buffer_with_damage(client->surface, buf[i], damages[i]);
		verify_damage(client, suite_data, damages[i], 19, 19);
	}

	test_assert_true(match);

	for (i = 0; i < COUNT_BUFS; i++)
		buffer_destroy(buf[i]);

	client->surface->buffer = NULL;
	client_destroy(client);
	free(refname);

	return RESULT_OK;
}
