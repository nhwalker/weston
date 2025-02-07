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
#include "xdg-shell-client-protocol.h"

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



/*
 * DO NOT COPY THIS CODE INTO ANOTHER FILE.
 *
 * This code is intended to be the starting point of a set of more generic XDG
 * support code to go into weston-test-client-helper.c. If you are the second
 * person here, please merge it into there, modify it as appropriate for your
 * needs (whilst keeping this test working), and cc @daniels on the MR.
 *
 * Thanks!
 */
struct xdg_client {
	struct client *client;
	struct xdg_wm_base *xdg_wm_base;
};

struct xdg_surface_data {
	struct xdg_client *xdg_client;
	struct surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;
	struct xdg_popup *xdg_popup;

	struct xdg_surface *xdg_parent;
	struct wl_list parent_link;
	struct wl_list child_list;

	struct {
		int width;
		int height;
		bool fullscreen;
		bool maximized;
		bool resizing;
		bool activated;
		uint32_t serial; /* != 0 if configure pending */
	} configure;

	struct {
		bool fullscreen;
		bool maximized;
	} target;
};

static void
handle_xdg_toplevel_configure(void *data, struct xdg_toplevel *toplevel,
			      int32_t width, int32_t height,
			      struct wl_array *states)
{
	struct xdg_surface_data *surface = data;
	uint32_t *state;

	surface->configure.width = width;
	surface->configure.height = height;
	surface->configure.fullscreen = false;
	surface->configure.maximized = false;
	surface->configure.resizing = false;
	surface->configure.activated = false;

	wl_array_for_each(state, states) {
		if (*state == XDG_TOPLEVEL_STATE_FULLSCREEN)
			surface->configure.fullscreen = true;
		else if (*state == XDG_TOPLEVEL_STATE_MAXIMIZED)
			surface->configure.maximized = true;
		else if (*state == XDG_TOPLEVEL_STATE_RESIZING)
			surface->configure.resizing = true;
		else if (*state == XDG_TOPLEVEL_STATE_ACTIVATED)
			surface->configure.activated = true;
	}
}

static void
handle_xdg_toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
}

static void
handle_xdg_toplevel_configure_bounds(void *data, struct xdg_toplevel *toplevel,
				     int32_t width, int32_t height)
{
}

static void
handle_xdg_toplevel_wm_capabilities(void *data, struct xdg_toplevel *toplevel,
				    struct wl_array *capabilities)
{
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
	handle_xdg_toplevel_configure,
	handle_xdg_toplevel_close,
	handle_xdg_toplevel_configure_bounds,
	handle_xdg_toplevel_wm_capabilities,
};

static void
handle_xdg_surface_configure(void *data, struct xdg_surface *wm_surface,
			     uint32_t serial)
{
	struct xdg_surface_data *surface = data;

	surface->configure.serial = serial;
}

static const struct xdg_surface_listener xdg_surface_listener = {
	handle_xdg_surface_configure,
};

static void
handle_xdg_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial)
{
	struct xdg_client *xdg_client = data;

	xdg_wm_base_pong(wm_base, serial);
	wl_display_flush(xdg_client->client->wl_display);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
	handle_xdg_ping,
};

static struct xdg_surface_data *
create_xdg_surface(struct xdg_client *xdg_client)
{
	struct xdg_surface_data *xdg_surface = xzalloc(sizeof(*xdg_surface));

	TEST_ASSERT_PTR_SET(xdg_surface);
	xdg_surface->surface = create_test_surface(xdg_client->client);
	TEST_ASSERT_PTR_SET(xdg_surface->surface);

	xdg_surface->xdg_surface =
		xdg_wm_base_get_xdg_surface(xdg_client->xdg_wm_base,
					    xdg_surface->surface->wl_surface);
	xdg_surface_add_listener(xdg_surface->xdg_surface,
				 &xdg_surface_listener, xdg_surface);

	return xdg_surface;
}

static void
destroy_xdg_surface(struct xdg_surface_data *xdg_surface)
{
	if (xdg_surface->xdg_toplevel)
		xdg_toplevel_destroy(xdg_surface->xdg_toplevel);
	xdg_surface_destroy(xdg_surface->xdg_surface);
	surface_destroy(xdg_surface->surface);
	free(xdg_surface);
}

static void
xdg_surface_make_toplevel(struct xdg_surface_data *xdg_surface,
			  const char *app_id, const char *title)
{
	xdg_surface->xdg_toplevel =
		xdg_surface_get_toplevel(xdg_surface->xdg_surface);
	TEST_ASSERT_PTR_SET(xdg_surface->xdg_toplevel);
	xdg_toplevel_add_listener(xdg_surface->xdg_toplevel,
				  &xdg_toplevel_listener, xdg_surface);
	xdg_toplevel_set_app_id(xdg_surface->xdg_toplevel, app_id);
	xdg_toplevel_set_title(xdg_surface->xdg_toplevel, title);
}

static void
xdg_surface_wait_configure(struct xdg_surface_data *xdg_surface)
{
	wl_surface_commit(xdg_surface->surface->wl_surface);
	wl_display_roundtrip(xdg_surface->surface->client->wl_display);
	TEST_ASSERT_U32_GT(xdg_surface->configure.serial, 0);
}

static void
xdg_surface_commit_solid(struct xdg_surface_data *xdg_surface,
			 uint8_t r, uint8_t g, uint8_t b)
{
	pixman_color_t color;
	struct buffer *buf;
	int width = xdg_surface->configure.width;
	int height = xdg_surface->configure.height;

	buf = create_shm_buffer_a8r8g8b8(xdg_surface->surface->client,
					 width, height);
	TEST_ASSERT_PTR_SET(buf);
	xdg_surface->surface->buffer = buf;

	color_rgb888(&color, r, g, b);
	fill_image_with_color(buf->image, &color);

	wl_surface_attach(xdg_surface->surface->wl_surface, buf->proxy, 0, 0);
	wl_surface_damage_buffer(xdg_surface->surface->wl_surface,
				 0, 0, width, height);

	if (xdg_surface->configure.serial > 0) {
		xdg_surface_ack_configure(xdg_surface->xdg_surface,
					  xdg_surface->configure.serial);
		xdg_surface->configure.serial = 0;
	}

	xdg_surface->surface->width = width;
	xdg_surface->surface->height = height;

	wl_surface_commit(xdg_surface->surface->wl_surface);
}

static struct xdg_client *
create_xdg_client(void)
{
	struct xdg_client *xdg_client = xzalloc(sizeof(*xdg_client));

	TEST_ASSERT_PTR_SET(xdg_client);
	xdg_client->client = create_client();
	TEST_ASSERT_PTR_SET(xdg_client->client);

	xdg_client->xdg_wm_base = bind_to_singleton_global(xdg_client->client,
							   &xdg_wm_base_interface,
							   5);
	TEST_ASSERT_PTR_SET(xdg_client->xdg_wm_base);
	xdg_wm_base_add_listener(xdg_client->xdg_wm_base, &xdg_wm_base_listener,
				 xdg_client);

	return xdg_client;
}

static void
xdg_client_destroy(struct xdg_client *xdg_client)
{
	xdg_wm_base_destroy(xdg_client->xdg_wm_base);
	client_destroy(xdg_client->client);
	free(xdg_client);
}

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

static void assert_resource_is_proxy(struct wet_testsuite_data *suite_data,
				     struct wl_resource *r, void *p)
{
	TEST_ASSERT_PTR_SET(r);
	TEST_ASSERT_PTR_EQ(wl_resource_get_client(r), suite_data->wl_client);
	TEST_ASSERT_U32_EQ(wl_resource_get_id(r),
			   wl_proxy_get_id((struct wl_proxy *) p));
}

static void assert_surface_matches(struct wet_testsuite_data *suite_data,
				   struct weston_surface *s, struct surface *c)
{
	TEST_ASSERT_PTR_SET(s);
	TEST_ASSERT_PTR_SET(c);

	assert_resource_is_proxy(suite_data, s->resource, c->wl_surface);
	TEST_ASSERT_S32_EQ(s->width, c->width);
	TEST_ASSERT_S32_EQ(s->height, c->height);

	TEST_ASSERT_PTR_SET(s->buffer_ref.buffer);
	TEST_ASSERT_PTR_SET(c->buffer);
	assert_resource_is_proxy(suite_data, s->buffer_ref.buffer->resource,
				 c->buffer->proxy);
}

static void assert_output_matches(struct wet_testsuite_data *suite_data,
				  struct weston_output *s, struct output *c)
{
	struct weston_head *head;
	bool found_client_resource = false;

	TEST_ASSERT_PTR_SET(s);
	TEST_ASSERT_PTR_SET(c);

	wl_list_for_each(head, &s->head_list, output_link) {
		struct wl_resource *res;
		wl_resource_for_each(res, &head->resource_list) {
			if (wl_resource_get_client(res) == suite_data->wl_client &&
			    wl_resource_get_id(res) ==
			     wl_proxy_get_id((struct wl_proxy *) c->wl_output)) {
				found_client_resource = true;
				break;
			}
		}
	}
	TEST_ASSERT_TRUE(found_client_resource);

	TEST_ASSERT_S32_EQ(s->width, c->width);
	TEST_ASSERT_S32_EQ(s->height, c->height);
}

static void *
get_server_res_from_proxy(struct wet_testsuite_data *suite_data,
			  void *p)
{
	uint32_t id = wl_proxy_get_id((struct wl_proxy *) p);
	struct wl_resource *res;

	TEST_ASSERT_PTR_SET(p);
	TEST_ASSERT_U32_GT(id, 0);
	res = wl_client_get_object(suite_data->wl_client, id);
	TEST_ASSERT_PTR_SET(res);
	return wl_resource_get_user_data(res);
}

static void
assert_surface_is_background(struct wet_testsuite_data *suite_data,
			     struct weston_surface *surface)
{
	char lbl[128];

	TEST_ASSERT_PTR_NOT_SET(surface->resource);
	TEST_ASSERT_PTR_SET(surface->buffer_ref.buffer);
	TEST_ASSERT_ENUM_EQ(surface->buffer_ref.buffer->type,
			    WESTON_BUFFER_SOLID);
	TEST_ASSERT_PTR_SET(surface->output);
	TEST_ASSERT_S32_EQ(surface->width, surface->output->width);
	TEST_ASSERT_S32_EQ(surface->height, surface->output->height);
	TEST_ASSERT_PTR_SET(surface->get_label);
	TEST_ASSERT_INT_NE(surface->get_label(surface, lbl, sizeof(lbl)), 0);
	TEST_ASSERT_STR_EQ(lbl, "kiosk shell background surface");
}

TEST(two_surface_switching)
{
	struct wet_testsuite_data *suite_data = TEST_GET_SUITE_DATA();
	struct xdg_client *xdg_client = create_xdg_client();
	struct xdg_surface_data *xdg_surface1, *xdg_surface2;
	struct input *input;

	TEST_ASSERT_PTR_SET(xdg_client);

	/* move the pointer clearly away from our screenshooting area */
	weston_test_move_pointer(xdg_client->client->test->weston_test,
				 0, 1, 0, 2, 30);

	xdg_surface1 = create_xdg_surface(xdg_client);
	TEST_ASSERT_PTR_SET(xdg_surface1);
	xdg_surface_make_toplevel(xdg_surface1, "weston.test.kiosk", "one");
	xdg_surface_wait_configure(xdg_surface1);
	TEST_ASSERT_TRUE(xdg_surface1->configure.fullscreen);
	TEST_ASSERT_INT_EQ(xdg_surface1->configure.width,
			   xdg_client->client->output->width);
	TEST_ASSERT_INT_EQ(xdg_surface1->configure.height,
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

		TEST_ASSERT_ENUM_EQ(breakpoint->template_->breakpoint,
				    WESTON_TEST_BREAKPOINT_POST_REPAINT);
		assert_output_matches(suite_data, output,
				      xdg_client->client->output);
		TEST_ASSERT_PTR_SET(pnode);
		TEST_ASSERT_PTR_SET(surface);
		TEST_ASSERT_PTR_SET(wds);
		TEST_ASSERT_PTR_SET(view);
		TEST_ASSERT_PTR_SET(buffer);

		/* check that our surface is top of the paint node list */
		assert_surface_matches(suite_data, surface, xdg_surface1->surface);
		TEST_ASSERT_STR_EQ(weston_desktop_surface_get_title(wds), "one");
		TEST_ASSERT_TRUE(weston_view_is_mapped(view));
		TEST_ASSERT_TRUE(weston_surface_is_mapped(surface));

		/* the background should be under that */
		pnode = next_pnode_from_z(output, pnode);
		TEST_ASSERT_PTR_SET(pnode);
		assert_surface_is_background(suite_data, pnode->view->surface);
	}

	wl_display_roundtrip(xdg_client->client->wl_display);
	input = container_of(xdg_client->client->inputs.next, struct input, link);
	TEST_ASSERT_PTR_SET(input);
	TEST_ASSERT_PTR_SET(input->keyboard);
	TEST_ASSERT_PTR_EQ(input->keyboard->focus, xdg_surface1->surface);

	xdg_surface2 = create_xdg_surface(xdg_client);
	TEST_ASSERT_PTR_SET(xdg_surface2);
	xdg_surface_make_toplevel(xdg_surface2, "weston.test.kiosk", "two");
	xdg_surface_wait_configure(xdg_surface2);
	TEST_ASSERT_TRUE(xdg_surface2->configure.fullscreen);
	TEST_ASSERT_INT_EQ(xdg_surface2->configure.width,
			   xdg_client->client->output->width);
	TEST_ASSERT_INT_EQ(xdg_surface2->configure.height,
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

		TEST_ASSERT_ENUM_EQ(breakpoint->template_->breakpoint,
				    WESTON_TEST_BREAKPOINT_POST_REPAINT);
		assert_output_matches(suite_data, output,
				      xdg_client->client->output);
		TEST_ASSERT_PTR_SET(pnode);
		TEST_ASSERT_PTR_SET(surface);
		TEST_ASSERT_PTR_SET(wds);
		TEST_ASSERT_PTR_SET(view);
		TEST_ASSERT_PTR_SET(buffer);

		/* check that our surface is top of the paint node list */
		assert_surface_matches(suite_data, surface, xdg_surface2->surface);
		TEST_ASSERT_STR_EQ(weston_desktop_surface_get_title(wds), "two");
		TEST_ASSERT_TRUE(weston_surface_is_mapped(surface));
		TEST_ASSERT_TRUE(weston_view_is_mapped(view));

		/* the background should be under that */
		pnode = next_pnode_from_z(output, pnode);
		TEST_ASSERT_PTR_SET(pnode);
		assert_surface_is_background(suite_data, pnode->view->surface);
	}

	wl_display_roundtrip(xdg_client->client->wl_display);
	TEST_ASSERT_PTR_EQ(input->keyboard->focus, xdg_surface2->surface);
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

		TEST_ASSERT_ENUM_EQ(breakpoint->template_->breakpoint,
				    WESTON_TEST_BREAKPOINT_POST_REPAINT);
		assert_output_matches(suite_data, output,
				      xdg_client->client->output);
		TEST_ASSERT_PTR_SET(pnode);
		TEST_ASSERT_PTR_SET(surface);
		TEST_ASSERT_PTR_SET(wds);
		TEST_ASSERT_PTR_SET(view);
		TEST_ASSERT_PTR_SET(buffer);

		/* check that our surface is top of the paint node list */
		assert_surface_matches(suite_data, surface, xdg_surface1->surface);
		TEST_ASSERT_PTR_SET(surface->resource);
		TEST_ASSERT_TRUE(weston_view_is_mapped(view));
		TEST_ASSERT_TRUE(weston_surface_is_mapped(surface));
		TEST_ASSERT_STR_EQ(weston_desktop_surface_get_title(wds), "one");
	}

	wl_display_roundtrip(xdg_client->client->wl_display);
	TEST_ASSERT_PTR_EQ(input->keyboard->focus, xdg_surface1->surface);

	destroy_xdg_surface(xdg_surface1);
	xdg_client_destroy(xdg_client);
}


TEST(top_surface_present_in_output_repaint)
{
	struct wet_testsuite_data *suite_data = TEST_GET_SUITE_DATA();
	struct xdg_client *xdg_client = create_xdg_client();
	struct xdg_surface_data *xdg_surface = create_xdg_surface(xdg_client);

	TEST_ASSERT_PTR_SET(xdg_client);
	TEST_ASSERT_PTR_SET(xdg_surface);

	/* move the pointer clearly away from our screenshooting area */
	weston_test_move_pointer(xdg_client->client->test->weston_test,
				 0, 1, 0, 2, 30);

	xdg_surface_make_toplevel(xdg_surface, "weston.test.kiosk", "one");
	xdg_surface_wait_configure(xdg_surface);
	TEST_ASSERT_TRUE(xdg_surface->configure.fullscreen);
	TEST_ASSERT_INT_EQ(xdg_surface->configure.width,
			   xdg_client->client->output->width);
	TEST_ASSERT_INT_EQ(xdg_surface->configure.height,
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

		TEST_ASSERT_ENUM_EQ(breakpoint->template_->breakpoint,
				    WESTON_TEST_BREAKPOINT_POST_REPAINT);
		assert_output_matches(suite_data, output, xdg_client->client->output);
		TEST_ASSERT_PTR_SET(pnode);
		TEST_ASSERT_PTR_SET(surface);
		TEST_ASSERT_PTR_SET(view);
		TEST_ASSERT_PTR_SET(buffer);

		/* check that our surface is top of the paint node list */
		assert_surface_matches(suite_data, surface, xdg_surface->surface);
		TEST_ASSERT_TRUE(weston_view_is_mapped(view));
		TEST_ASSERT_TRUE(weston_surface_is_mapped(surface));
	}

	destroy_xdg_surface(xdg_surface);
	xdg_client_destroy(xdg_client);
}

TEST(test_surface_unmaps_on_null)
{
	struct wet_testsuite_data *suite_data = TEST_GET_SUITE_DATA();
	struct xdg_client *xdg_client = create_xdg_client();
	struct xdg_surface_data *xdg_surface = create_xdg_surface(xdg_client);;

	TEST_ASSERT_PTR_SET(xdg_client);
	TEST_ASSERT_PTR_SET(xdg_surface);

	/* move the pointer clearly away from our screenshooting area */
	weston_test_move_pointer(xdg_client->client->test->weston_test,
				 0, 1, 0, 2, 30);

	xdg_surface_make_toplevel(xdg_surface, "weston.test.kiosk", "one");
	xdg_surface_wait_configure(xdg_surface);
	TEST_ASSERT_TRUE(xdg_surface->configure.fullscreen);
	TEST_ASSERT_INT_EQ(xdg_surface->configure.width,
			   xdg_client->client->output->width);
	TEST_ASSERT_INT_EQ(xdg_surface->configure.height,
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
		TEST_ASSERT_ENUM_EQ(breakpoint->template_->breakpoint,
				    WESTON_TEST_BREAKPOINT_POST_REPAINT);
		TEST_ASSERT_PTR_SET(pnode);
		TEST_ASSERT_PTR_SET(surface);
		TEST_ASSERT_PTR_SET(view);
		assert_surface_matches(suite_data, surface, xdg_surface->surface);
		assert_output_matches(suite_data, surface->output,
				      xdg_client->client->output);
		TEST_ASSERT_TRUE(weston_view_is_mapped(view));
		TEST_ASSERT_TRUE(weston_surface_is_mapped(surface));
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

		TEST_ASSERT_ENUM_EQ(breakpoint->template_->breakpoint,
				    WESTON_TEST_BREAKPOINT_POST_REPAINT);

		/* Check that the background is being shown on top. */
		TEST_ASSERT_PTR_SET(pnode);
		TEST_ASSERT_PTR_SET(surface);
		TEST_ASSERT_PTR_SET(view);
		TEST_ASSERT_PTR_SET(buffer);
		assert_surface_is_background(suite_data, surface);

		/* Check that kiosk-shell's view of our surface has been
		 * unmapped, and that there aren't any more views. */
		surface = get_server_res_from_proxy(suite_data,
						    xdg_surface->surface->wl_surface);
		TEST_ASSERT_FALSE(weston_surface_is_mapped(surface));
		TEST_ASSERT_PTR_NOT_SET(surface->buffer_ref.buffer);
		TEST_ASSERT_PTR_NOT_SET(surface->output);
		view = next_view_from_surface(surface, NULL);
		TEST_ASSERT_FALSE(weston_view_is_mapped(view));
		TEST_ASSERT_PTR_NOT_SET(next_view_from_surface(surface, view));
	}

	destroy_xdg_surface(xdg_surface);
	xdg_client_destroy(xdg_client);
}
