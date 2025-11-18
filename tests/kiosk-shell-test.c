/*
 * Copyright © 2016-2023 Collabora, Ltd.
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

#include "libweston-internal.h"
#include "libweston/desktop.h"
#include "shared/xalloc.h"
#include "weston-test-client-helper.h"
#include "weston-test-fixture-compositor.h"
#include "weston-test-assert.h"
#include "xdg-client-helper.h"

static enum test_result_code
fixture_setup(struct weston_test_harness *harness)
{
	struct compositor_setup setup;

	compositor_setup_defaults(&setup);
	setup.renderer = WESTON_RENDERER_PIXMAN;
	setup.width = 320;
	setup.height = 240;
	setup.shell = SHELL_KIOSK;
	setup.logging_scopes = "log,test-harness-plugin";
	setup.refresh = HIGHEST_OUTPUT_REFRESH;

	return weston_test_harness_execute_as_client(harness, &setup);
}
DECLARE_FIXTURE_SETUP(fixture_setup);

#define DECLARE_LIST_ITERATOR(name, parent, list, child, link)			\
static child *									\
next_##name(parent *from, child *pos)						\
{										\
	struct wl_list *entry = pos ? &pos->link : &from->list;			\
	child *ret = wl_container_of(entry->next, ret, link);			\
	return (&ret->link == &from->list) ? NULL : ret;			\
}

DECLARE_LIST_ITERATOR(pnode_from_z, struct weston_output, paint_node_z_order_list,
		      struct weston_paint_node, z_order_link);
DECLARE_LIST_ITERATOR(view_from_surface, struct weston_surface, views,
		      struct weston_view, link);


static void
assert_surface_is_background(struct wet_testsuite_data *suite_data,
			     struct weston_surface *surface)
{
	char lbl[128];

	assert_ptr_null(surface->resource);
	assert_ptr_not_null(surface->buffer_ref.buffer);
	assert_enum(surface->buffer_ref.buffer->type, WESTON_BUFFER_SOLID);
	assert_ptr_not_null(surface->output);
	assert_s32_eq(surface->width, surface->output->width);
	assert_s32_eq(surface->height, surface->output->height);
	assert_ptr_not_null(surface->get_label);
	assert_int_ne(surface->get_label(surface, lbl, sizeof(lbl)), 0);
	assert_str_eq(lbl, "kiosk shell background surface");
}

TEST(two_surface_switching)
{
	struct wet_testsuite_data *suite_data = TEST_GET_SUITE_DATA();
	struct xdg_client *xdg_client = create_xdg_client();
	struct xdg_surface_data *xdg_surface1, *xdg_surface2;
	struct input *input;

	assert_ptr_not_null(xdg_client);

	/* move the pointer clearly away from our screenshooting area */
	weston_test_move_pointer(xdg_client->client->test->weston_test,
				 0, 1, 0, 2, 30);

	xdg_surface1 = create_xdg_surface(xdg_client);
	assert_ptr_not_null(xdg_surface1);
	xdg_surface_make_toplevel(xdg_surface1, "weston.test.kiosk", "one");
	xdg_surface_wait_configure(xdg_surface1);
	assert_true(xdg_surface1->configure.fullscreen);
	assert_int_eq(xdg_surface1->configure.width,
		      xdg_client->client->output->width);
	assert_int_eq(xdg_surface1->configure.height,
		      xdg_client->client->output->height);

	client_push_breakpoint(xdg_client->client, suite_data,
			       WESTON_TEST_BREAKPOINT_POST_REPAINT,
			       (struct wl_proxy *) xdg_client->client->output->wl_output);

	xdg_surface_commit_solid(xdg_surface1, 255, 0, 0);

	RUN_INSIDE_BREAKPOINT(xdg_client->client, suite_data) {
		struct weston_head *head = breakpoint->resource;
		struct weston_output *output = head->output;
		struct weston_paint_node *pnode =
			next_pnode_from_z(output, NULL);
		struct weston_view *view = pnode->view;
		struct weston_surface *surface = view->surface;
		struct weston_buffer *buffer = surface->buffer_ref.buffer;
		struct weston_desktop_surface *wds =
			weston_surface_get_desktop_surface(surface);

		assert_enum(breakpoint->template_->breakpoint,
			    WESTON_TEST_BREAKPOINT_POST_REPAINT);
		assert_output_matches(suite_data, output,
				      xdg_client->client->output);
		assert_ptr_not_null(pnode);
		assert_ptr_not_null(surface);
		assert_ptr_not_null(wds);
		assert_ptr_not_null(view);
		assert_ptr_not_null(buffer);

		/* check that our surface is top of the paint node list */
		assert_surface_matches(suite_data, surface, xdg_surface1->surface);
		assert_str_eq(weston_desktop_surface_get_title(wds), "one");
		assert_true(weston_view_is_mapped(view));
		assert_true(weston_surface_is_mapped(surface));

		/* the background should be under that */
		pnode = next_pnode_from_z(output, pnode);
		assert_ptr_not_null(pnode);
		assert_surface_is_background(suite_data, pnode->view->surface);
	}

	wl_display_roundtrip(xdg_client->client->wl_display);
	input = container_of(xdg_client->client->inputs.next, struct input, link);
	assert_ptr_not_null(input);
	assert_ptr_not_null(input->keyboard);
	assert_ptr_eq(input->keyboard->focus, xdg_surface1->surface);

	xdg_surface2 = create_xdg_surface(xdg_client);
	assert_ptr_not_null(xdg_surface2);
	xdg_surface_make_toplevel(xdg_surface2, "weston.test.kiosk", "two");
	xdg_surface_wait_configure(xdg_surface2);
	assert_true(xdg_surface2->configure.fullscreen);
	assert_int_eq(xdg_surface2->configure.width,
		      xdg_client->client->output->width);
	assert_int_eq(xdg_surface2->configure.height,
		      xdg_client->client->output->height);

	client_push_breakpoint(xdg_client->client, suite_data,
			       WESTON_TEST_BREAKPOINT_POST_REPAINT,
			       (struct wl_proxy *) xdg_client->client->output->wl_output);

	xdg_surface_commit_solid(xdg_surface2, 0, 255, 0);

	RUN_INSIDE_BREAKPOINT(xdg_client->client, suite_data) {
		struct weston_head *head = breakpoint->resource;
		struct weston_output *output = head->output;
		struct weston_paint_node *pnode =
			next_pnode_from_z(output, NULL);
		struct weston_view *view = pnode->view;
		struct weston_surface *surface = view->surface;
		struct weston_buffer *buffer = surface->buffer_ref.buffer;
		struct weston_desktop_surface *wds =
			weston_surface_get_desktop_surface(surface);

		assert_enum(breakpoint->template_->breakpoint,
			    WESTON_TEST_BREAKPOINT_POST_REPAINT);
		assert_output_matches(suite_data, output,
				      xdg_client->client->output);
		assert_ptr_not_null(pnode);
		assert_ptr_not_null(surface);
		assert_ptr_not_null(wds);
		assert_ptr_not_null(view);
		assert_ptr_not_null(buffer);

		/* check that our surface is top of the paint node list */
		assert_surface_matches(suite_data, surface, xdg_surface2->surface);
		assert_str_eq(weston_desktop_surface_get_title(wds), "two");
		assert_true(weston_surface_is_mapped(surface));
		assert_true(weston_view_is_mapped(view));

		/* the background should be under that */
		pnode = next_pnode_from_z(output, pnode);
		assert_ptr_not_null(pnode);
		assert_surface_is_background(suite_data, pnode->view->surface);
	}

	wl_display_roundtrip(xdg_client->client->wl_display);
	assert_ptr_eq(input->keyboard->focus, xdg_surface2->surface);
	destroy_xdg_surface(xdg_surface2);

	client_push_breakpoint(xdg_client->client, suite_data,
			       WESTON_TEST_BREAKPOINT_POST_REPAINT,
			       (struct wl_proxy *) xdg_client->client->output->wl_output);

	RUN_INSIDE_BREAKPOINT(xdg_client->client, suite_data) {
		struct weston_head *head = breakpoint->resource;
		struct weston_output *output = head->output;
		struct weston_paint_node *pnode =
			next_pnode_from_z(output, NULL);
		struct weston_view *view = pnode->view;
		struct weston_surface *surface = view->surface;
		struct weston_buffer *buffer = surface->buffer_ref.buffer;
		struct weston_desktop_surface *wds =
			weston_surface_get_desktop_surface(surface);

		assert_enum(breakpoint->template_->breakpoint,
			    WESTON_TEST_BREAKPOINT_POST_REPAINT);
		assert_output_matches(suite_data, output,
				      xdg_client->client->output);
		assert_ptr_not_null(pnode);
		assert_ptr_not_null(surface);
		assert_ptr_not_null(wds);
		assert_ptr_not_null(view);
		assert_ptr_not_null(buffer);

		/* check that our surface is top of the paint node list */
		assert_surface_matches(suite_data, surface, xdg_surface1->surface);
		assert_ptr_not_null(surface->resource);
		assert_true(weston_view_is_mapped(view));
		assert_true(weston_surface_is_mapped(surface));
		assert_str_eq(weston_desktop_surface_get_title(wds), "one");
	}

	wl_display_roundtrip(xdg_client->client->wl_display);
	assert_ptr_eq(input->keyboard->focus, xdg_surface1->surface);

	destroy_xdg_surface(xdg_surface1);
	xdg_client_destroy(xdg_client);

	return RESULT_OK;
}

TEST(top_surface_present_in_output_repaint)
{
	struct wet_testsuite_data *suite_data = TEST_GET_SUITE_DATA();
	struct xdg_client *xdg_client = create_xdg_client();
	struct xdg_surface_data *xdg_surface = create_xdg_surface(xdg_client);

	assert_ptr_not_null(xdg_client);
	assert_ptr_not_null(xdg_surface);

	/* move the pointer clearly away from our screenshooting area */
	weston_test_move_pointer(xdg_client->client->test->weston_test,
				 0, 1, 0, 2, 30);

	xdg_surface_make_toplevel(xdg_surface, "weston.test.kiosk", "one");
	xdg_surface_wait_configure(xdg_surface);
	assert_true(xdg_surface->configure.fullscreen);
	assert_int_eq(xdg_surface->configure.width,
		      xdg_client->client->output->width);
	assert_int_eq(xdg_surface->configure.height,
		      xdg_client->client->output->height);

	client_push_breakpoint(xdg_client->client, suite_data,
			       WESTON_TEST_BREAKPOINT_POST_REPAINT,
			       (struct wl_proxy *) xdg_client->client->output->wl_output);

	xdg_surface_commit_solid(xdg_surface, 255, 0, 0);

	RUN_INSIDE_BREAKPOINT(xdg_client->client, suite_data) {
		struct weston_head *head = breakpoint->resource;
		struct weston_output *output = head->output;
		struct weston_paint_node *pnode =
			next_pnode_from_z(output, NULL);
		struct weston_view *view = pnode->view;
		struct weston_surface *surface = view->surface;
		struct weston_buffer *buffer = surface->buffer_ref.buffer;

		assert_enum(breakpoint->template_->breakpoint,
			    WESTON_TEST_BREAKPOINT_POST_REPAINT);
		assert_output_matches(suite_data, output, xdg_client->client->output);
		assert_ptr_not_null(pnode);
		assert_ptr_not_null(surface);
		assert_ptr_not_null(view);
		assert_ptr_not_null(buffer);

		/* check that our surface is top of the paint node list */
		assert_surface_matches(suite_data, surface, xdg_surface->surface);
		assert_true(weston_view_is_mapped(view));
		assert_true(weston_surface_is_mapped(surface));
	}

	destroy_xdg_surface(xdg_surface);
	xdg_client_destroy(xdg_client);

	return RESULT_OK;
}

TEST(test_surface_unmaps_on_null)
{
	struct wet_testsuite_data *suite_data = TEST_GET_SUITE_DATA();
	struct xdg_client *xdg_client = create_xdg_client();
	struct xdg_surface_data *xdg_surface = create_xdg_surface(xdg_client);;

	assert_ptr_not_null(xdg_client);
	assert_ptr_not_null(xdg_surface);

	/* move the pointer clearly away from our screenshooting area */
	weston_test_move_pointer(xdg_client->client->test->weston_test,
				 0, 1, 0, 2, 30);

	xdg_surface_make_toplevel(xdg_surface, "weston.test.kiosk", "one");
	xdg_surface_wait_configure(xdg_surface);
	assert_true(xdg_surface->configure.fullscreen);
	assert_int_eq(xdg_surface->configure.width,
		      xdg_client->client->output->width);
	assert_int_eq(xdg_surface->configure.height,
		      xdg_client->client->output->height);

	client_push_breakpoint(xdg_client->client, suite_data,
			       WESTON_TEST_BREAKPOINT_POST_REPAINT,
			       (struct wl_proxy *) xdg_client->client->output->wl_output);

	xdg_surface_commit_solid(xdg_surface, 255, 0, 0);

	RUN_INSIDE_BREAKPOINT(xdg_client->client, suite_data) {
		struct weston_head *head = breakpoint->resource;
		struct weston_output *output = head->output;
		struct weston_paint_node *pnode =
			next_pnode_from_z(output, NULL);
		struct weston_view *view = pnode->view;
		struct weston_surface *surface = view->surface;

		/* Check that our surface is being shown on top */
		assert_enum(breakpoint->template_->breakpoint,
			    WESTON_TEST_BREAKPOINT_POST_REPAINT);
		assert_ptr_not_null(pnode);
		assert_ptr_not_null(surface);
		assert_ptr_not_null(view);
		assert_surface_matches(suite_data, surface, xdg_surface->surface);
		assert_output_matches(suite_data, surface->output,
				      xdg_client->client->output);
		assert_true(weston_view_is_mapped(view));
		assert_true(weston_surface_is_mapped(surface));
	}

	wl_surface_attach(xdg_surface->surface->wl_surface, NULL, 0, 0);
	wl_surface_commit(xdg_surface->surface->wl_surface);

	client_push_breakpoint(xdg_client->client, suite_data,
			       WESTON_TEST_BREAKPOINT_POST_REPAINT,
			       (struct wl_proxy *) xdg_client->client->output->wl_output);

	RUN_INSIDE_BREAKPOINT(xdg_client->client, suite_data) {
		struct weston_head *head = breakpoint->resource;
		struct weston_output *output = head->output;
		struct weston_paint_node *pnode =
			next_pnode_from_z(output, NULL);
		struct weston_view *view = pnode->view;
		struct weston_surface *surface = view->surface;
		struct weston_buffer *buffer = surface->buffer_ref.buffer;

		assert_enum(breakpoint->template_->breakpoint,
			    WESTON_TEST_BREAKPOINT_POST_REPAINT);

		/* Check that the background is being shown on top. */
		assert_ptr_not_null(pnode);
		assert_ptr_not_null(surface);
		assert_ptr_not_null(view);
		assert_ptr_not_null(buffer);
		assert_surface_is_background(suite_data, surface);

		/* Check that kiosk-shell's view of our surface has been
		 * unmapped, and that there aren't any more views. */
		surface = get_resource_data_from_proxy(suite_data,
						      (struct wl_proxy *) xdg_surface->surface->wl_surface);
		assert_false(weston_surface_is_mapped(surface));
		assert_ptr_null(surface->buffer_ref.buffer);
		assert_ptr_null(surface->output);
		view = next_view_from_surface(surface, NULL);
		assert_false(weston_view_is_mapped(view));
		assert_ptr_null(next_view_from_surface(surface, view));
	}

	destroy_xdg_surface(xdg_surface);
	xdg_client_destroy(xdg_client);

	return RESULT_OK;
}
