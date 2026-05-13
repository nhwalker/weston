/*
 * Copyright © 2026 Weston contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include <libweston/libweston.h>

#include "shell.h"
#include "pinned.h"
#include "weston-pinned-windows-server-protocol.h"

/* Per-resource staging area. Lives until the client destroys the resource
 * (explicit destroy request, disconnect, or compositor shutdown). */
struct pinned_resource {
	struct desktop_shell *shell;
	struct pinned_config staged;
};

static bool
str_empty(const char *s)
{
	return s == NULL || s[0] == '\0';
}

static void
handle_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
handle_add_rule(struct wl_client *client, struct wl_resource *resource,
		const char *app_id, const char *title,
		int32_t x, int32_t y, int32_t width, int32_t height)
{
	struct pinned_resource *pr = wl_resource_get_user_data(resource);
	struct pinned_rule *r;

	if (str_empty(app_id) && str_empty(title)) {
		wl_resource_post_error(resource,
			WESTON_PINNED_WINDOWS_V1_ERROR_INVALID_MATCH,
			"add_rule needs at least one of app_id or title");
		return;
	}
	if (width < 0 || height < 0) {
		wl_resource_post_error(resource,
			WESTON_PINNED_WINDOWS_V1_ERROR_INVALID_SIZE,
			"add_rule width/height must be non-negative");
		return;
	}

	r = zalloc(sizeof *r);
	if (!r) {
		wl_client_post_no_memory(client);
		return;
	}

	if (!str_empty(app_id)) {
		r->app_id = strdup(app_id);
		if (!r->app_id)
			goto err_oom;
	}
	if (!str_empty(title)) {
		r->title = strdup(title);
		if (!r->title)
			goto err_oom;
	}
	r->x = x;
	r->y = y;
	r->width = width;
	r->height = height;
	wl_list_insert(pr->staged.rules.prev, &r->link);
	return;

err_oom:
	free(r->app_id);
	free(r->title);
	free(r);
	wl_client_post_no_memory(client);
}

static void
handle_clear(struct wl_client *client, struct wl_resource *resource)
{
	struct pinned_resource *pr = wl_resource_get_user_data(resource);

	pinned_config_clear(&pr->staged);
}

static void
handle_commit(struct wl_client *client, struct wl_resource *resource)
{
	struct pinned_resource *pr = wl_resource_get_user_data(resource);
	struct desktop_shell *shell = pr->shell;
	uint32_t count;

	/* Atomic replace-all: drop the live set, splice the staged
	 * entries onto the live head, then re-arm the staged head
	 * empty so the same resource can build the next batch. */
	pinned_config_clear(&shell->pinned);
	wl_list_insert_list(&shell->pinned.rules, &pr->staged.rules);
	wl_list_init(&pr->staged.rules);

	pinned_reapply_all(shell);

	count = wl_list_length(&shell->pinned.rules);
	weston_pinned_windows_v1_send_applied(resource, count);
}

static const struct weston_pinned_windows_v1_interface pinned_windows_impl = {
	.destroy	= handle_destroy,
	.add_rule	= handle_add_rule,
	.clear		= handle_clear,
	.commit		= handle_commit,
};

static void
unbind_pinned_windows(struct wl_resource *resource)
{
	struct pinned_resource *pr = wl_resource_get_user_data(resource);

	pinned_config_clear(&pr->staged);
	free(pr);
}

static void
bind_pinned_windows(struct wl_client *client, void *data,
		    uint32_t version, uint32_t id)
{
	struct desktop_shell *shell = data;
	struct pinned_resource *pr;
	struct wl_resource *resource;

	pr = zalloc(sizeof *pr);
	if (!pr) {
		wl_client_post_no_memory(client);
		return;
	}
	pr->shell = shell;
	pinned_config_init(&pr->staged);

	resource = wl_resource_create(client,
				      &weston_pinned_windows_v1_interface,
				      1, id);
	if (!resource) {
		free(pr);
		wl_client_post_no_memory(client);
		return;
	}

	/* No privilege gate: this protocol is open to any client.
	 * See protocol/weston-pinned-windows.xml for the warning. */
	wl_resource_set_implementation(resource, &pinned_windows_impl,
				       pr, unbind_pinned_windows);
}

void
pinned_windows_global_create(struct desktop_shell *shell)
{
	if (wl_global_create(shell->compositor->wl_display,
			     &weston_pinned_windows_v1_interface, 1,
			     shell, bind_pinned_windows) == NULL) {
		weston_log("pinned-windows: failed to create wl_global\n");
	}
}
