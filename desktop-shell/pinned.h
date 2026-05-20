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

#ifndef WESTON_DESKTOP_SHELL_PINNED_H
#define WESTON_DESKTOP_SHELL_PINNED_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-util.h>

struct desktop_shell;

/* A single placement rule. Each of the four matcher fields is independent:
 * a NULL field is a wildcard, a non-NULL field must strcmp-equal the
 * corresponding surface attribute. app_id / title come from xdg-shell;
 * x11_wm_class / x11_wm_name come from X11 WM_CLASS / WM_NAME and let
 * rules target Xwayland clients (whose xdg app_id is typically empty). */
struct pinned_rule {
	char *app_id;
	char *title;
	char *x11_wm_class;
	char *x11_wm_name;
	int32_t x, y;
	int32_t width, height;	/* 0 = leave intrinsic */
	struct wl_list link;	/* pinned_config::rules */
};

struct pinned_config {
	struct wl_list rules;	/* struct pinned_rule */
};

/* Initialize an empty rule set. Safe to call before any load attempt. */
void
pinned_config_init(struct pinned_config *pc);

/* Free every rule and re-init the list. */
void
pinned_config_clear(struct pinned_config *pc);

/* Parse `path` (weston-config .ini format) and push rules onto pc.
 * Returns true on success or if path is NULL (treated as no rules).
 * On parse failure the partially-loaded set is cleared. */
bool
pinned_config_load(struct pinned_config *pc, const char *path);

/* First matching rule wins. A rule matches when every non-NULL matcher
 * field in the rule strcmp-equals the corresponding argument. NULL
 * arguments (e.g. an X11 client with no xdg app_id) only match rules
 * whose corresponding field is also NULL/wildcard. Returns NULL if no
 * rule applies. */
struct pinned_rule *
pinned_config_match(const struct pinned_config *pc,
		    const char *app_id, const char *title,
		    const char *x11_wm_class, const char *x11_wm_name);

/* Re-evaluate every shell_surface against the current rule set and
 * apply layer/geometry changes. V1 calls this once at startup; V2
 * calls it after each successful commit via the runtime protocol. */
void
pinned_reapply_all(struct desktop_shell *shell);

/* Register the weston_pinned_windows_v1 global on the shell's
 * wl_display. Bind is open to any client (no privilege gate). */
void
pinned_windows_global_create(struct desktop_shell *shell);

#endif
