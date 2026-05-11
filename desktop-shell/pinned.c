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
#include <stdint.h>
#include <string.h>

#include <libweston/libweston.h>
#include <libweston/config-parser.h>

#include "shell.h"
#include "pinned.h"

static bool
str_empty(const char *s)
{
	return s == NULL || s[0] == '\0';
}

void
pinned_config_init(struct pinned_config *pc)
{
	wl_list_init(&pc->rules);
}

void
pinned_config_clear(struct pinned_config *pc)
{
	struct pinned_rule *r, *tmp;

	wl_list_for_each_safe(r, tmp, &pc->rules, link) {
		wl_list_remove(&r->link);
		free(r->app_id);
		free(r->title);
		free(r);
	}
	wl_list_init(&pc->rules);
}

static void
parse_one_rule(struct weston_config_section *section,
	       struct pinned_config *pc)
{
	struct pinned_rule *r;
	char *app_id = NULL, *title = NULL;
	int32_t x = 0, y = 0, w = 0, h = 0;

	weston_config_section_get_string(section, "app-id", &app_id, NULL);
	weston_config_section_get_string(section, "title", &title, NULL);
	weston_config_section_get_int(section, "x", &x, 0);
	weston_config_section_get_int(section, "y", &y, 0);
	weston_config_section_get_int(section, "width", &w, 0);
	weston_config_section_get_int(section, "height", &h, 0);

	if (str_empty(app_id) && str_empty(title)) {
		/* A rule that matches everything is almost certainly a
		 * misconfiguration; skip it. */
		weston_log("pinned-window: rule with no app-id or title; "
			   "skipping\n");
		free(app_id);
		free(title);
		return;
	}

	r = zalloc(sizeof *r);
	if (!r) {
		free(app_id);
		free(title);
		return;
	}

	if (str_empty(app_id)) {
		free(app_id);
		r->app_id = NULL;
	} else {
		r->app_id = app_id;
	}
	if (str_empty(title)) {
		free(title);
		r->title = NULL;
	} else {
		r->title = title;
	}
	r->x = x;
	r->y = y;
	r->width = w;
	r->height = h;
	wl_list_insert(pc->rules.prev, &r->link);
}

bool
pinned_config_load(struct pinned_config *pc, const char *path)
{
	struct weston_config *config;
	struct weston_config_section *section = NULL;
	const char *name = NULL;

	pinned_config_clear(pc);

	if (!path)
		return true;

	config = weston_config_parse(path);
	if (!config) {
		weston_log("pinned-window: failed to parse '%s'\n", path);
		return false;
	}

	while (weston_config_next_section(config, &section, &name) == 1) {
		if (strcmp(name, "pinned-window") == 0)
			parse_one_rule(section, pc);
	}

	weston_config_destroy(config);
	return true;
}

struct pinned_rule *
pinned_config_match(const struct pinned_config *pc,
		    const char *app_id, const char *title)
{
	struct pinned_rule *r;

	wl_list_for_each(r, &pc->rules, link) {
		if (r->app_id) {
			if (!app_id || strcmp(r->app_id, app_id) != 0)
				continue;
		}
		if (r->title) {
			if (!title || strcmp(r->title, title) != 0)
				continue;
		}
		return r;
	}
	return NULL;
}
