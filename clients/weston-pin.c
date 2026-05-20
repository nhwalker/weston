/*
 * Copyright © 2026 Weston contributors
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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-client.h>

#include "weston-pinned-windows-client-protocol.h"

struct pin_app {
	struct wl_display *display;
	struct wl_registry *registry;
	struct weston_pinned_windows_v1 *iface;
	bool applied;
	uint32_t applied_count;
};

static void
handle_applied(void *data, struct weston_pinned_windows_v1 *iface,
	       uint32_t count)
{
	struct pin_app *app = data;

	app->applied = true;
	app->applied_count = count;
}

static const struct weston_pinned_windows_v1_listener pin_listener = {
	.applied = handle_applied,
};

static void
registry_handle_global(void *data, struct wl_registry *registry, uint32_t id,
		       const char *interface, uint32_t version)
{
	struct pin_app *app = data;

	if (strcmp(interface, weston_pinned_windows_v1_interface.name) == 0 &&
	    !app->iface) {
		app->iface = wl_registry_bind(registry, id,
					      &weston_pinned_windows_v1_interface,
					      1);
		weston_pinned_windows_v1_add_listener(app->iface,
						      &pin_listener, app);
	}
}

static void
registry_handle_global_remove(void *data, struct wl_registry *registry,
			      uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	registry_handle_global,
	registry_handle_global_remove,
};

static void
usage(FILE *out, int status)
{
	fprintf(out,
"Usage: weston-pin [COMMAND ...]\n"
"\n"
"Drives the weston_pinned_windows_v1 protocol against the running\n"
"compositor. Commands are processed in order:\n"
"\n"
"  clear                                                drop staged rules\n"
"  add APP_ID TITLE X11_CLASS X11_NAME X Y WIDTH HEIGHT  stage one rule\n"
"  commit                                               apply atomically\n"
"\n"
"Empty string for any matcher field is a wildcard (use '').  At least one\n"
"matcher must be non-empty.  X11_CLASS and X11_NAME match Xwayland\n"
"WM_CLASS and WM_NAME and exist because Xwayland clients usually have an\n"
"empty xdg app_id.  WIDTH/HEIGHT of 0 keep the surface's intrinsic size.\n"
"X, Y, WIDTH, HEIGHT are in global compositor coordinates.\n"
"\n"
"Examples:\n"
"  weston-pin add weston-terminal '' '' '' 100 100 400 300 commit\n"
"  weston-pin add '' '' xterm '' 0 0 800 600 commit\n");
	exit(status);
}

static int
parse_int(const char *s, int *out)
{
	char *end;
	long v;

	v = strtol(s, &end, 10);
	if (*s == '\0' || *end != '\0') {
		fprintf(stderr, "weston-pin: not a number: '%s'\n", s);
		return -1;
	}
	*out = (int)v;
	return 0;
}

int
main(int argc, char *argv[])
{
	struct pin_app app = { 0 };
	int i;

	if (argc >= 2 && (strcmp(argv[1], "-h") == 0 ||
			  strcmp(argv[1], "--help") == 0)) {
		usage(stdout, 0);
	}

	app.display = wl_display_connect(NULL);
	if (!app.display) {
		fprintf(stderr, "weston-pin: failed to connect to display\n");
		return 1;
	}

	app.registry = wl_display_get_registry(app.display);
	wl_registry_add_listener(app.registry, &registry_listener, &app);
	wl_display_roundtrip(app.display);

	if (!app.iface) {
		fprintf(stderr,
			"weston-pin: compositor does not advertise "
			"weston_pinned_windows_v1\n");
		wl_display_disconnect(app.display);
		return 1;
	}

	for (i = 1; i < argc; i++) {
		const char *cmd = argv[i];

		if (strcmp(cmd, "clear") == 0) {
			weston_pinned_windows_v1_clear(app.iface);
		} else if (strcmp(cmd, "add") == 0) {
			int x, y, w, h;

			if (i + 8 >= argc) {
				fprintf(stderr,
					"weston-pin: 'add' needs 8 args\n");
				return 2;
			}
			if (parse_int(argv[i + 5], &x) < 0 ||
			    parse_int(argv[i + 6], &y) < 0 ||
			    parse_int(argv[i + 7], &w) < 0 ||
			    parse_int(argv[i + 8], &h) < 0)
				return 2;

			weston_pinned_windows_v1_add_rule(app.iface,
							  argv[i + 1],
							  argv[i + 2],
							  argv[i + 3],
							  argv[i + 4],
							  x, y, w, h);
			i += 8;
		} else if (strcmp(cmd, "commit") == 0) {
			weston_pinned_windows_v1_commit(app.iface);
		} else {
			fprintf(stderr, "weston-pin: unknown command '%s'\n",
				cmd);
			usage(stderr, 2);
		}
	}

	wl_display_roundtrip(app.display);

	if (app.applied)
		fprintf(stderr, "weston-pin: applied %u rule(s)\n",
			app.applied_count);

	weston_pinned_windows_v1_destroy(app.iface);
	wl_display_roundtrip(app.display);
	wl_registry_destroy(app.registry);
	wl_display_disconnect(app.display);
	return 0;
}
