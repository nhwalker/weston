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

/**
 * layout-shell: positions and sizes windows by title using a JSON layout file.
 *
 * Windows whose title matches an entry in the JSON file are placed on the
 * layout layer (BOTTOM_UI) with the specified position and size.  All other
 * windows are placed on the normal layer (NORMAL) and centered on the output,
 * so they appear on top of the managed layout windows.
 *
 * JSON layout file format:
 *   {
 *     "layouts": [
 *       {
 *         "title":  "My Application",
 *         "x":      0,
 *         "y":      0,
 *         "width":  1280,
 *         "height": 720
 *       }
 *     ]
 *   }
 *
 * Title matching is a substring search: a rule whose "title" value appears
 * anywhere in the window's actual title is considered a match.
 *
 * Configuration (pick one):
 *   - Environment variable:  WESTON_LAYOUT_FILE=/path/to/layout.json
 *   - weston.ini [shell] section:  layout-file=/path/to/layout.json
 */

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "frontend/weston.h"
#include "libweston/libweston.h"
#include "shared/helpers.h"
#include <libweston/desktop.h>
#include <libweston/shell-utils.h>

/* -------------------------------------------------------------------------
 * Data structures
 * ---------------------------------------------------------------------- */

/** One entry from the JSON layout file. */
struct layout_rule {
	char    *title;
	int32_t  x, y;
	int32_t  width, height;
	struct wl_list link; /* in layout_shell::rules */
};

/** Per-compositor shell state. */
struct layout_shell {
	struct weston_compositor *compositor;
	struct weston_desktop    *desktop;

	/**
	 * layout_layer (BOTTOM_UI): windows that matched a JSON rule.
	 * Sits below normal_layer so unmanaged windows appear on top.
	 */
	struct weston_layer layout_layer;

	/**
	 * normal_layer (NORMAL): windows that did NOT match any rule.
	 * Centered on the output, rendered above the layout layer.
	 */
	struct weston_layer normal_layer;

	struct wl_list rules; /* list of layout_rule */

	struct wl_listener destroy_listener;
	struct wl_listener output_created_listener;
};

/** Per-surface shell state. */
struct layout_surface {
	struct layout_shell          *shell;
	struct weston_desktop_surface *desktop_surface;
	struct weston_view           *view;
	struct wl_listener            metadata_listener;
	bool                          mapped;
};

/* -------------------------------------------------------------------------
 * Minimal JSON parser
 *
 * Handles the specific subset of JSON used by the layout file:
 *   top-level object → "layouts" array → objects with string/integer values.
 * ---------------------------------------------------------------------- */

static const char *
json_skip_ws(const char *p)
{
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
		p++;
	return p;
}

/**
 * Parse a JSON quoted string at *p (must point at '"').
 * Returns a newly allocated copy of the string value.
 * Advances *p to point after the closing '"'.
 * Returns NULL on error.
 */
static char *
json_parse_string(const char **p)
{
	const char *s = *p;
	char       *result;
	char       *out;
	size_t      len = 0;

	if (*s != '"')
		return NULL;
	s++;

	/* First pass: count output characters. */
	{
		const char *q = s;
		while (*q && *q != '"') {
			if (*q == '\\' && q[1])
				q++;
			q++;
			len++;
		}
		if (!*q)
			return NULL;
	}

	result = malloc(len + 1);
	if (!result)
		return NULL;

	/* Second pass: copy with escape handling. */
	out = result;
	while (*s && *s != '"') {
		if (*s == '\\') {
			s++;
			switch (*s) {
			case 'n':  *out++ = '\n'; break;
			case 't':  *out++ = '\t'; break;
			case 'r':  *out++ = '\r'; break;
			case '"':  *out++ = '"';  break;
			case '\\': *out++ = '\\'; break;
			default:   *out++ = *s;  break;
			}
		} else {
			*out++ = *s;
		}
		if (*s)
			s++;
	}
	*out = '\0';

	if (*s == '"')
		s++;
	*p = s;
	return result;
}

/**
 * Parse a JSON integer (with optional leading '-') at *p.
 * Advances *p past the number.
 */
static int32_t
json_parse_int(const char **p)
{
	const char *s = *p;
	int32_t     sign = 1, val = 0;

	if (*s == '-') {
		sign = -1;
		s++;
	}
	while (*s >= '0' && *s <= '9')
		val = val * 10 + (*s++ - '0');
	*p = s;
	return sign * val;
}

/**
 * Skip one JSON value at *p (string, number, object, or array).
 * Advances *p past the value.
 */
static void
json_skip_value(const char **p)
{
	const char *s = json_skip_ws(*p);

	if (*s == '"') {
		s++;
		while (*s && *s != '"') {
			if (*s == '\\' && s[1])
				s++;
			s++;
		}
		if (*s)
			s++;
	} else if (*s == '{' || *s == '[') {
		char open  = *s;
		char close = (*s == '{') ? '}' : ']';
		int  depth = 1;
		s++;
		while (*s && depth > 0) {
			if (*s == '"') {
				s++;
				while (*s && *s != '"') {
					if (*s == '\\' && s[1])
						s++;
					s++;
				}
				if (*s)
					s++;
			} else if (*s == open) {
				depth++;
				s++;
			} else if (*s == close) {
				depth--;
				s++;
			} else {
				s++;
			}
		}
	} else {
		/* number, true, false, null */
		while (*s && *s != ',' && *s != '}' && *s != ']')
			s++;
	}
	*p = s;
}

/**
 * Within the JSON object starting at *p (which must point after the '{'),
 * find the value for `key` at the top level of that object.
 *
 * Returns a pointer to the start of the value text, or NULL if not found.
 * Does not modify *p.
 */
static const char *
json_obj_find_key(const char *p, const char *key)
{
	while (*p) {
		p = json_skip_ws(p);
		if (*p == '}' || *p == '\0')
			break;
		if (*p != '"') {
			p++;
			continue;
		}

		/* Parse the key string. */
		const char *key_start = p;
		char       *k = json_parse_string(&p);
		if (!k)
			break;

		int match = (strcmp(k, key) == 0);
		free(k);

		p = json_skip_ws(p);
		if (*p != ':') {
			/* Malformed; rewind and skip */
			p = key_start + 1;
			continue;
		}
		p++; /* skip ':' */
		p = json_skip_ws(p);

		if (match)
			return p;

		/* Skip the value and continue to the next key. */
		json_skip_value(&p);
		p = json_skip_ws(p);
		if (*p == ',')
			p++;
	}
	return NULL;
}

/* -------------------------------------------------------------------------
 * Layout rule loading
 * ---------------------------------------------------------------------- */

static char *
read_file(const char *path)
{
	FILE  *f;
	long   size;
	char  *buf;

	f = fopen(path, "r");
	if (!f) {
		weston_log("layout-shell: cannot open '%s': %s\n",
			   path, strerror(errno));
		return NULL;
	}

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	rewind(f);

	if (size <= 0) {
		fclose(f);
		return NULL;
	}

	buf = malloc((size_t)size + 1);
	if (!buf) {
		fclose(f);
		return NULL;
	}

	if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
		free(buf);
		fclose(f);
		return NULL;
	}
	buf[size] = '\0';
	fclose(f);
	return buf;
}

/**
 * Parse the JSON layout file contents and populate shell->rules.
 */
static void
load_layout_rules(struct layout_shell *shell, const char *json)
{
	const char *p = json;
	const char *layouts_val;

	/* Find the top-level "layouts" key. */
	p = json_skip_ws(p);
	if (*p != '{') {
		weston_log("layout-shell: JSON must start with '{'\n");
		return;
	}
	p++; /* skip '{' */

	layouts_val = json_obj_find_key(p, "layouts");
	if (!layouts_val) {
		weston_log("layout-shell: no 'layouts' array found in JSON\n");
		return;
	}

	p = json_skip_ws(layouts_val);
	if (*p != '[') {
		weston_log("layout-shell: 'layouts' value is not an array\n");
		return;
	}
	p++; /* skip '[' */

	/* Iterate over objects in the array. */
	while (*p) {
		p = json_skip_ws(p);
		if (*p == ']')
			break;
		if (*p != '{') {
			p++;
			continue;
		}

		/* Find the extent of this object. */
		const char *obj_start = p;
		{
			int depth = 1;
			p++;
			while (*p && depth > 0) {
				if (*p == '"') {
					p++;
					while (*p && *p != '"') {
						if (*p == '\\' && p[1])
							p++;
						p++;
					}
					if (*p)
						p++;
				} else if (*p == '{') {
					depth++; p++;
				} else if (*p == '}') {
					depth--; p++;
				} else {
					p++;
				}
			}
		}
		const char *obj_end = p;

		/* Copy the object text and parse it. */
		size_t obj_len = (size_t)(obj_end - obj_start);
		char  *obj     = malloc(obj_len + 1);
		if (!obj)
			continue;
		memcpy(obj, obj_start, obj_len);
		obj[obj_len] = '\0';

		/* obj points at '{...}'; move inside. */
		const char *inner = obj + 1; /* skip '{' */

		char    *title  = NULL;
		int32_t  x = 0, y = 0, width = 0, height = 0;

		const char *val;

		val = json_obj_find_key(inner, "title");
		if (val)
			title = json_parse_string(&val);

		val = json_obj_find_key(inner, "x");
		if (val)
			x = json_parse_int(&val);

		val = json_obj_find_key(inner, "y");
		if (val)
			y = json_parse_int(&val);

		val = json_obj_find_key(inner, "width");
		if (val)
			width = json_parse_int(&val);

		val = json_obj_find_key(inner, "height");
		if (val)
			height = json_parse_int(&val);

		free(obj);

		if (title && width > 0 && height > 0) {
			struct layout_rule *rule = zalloc(sizeof *rule);
			if (rule) {
				rule->title  = title;
				rule->x      = x;
				rule->y      = y;
				rule->width  = width;
				rule->height = height;
				wl_list_insert(shell->rules.prev, &rule->link);
				weston_log("layout-shell: rule '%s' → %dx%d+%d+%d\n",
					   title, width, height, x, y);
			} else {
				free(title);
			}
		} else {
			free(title);
		}

		/* Advance past commas between objects. */
		p = json_skip_ws(p);
		if (*p == ',')
			p++;
	}
}

/* -------------------------------------------------------------------------
 * Layout application
 * ---------------------------------------------------------------------- */

/**
 * Find the first rule whose "title" field is a substring of the window title.
 * Returns NULL if no rule matches.
 */
static struct layout_rule *
find_rule(struct layout_shell *shell, const char *window_title)
{
	struct layout_rule *rule;

	if (!window_title)
		return NULL;

	wl_list_for_each(rule, &shell->rules, link) {
		if (strstr(window_title, rule->title))
			return rule;
	}
	return NULL;
}

/**
 * (Re-)apply layout to a surface.  Called from committed() and when the
 * title metadata changes.
 *
 * - If the title matches a rule: move to layout_layer, set size and position.
 * - Otherwise: move to normal_layer, center on the default output.
 */
static void
layout_surface_configure(struct layout_surface *lsurf)
{
	struct weston_desktop_surface *dsurf = lsurf->desktop_surface;
	struct weston_surface         *surf  =
		weston_desktop_surface_get_surface(dsurf);
	struct layout_shell           *shell = lsurf->shell;
	const char                    *title =
		weston_desktop_surface_get_title(dsurf);
	struct layout_rule            *rule  = find_rule(shell, title);

	if (rule) {
		/* Matched: place on layout layer with explicit geometry. */
		struct weston_coord_global pos;

		weston_desktop_surface_set_size(dsurf, rule->width, rule->height);

		pos.c = weston_coord(rule->x, rule->y);
		weston_view_set_position(lsurf->view, pos);

		weston_view_move_to_layer(lsurf->view,
					  &shell->layout_layer.view_list);
	} else {
		/* Unmatched: place on normal layer, centered on output. */
		struct weston_output *output =
			weston_shell_utils_get_default_output(shell->compositor);

		if (output)
			weston_shell_utils_center_on_output(lsurf->view, output);

		weston_view_move_to_layer(lsurf->view,
					  &shell->normal_layer.view_list);
	}

	weston_view_update_transform(lsurf->view);

	if (!lsurf->mapped) {
		weston_surface_map(surf);
		lsurf->mapped = true;
	}
}

/* -------------------------------------------------------------------------
 * Desktop API callbacks
 * ---------------------------------------------------------------------- */

static void
metadata_changed(struct wl_listener *listener, void *data)
{
	struct layout_surface *lsurf =
		container_of(listener, struct layout_surface, metadata_listener);
	struct weston_surface *surf =
		weston_desktop_surface_get_surface(lsurf->desktop_surface);

	/* Only re-configure if the surface has been mapped already. */
	if (weston_surface_is_mapped(surf))
		layout_surface_configure(lsurf);
}

static void
desktop_surface_added(struct weston_desktop_surface *dsurf, void *data)
{
	struct layout_shell   *shell = data;
	struct layout_surface *lsurf;
	struct weston_view    *view;

	view = weston_desktop_surface_create_view(dsurf);
	if (!view)
		return;

	lsurf = zalloc(sizeof *lsurf);
	if (!lsurf) {
		weston_desktop_surface_unlink_view(view);
		weston_view_destroy(view);
		return;
	}

	lsurf->shell          = shell;
	lsurf->desktop_surface = dsurf;
	lsurf->view           = view;
	lsurf->mapped         = false;

	lsurf->metadata_listener.notify = metadata_changed;
	weston_desktop_surface_add_metadata_listener(dsurf,
						     &lsurf->metadata_listener);

	weston_desktop_surface_set_user_data(dsurf, lsurf);
}

static void
desktop_surface_removed(struct weston_desktop_surface *dsurf, void *data)
{
	struct layout_surface *lsurf =
		weston_desktop_surface_get_user_data(dsurf);

	if (!lsurf)
		return;

	wl_list_remove(&lsurf->metadata_listener.link);
	weston_desktop_surface_unlink_view(lsurf->view);
	weston_view_destroy(lsurf->view);
	weston_desktop_surface_set_user_data(dsurf, NULL);
	free(lsurf);
}

static void
desktop_surface_committed(struct weston_desktop_surface *dsurf,
			  struct weston_coord_surface buf_offset,
			  void *data)
{
	struct layout_surface *lsurf =
		weston_desktop_surface_get_user_data(dsurf);
	struct weston_surface *surf =
		weston_desktop_surface_get_surface(dsurf);

	if (!lsurf || surf->width == 0)
		return;

	layout_surface_configure(lsurf);
}

static const struct weston_desktop_api layout_shell_desktop_api = {
	.struct_size     = sizeof(struct weston_desktop_api),
	.surface_added   = desktop_surface_added,
	.surface_removed = desktop_surface_removed,
	.committed       = desktop_surface_committed,
};

/* -------------------------------------------------------------------------
 * Shell lifecycle
 * ---------------------------------------------------------------------- */

static void
output_created(struct wl_listener *listener, void *data)
{
	/* Nothing to do; windows are re-positioned on every commit. */
}

static void
layout_shell_destroy(struct wl_listener *listener, void *data)
{
	struct layout_shell *shell =
		container_of(listener, struct layout_shell, destroy_listener);
	struct layout_rule *rule, *tmp;

	wl_list_remove(&shell->destroy_listener.link);
	wl_list_remove(&shell->output_created_listener.link);

	wl_list_for_each_safe(rule, tmp, &shell->rules, link) {
		wl_list_remove(&rule->link);
		free(rule->title);
		free(rule);
	}

	weston_desktop_destroy(shell->desktop);
	weston_layer_fini(&shell->layout_layer);
	weston_layer_fini(&shell->normal_layer);
	free(shell);
}

WL_EXPORT int
wet_shell_init(struct weston_compositor *ec, int *argc, char *argv[])
{
	struct layout_shell *shell;
	const char          *layout_file = NULL;
	char                *config_layout_file = NULL;
	char                *json        = NULL;

	shell = zalloc(sizeof *shell);
	if (!shell)
		return -1;

	shell->compositor = ec;
	wl_list_init(&shell->rules);

	if (!weston_compositor_add_destroy_listener_once(ec,
							 &shell->destroy_listener,
							 layout_shell_destroy)) {
		free(shell);
		return 0;
	}

	/*
	 * layout_layer (BOTTOM_UI): matched windows with fixed geometry.
	 * normal_layer (NORMAL): unmatched windows, stacked above layout.
	 */
	weston_layer_init(&shell->layout_layer, ec);
	weston_layer_set_position(&shell->layout_layer,
				  WESTON_LAYER_POSITION_BOTTOM_UI);

	weston_layer_init(&shell->normal_layer, ec);
	weston_layer_set_position(&shell->normal_layer,
				  WESTON_LAYER_POSITION_NORMAL);

	/* Locate the layout file. */
	layout_file = getenv("WESTON_LAYOUT_FILE");
	if (!layout_file) {
		struct weston_config *wc = wet_get_config(ec);
		if (wc) {
			struct weston_config_section *section =
				weston_config_get_section(wc, "shell",
							  NULL, NULL);
			if (section)
				weston_config_section_get_string(
					section, "layout-file",
					&config_layout_file, NULL);
		}
		if (config_layout_file)
			layout_file = config_layout_file;
	}

	if (layout_file) {
		json = read_file(layout_file);
		if (json) {
			load_layout_rules(shell, json);
			free(json);
		}
	} else {
		weston_log("layout-shell: no layout file configured; "
			   "set WESTON_LAYOUT_FILE or add layout-file= to "
			   "[shell] in weston.ini\n");
	}
	free(config_layout_file);

	shell->desktop = weston_desktop_create(ec,
					       &layout_shell_desktop_api,
					       shell);
	if (!shell->desktop)
		return -1;

	shell->output_created_listener.notify = output_created;
	wl_signal_add(&ec->output_created_signal,
		      &shell->output_created_listener);

	weston_log("layout-shell: initialized with %d layout rule(s)\n",
		   wl_list_length(&shell->rules));

	return 0;
}
