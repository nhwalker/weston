/*
 * Copyright 2024 Weston Contributors
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

#include <linux/input.h>
#include <stdint.h>

#include <libweston/libweston.h>
#include <libweston/desktop.h>

/*
 * snap-shell: a standalone loadable module that binds Alt+Arrow to snap the
 * focused window to the corresponding half of its output.
 *
 * Load via weston.ini:
 *   [core]
 *   modules=snap-shell.so
 *
 * Key bindings:
 *   Alt+Left  -> snap window to left half
 *   Alt+Right -> snap window to right half
 *   Alt+Up    -> snap window to top half
 *   Alt+Down  -> snap window to bottom half
 *
 * Note: snapping uses the full output area. If the shell has panels, the
 * snapped window may overlap them, as this module has no access to the
 * shell's reserved panel regions.
 */

struct snap_module {
	struct weston_compositor *compositor;
	struct wl_listener destroy_listener;
};

/*
 * Return a mapped view for the surface, or the first view as a fallback.
 * Returns NULL if the surface has no views.
 */
static struct weston_view *
get_view(struct weston_surface *surface)
{
	struct weston_view *view;

	if (wl_list_empty(&surface->views))
		return NULL;

	wl_list_for_each(view, &surface->views, surface_link)
		if (weston_view_is_mapped(view))
			return view;

	return container_of(surface->views.next, struct weston_view, surface_link);
}

static void
snap_to_orientation(struct weston_keyboard *keyboard,
		    enum weston_top_level_tiled_orientation orientation)
{
	struct weston_surface *focus;
	struct weston_surface *surface;
	struct weston_desktop_surface *desktop_surface;
	struct weston_view *view;
	struct weston_output *output;
	struct weston_geometry geom;
	struct weston_coord_global pos;
	int x, y, width, height;

	if (!keyboard->focus)
		return;

	focus = keyboard->focus;
	surface = weston_surface_get_main_surface(focus);
	if (!surface)
		return;

	if (!weston_surface_is_desktop_surface(surface))
		return;

	desktop_surface = weston_surface_get_desktop_surface(surface);
	if (!desktop_surface)
		return;

	view = get_view(surface);
	if (!view || !view->output)
		return;

	output = view->output;
	geom = weston_desktop_surface_get_geometry(desktop_surface);

	/* Use full output dimensions (no panel exclusion in standalone module) */
	width  = output->width;
	height = output->height;
	x = (int)output->pos.c.x - geom.x;
	y = (int)output->pos.c.y - geom.y;

	if (orientation & WESTON_TOP_LEVEL_TILED_ORIENTATION_LEFT ||
	    orientation & WESTON_TOP_LEVEL_TILED_ORIENTATION_RIGHT)
		width /= 2;
	else
		height /= 2;

	if (orientation & WESTON_TOP_LEVEL_TILED_ORIENTATION_RIGHT)
		x += width;
	else if (orientation & WESTON_TOP_LEVEL_TILED_ORIENTATION_BOTTOM)
		y += height;

	pos.c = weston_coord(x, y);
	weston_view_set_position(view, pos);
	weston_desktop_surface_set_size(desktop_surface, width, height);
	weston_desktop_surface_set_orientation(desktop_surface, orientation);
}

static void
snap_left(struct weston_keyboard *keyboard,
	  const struct timespec *time,
	  uint32_t key, void *data)
{
	snap_to_orientation(keyboard, WESTON_TOP_LEVEL_TILED_ORIENTATION_LEFT);
}

static void
snap_right(struct weston_keyboard *keyboard,
	   const struct timespec *time,
	   uint32_t key, void *data)
{
	snap_to_orientation(keyboard, WESTON_TOP_LEVEL_TILED_ORIENTATION_RIGHT);
}

static void
snap_up(struct weston_keyboard *keyboard,
	const struct timespec *time,
	uint32_t key, void *data)
{
	snap_to_orientation(keyboard, WESTON_TOP_LEVEL_TILED_ORIENTATION_TOP);
}

static void
snap_down(struct weston_keyboard *keyboard,
	  const struct timespec *time,
	  uint32_t key, void *data)
{
	snap_to_orientation(keyboard, WESTON_TOP_LEVEL_TILED_ORIENTATION_BOTTOM);
}

static void
snap_module_destroy(struct wl_listener *listener, void *data)
{
	struct snap_module *mod =
		wl_container_of(listener, mod, destroy_listener);

	wl_list_remove(&mod->destroy_listener.link);
	free(mod);
}

WL_EXPORT int
weston_module_init(struct weston_compositor *compositor)
{
	struct snap_module *mod;

	mod = calloc(1, sizeof *mod);
	if (!mod)
		return -1;

	mod->compositor = compositor;

	if (!weston_compositor_add_destroy_listener_once(compositor,
							 &mod->destroy_listener,
							 snap_module_destroy)) {
		/* Already loaded */
		free(mod);
		return 0;
	}

	weston_compositor_add_key_binding(compositor, KEY_LEFT,  MODIFIER_ALT,
					  snap_left,  NULL);
	weston_compositor_add_key_binding(compositor, KEY_RIGHT, MODIFIER_ALT,
					  snap_right, NULL);
	weston_compositor_add_key_binding(compositor, KEY_UP,    MODIFIER_ALT,
					  snap_up,    NULL);
	weston_compositor_add_key_binding(compositor, KEY_DOWN,  MODIFIER_ALT,
					  snap_down,  NULL);

	weston_log("snap-shell: loaded, Alt+Arrow snaps focused window\n");
	return 0;
}
