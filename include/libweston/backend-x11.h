/*
 * Copyright © 2016 Benoit Gschwind
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

#ifndef WESTON_COMPOSITOR_X11_H
#define WESTON_COMPOSITOR_X11_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include <libweston/libweston.h>
#include <libweston/plugin-registry.h>

#define WESTON_X11_BACKEND_CONFIG_VERSION 3

struct weston_x11_backend_config {
	struct weston_backend_config base;

	bool fullscreen;
	bool no_input;

	enum weston_renderer_type renderer;
};

/** Geometry of the host X11 monitor backing a head, in root-window pixels. */
struct weston_x11_monitor_info {
	int32_t x;
	int32_t y;
	int32_t width;
	int32_t height;
	int32_t mm_width;
	int32_t mm_height;
};

#define WESTON_X11_OUTPUT_API_NAME "weston_x11_output_api_v1"

struct weston_x11_output_api {
	/** Query the host-monitor geometry associated with an X11 head.
	 *
	 * Returns true and fills \p info if the head was created by the
	 * X11 backend to mirror a host XRandR monitor (i.e. fullscreen
	 * mode picked it up). Returns false otherwise; \p info is left
	 * untouched.
	 */
	bool (*head_get_monitor_info)(struct weston_head *head,
				      struct weston_x11_monitor_info *info);
};

static inline const struct weston_x11_output_api *
weston_x11_output_get_api(struct weston_compositor *compositor)
{
	const void *api;
	api = weston_plugin_api_get(compositor, WESTON_X11_OUTPUT_API_NAME,
				    sizeof(struct weston_x11_output_api));
	return (const struct weston_x11_output_api *)api;
}

#ifdef  __cplusplus
}
#endif

#endif /* WESTON_COMPOSITOR_X11_H_ */
