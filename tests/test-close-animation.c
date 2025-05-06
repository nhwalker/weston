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
	setup.shell = SHELL_DESKTOP;
	setup.logging_scopes = "log,test-harness-plugin";
	setup.refresh = HIGHEST_OUTPUT_REFRESH;

	weston_ini_setup(&setup,
			 cfgln("[shell]"),
			 cfgln("close-animation=fade"));

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

static struct weston_view *
get_pnode_view_with_label(struct weston_output *output,
		     struct weston_paint_node *pnode, const char *label)
{
	struct weston_paint_node *pnode_found = NULL;

	while ((pnode = next_pnode_from_z(output, pnode)) != NULL) {
		struct weston_view *view = pnode->view;
		struct weston_surface *surface = view->surface;
		char lbl[128];

		test_assert_ptr_not_null(surface->get_label);
		surface->get_label(surface, lbl, sizeof(lbl));
		if (!strcmp(lbl, label)) {
			pnode_found = pnode;
			break;
		}
	}

	if (!pnode_found)
		return NULL;

	return pnode_found->view;
}

static struct weston_view *
get_desktop_surface_view(struct weston_output *output,
			 struct weston_paint_node *pnode, const char *app_id)
{
	struct weston_paint_node *pnode_found = NULL;

	while ((pnode = next_pnode_from_z(output, pnode)) != NULL) {
		struct weston_view *view = pnode->view;
		struct weston_surface *surface = view->surface;
		struct weston_desktop_surface *wds =
			weston_surface_get_desktop_surface(surface);

		if (!wds)
			continue;

		const char *f_app_id = weston_desktop_surface_get_app_id(wds);

		if (!strcmp(f_app_id, app_id)) {
			pnode_found = pnode;
			break;
		}
	}

	if (!pnode_found)
		return NULL;

	return pnode_found->view;
}

static bool
check_animation_finished(struct weston_view *ev)
{
	/* good enough to know that we've been fading out */
	if (ev->alpha <= 1.0f && ev->alpha >= 0.1f)
		return false;

	return true;
}

TEST(fade_out_close_animation)
{
	struct wet_testsuite_data *suite_data = TEST_GET_SUITE_DATA();
	struct xdg_client *xdg_client = create_xdg_client();
	struct xdg_surface_data *xdg_surface = create_xdg_surface(xdg_client);
	const char *fade_out_label = "surface closing fade out animation";
	const char *app_id = "weston.desktop-shell";
	const char *title = "one";

	test_assert_ptr_not_null(xdg_client);
	test_assert_ptr_not_null(xdg_surface);

	xdg_surface_make_toplevel(xdg_surface, app_id, title);
	xdg_surface_wait_configure(xdg_surface);

	client_push_breakpoint(xdg_client->client, suite_data,
			       WESTON_TEST_BREAKPOINT_POST_REPAINT,
			       (struct wl_proxy *) xdg_client->client->output->wl_output);

	xdg_surface_commit_solid(xdg_surface, 255, 0, 0);

	/* check we're mapped */
	RUN_INSIDE_BREAKPOINT(xdg_client->client, suite_data) {
		struct weston_head *head = breakpoint->resource;
		struct weston_output *output = head->output;
		struct weston_paint_node *pnode =
			next_pnode_from_z(output, NULL);
		struct weston_view *view = pnode->view;
		struct weston_surface *surface = view->surface;
		struct weston_desktop_surface *wds =
			weston_surface_get_desktop_surface(surface);
		const char *wds_title;
		const char *wds_app_id;

		test_assert_enum(breakpoint->template_->breakpoint,
				WESTON_TEST_BREAKPOINT_POST_REPAINT);

		if (wds) {
			wds_title = weston_desktop_surface_get_title(wds);
			wds_app_id = weston_desktop_surface_get_app_id(wds);

			test_assert_ptr_not_null(wds_title);
			test_assert_ptr_not_null(app_id);

			test_assert_str_eq(wds_title, title);
			test_assert_str_eq(wds_app_id, app_id);
		}
	}

	/* we still need to have the view mapped in order to create the
	 * animation so we fake here a client disconnect */
	xdg_wm_base_destroy(xdg_client->xdg_wm_base);

	bool found_surface_label_animation = false;

	/* find out the surface fade out label, here we might time out but
	 * meson /tests would catch that */
	do {
		client_push_breakpoint(xdg_client->client, suite_data,
				       WESTON_TEST_BREAKPOINT_POST_REPAINT,
				       (struct wl_proxy *) xdg_client->client->output->wl_output);

		RUN_INSIDE_BREAKPOINT(xdg_client->client, suite_data) {
			struct weston_head *head = breakpoint->resource;
			struct weston_output *output = head->output;
			struct weston_paint_node *pnode = NULL;
			struct weston_view *view = NULL;

			test_assert_enum(breakpoint->template_->breakpoint,
					WESTON_TEST_BREAKPOINT_POST_REPAINT);

			view = get_pnode_view_with_label(output, pnode, fade_out_label);
			if (view)
				found_surface_label_animation = true;
		}

	} while (!found_surface_label_animation);

	/* check of the fade out animation has gone through */
	bool animation_done = false;
	do {
		client_push_breakpoint(xdg_client->client, suite_data,
				       WESTON_TEST_BREAKPOINT_POST_REPAINT,
				       (struct wl_proxy *) xdg_client->client->output->wl_output);

		RUN_INSIDE_BREAKPOINT(xdg_client->client, suite_data) {
			struct weston_head *head = breakpoint->resource;
			struct weston_output *output = head->output;
			struct weston_paint_node *pnode = NULL;
			struct weston_view *view = NULL;

			test_assert_enum(breakpoint->template_->breakpoint,
					WESTON_TEST_BREAKPOINT_POST_REPAINT);

			view = get_pnode_view_with_label(output, pnode, fade_out_label);
			test_assert_ptr_not_null(view);

			if (check_animation_finished(view)) {
				testlog("animation finished!\n");
				animation_done = true;
			}
		}

	} while (!animation_done);

	client_destroy(xdg_client->client);
	free(xdg_client);

	return RESULT_OK;
}

TEST(move_view_out_of_output_area)
{
	struct wet_testsuite_data *suite_data = TEST_GET_SUITE_DATA();
	struct xdg_client *xdg_client = create_xdg_client();
	struct xdg_surface_data *xdg_surface = create_xdg_surface(xdg_client);

	const char *app_id = "weston.desktop-shell.without.output";
	const char *title = "two";

	test_assert_ptr_not_null(xdg_client);
	test_assert_ptr_not_null(xdg_surface);

	xdg_surface_make_toplevel(xdg_surface, app_id, title);
	xdg_surface_wait_configure(xdg_surface);

	client_push_breakpoint(xdg_client->client, suite_data,
			       WESTON_TEST_BREAKPOINT_POST_REPAINT,
			       (struct wl_proxy *) xdg_client->client->output->wl_output);

	xdg_surface_commit_solid(xdg_surface, 255, 0, 0);

	RUN_INSIDE_BREAKPOINT(xdg_client->client, suite_data) {
		struct weston_head *head = breakpoint->resource;
		struct weston_output *output = head->output;
		struct weston_surface *surface;
		struct weston_desktop_surface *wds;
		struct weston_coord_global pos;
		struct weston_paint_node *pnode =
			next_pnode_from_z(output, NULL);

		struct weston_view *view =
			get_desktop_surface_view(output, pnode, app_id);

		test_assert_ptr_not_null(view);
		test_assert_true(weston_view_is_mapped(view));

		surface = view->surface;
		test_assert_true(weston_surface_is_mapped(surface));

		wds = weston_surface_get_desktop_surface(surface);
		test_assert_ptr_not_null(wds);

		test_assert_enum(breakpoint->template_->breakpoint,
				 WESTON_TEST_BREAKPOINT_POST_REPAINT);

		const char *wds_title = weston_desktop_surface_get_title(wds);

		test_assert_ptr_not_null(wds_title);
		test_assert_str_eq(wds_title, title);
			
		pos.c = weston_coord(400, 400);
		weston_view_set_position(view, pos);
		weston_view_update_transform(view);

	}

	/* Note: it is NOT a bug not calling destroy_xdg_surface here;
	 * as we want the shell go on the closing fade out animation path */
	xdg_client_destroy(xdg_client);

	return RESULT_OK;
}
