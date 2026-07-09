/*
 * weston-automation: compositor-side module implementing the
 * weston-automation-v1 protocol for UI test automation.
 *
 * Loaded into weston via weston.ini:
 *     [core]
 *     modules=automation.so
 * or, during development:
 *     WESTON_MODULE_MAP=automation.so=/abs/path/automation.so
 *
 * Only clients running as the compositor's uid may bind the global.
 * This module must only be enabled on test sessions.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <wayland-server.h>

#include <libweston/libweston.h>
#include <libweston/desktop.h>

#include "weston-private-14.h"
#include "weston-automation-server-protocol.h"

#define SCAN_INTERVAL_MS 50

struct automation;

struct automation_toplevel {
	struct wl_list link;		/* automation::toplevel_list */
	struct automation *automation;
	struct weston_desktop_surface *desktop_surface;
	struct weston_surface *surface;
	struct wl_listener surface_destroy_listener;
	struct wl_list resource_list;	/* bound client toplevel resources */

	/* cached properties, diffed on every scan */
	char *title;
	char *app_id;
	pid_t pid;
	int32_t x, y, width, height;
	uint32_t state;
	bool have_geometry;
};

struct automation {
	struct weston_compositor *compositor;
	struct wl_listener compositor_destroy_listener;
	struct wl_listener screenshot_authority;
	struct wl_global *global;
	struct wl_event_source *scan_timer;
	struct wl_list toplevel_list;	/* automation_toplevel::link */
	struct wl_list resource_list;	/* bound manager resources */
	struct weston_seat seat;
	bool seat_initialized;
};

static bool
client_is_trusted(struct wl_client *client)
{
	uid_t uid;
	gid_t gid;
	pid_t pid;

	wl_client_get_credentials(client, &pid, &uid, &gid);
	return uid == getuid();
}

/* Screenshots through weston-capture-v1 need an authority to allow
 * them (without one, all captures fail "unauthorized"). Testing wants
 * screenshots, so authorize captures from clients running as the
 * compositor's uid - the same trust policy the automation global uses. */
static void
automation_authorize_screenshot(struct wl_listener *listener,
				struct weston_output_capture_attempt *att)
{
	if (client_is_trusted(att->who->client))
		att->authorized = true;
}

static void
current_time(struct timespec *ts)
{
	clock_gettime(CLOCK_MONOTONIC, ts);
}

/* ------------------------------------------------------------------ */
/* toplevel property collection                                        */

static struct weston_view *
toplevel_get_view(struct automation_toplevel *toplevel)
{
	struct weston_view *view;

	if (wl_list_empty(&toplevel->surface->views))
		return NULL;

	view = wl_container_of(toplevel->surface->views.next, view,
			       surface_link);
	return view;
}

static uint32_t
toplevel_read_state(struct automation_toplevel *toplevel)
{
	struct weston_desktop_surface *ds = toplevel->desktop_surface;
	uint32_t state = 0;

	if (weston_desktop_surface_get_activated(ds))
		state |= WESTON_AUTOMATION_TOPLEVEL_V1_STATE_ACTIVATED;
	if (weston_desktop_surface_get_maximized(ds))
		state |= WESTON_AUTOMATION_TOPLEVEL_V1_STATE_MAXIMIZED;
	if (weston_desktop_surface_get_fullscreen(ds))
		state |= WESTON_AUTOMATION_TOPLEVEL_V1_STATE_FULLSCREEN;

	return state;
}

static bool
toplevel_read_geometry(struct automation_toplevel *toplevel,
		       int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
	struct weston_geometry geom =
		weston_desktop_surface_get_geometry(toplevel->desktop_surface);
	struct weston_view *view = toplevel_get_view(toplevel);
	struct weston_coord_global pos;

	if (!view)
		return false;

	pos = weston_view_get_pos_offset_global(view);
	*x = (int32_t)pos.c.x + geom.x;
	*y = (int32_t)pos.c.y + geom.y;
	*w = geom.width;
	*h = geom.height;

	return true;
}

/* Send the full current property set to one client resource. */
static void
toplevel_send_all(struct automation_toplevel *toplevel,
		  struct wl_resource *resource)
{
	weston_automation_toplevel_v1_send_title(resource,
						 toplevel->title ?: "");
	weston_automation_toplevel_v1_send_app_id(resource,
						  toplevel->app_id ?: "");
	weston_automation_toplevel_v1_send_pid(resource,
					       (uint32_t)toplevel->pid);
	if (toplevel->have_geometry)
		weston_automation_toplevel_v1_send_geometry(resource,
							    toplevel->x,
							    toplevel->y,
							    toplevel->width,
							    toplevel->height);
	weston_automation_toplevel_v1_send_state(resource, toplevel->state);
	weston_automation_toplevel_v1_send_done(resource);
}

static bool
str_changed(const char *cached, const char *fresh)
{
	return strcmp(cached ?: "", fresh ?: "") != 0;
}

/* Re-read properties; broadcast changed ones to every bound resource. */
static void
toplevel_refresh(struct automation_toplevel *toplevel)
{
	struct weston_desktop_surface *ds = toplevel->desktop_surface;
	const char *title = weston_desktop_surface_get_title(ds);
	const char *app_id = weston_desktop_surface_get_app_id(ds);
	pid_t pid = weston_desktop_surface_get_pid(ds);
	uint32_t state = toplevel_read_state(toplevel);
	int32_t x, y, w, h;
	bool have_geom, geom_changed = false;
	bool title_changed, app_id_changed, pid_changed, state_changed;
	struct wl_resource *resource;

	title_changed = str_changed(toplevel->title, title);
	if (title_changed) {
		free(toplevel->title);
		toplevel->title = strdup(title ?: "");
	}

	app_id_changed = str_changed(toplevel->app_id, app_id);
	if (app_id_changed) {
		free(toplevel->app_id);
		toplevel->app_id = strdup(app_id ?: "");
	}

	pid_changed = pid != toplevel->pid;
	toplevel->pid = pid;

	state_changed = state != toplevel->state;
	toplevel->state = state;

	have_geom = toplevel_read_geometry(toplevel, &x, &y, &w, &h);
	if (have_geom &&
	    (!toplevel->have_geometry ||
	     x != toplevel->x || y != toplevel->y ||
	     w != toplevel->width || h != toplevel->height)) {
		geom_changed = true;
		toplevel->x = x;
		toplevel->y = y;
		toplevel->width = w;
		toplevel->height = h;
		toplevel->have_geometry = true;
	}

	if (!title_changed && !app_id_changed && !pid_changed &&
	    !state_changed && !geom_changed)
		return;

	wl_resource_for_each(resource, &toplevel->resource_list) {
		if (title_changed)
			weston_automation_toplevel_v1_send_title(
				resource, toplevel->title ?: "");
		if (app_id_changed)
			weston_automation_toplevel_v1_send_app_id(
				resource, toplevel->app_id ?: "");
		if (pid_changed)
			weston_automation_toplevel_v1_send_pid(
				resource, (uint32_t)toplevel->pid);
		if (geom_changed)
			weston_automation_toplevel_v1_send_geometry(
				resource, toplevel->x, toplevel->y,
				toplevel->width, toplevel->height);
		if (state_changed)
			weston_automation_toplevel_v1_send_state(
				resource, toplevel->state);
		weston_automation_toplevel_v1_send_done(resource);
	}
}

/* ------------------------------------------------------------------ */
/* toplevel object requests                                            */

static void
toplevel_handle_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
toplevel_handle_activate(struct wl_client *client,
			 struct wl_resource *resource)
{
	struct automation_toplevel *toplevel =
		wl_resource_get_user_data(resource);
	struct weston_view *view;

	if (!toplevel)
		return;

	view = toplevel_get_view(toplevel);
	if (!view)
		return;

	weston_view_activate_input(view, &toplevel->automation->seat,
				   WESTON_ACTIVATE_FLAG_CLICKED);
	weston_desktop_surface_set_activated(toplevel->desktop_surface, true);
}

static void
toplevel_handle_close(struct wl_client *client, struct wl_resource *resource)
{
	struct automation_toplevel *toplevel =
		wl_resource_get_user_data(resource);

	if (!toplevel)
		return;

	weston_desktop_surface_close(toplevel->desktop_surface);
}

static void
toplevel_handle_set_maximized(struct wl_client *client,
			      struct wl_resource *resource,
			      uint32_t maximized)
{
	struct automation_toplevel *toplevel =
		wl_resource_get_user_data(resource);

	if (!toplevel)
		return;

	weston_desktop_surface_set_maximized(toplevel->desktop_surface,
					     maximized != 0);
}

static void
toplevel_handle_set_fullscreen(struct wl_client *client,
			       struct wl_resource *resource,
			       uint32_t fullscreen)
{
	struct automation_toplevel *toplevel =
		wl_resource_get_user_data(resource);

	if (!toplevel)
		return;

	weston_desktop_surface_set_fullscreen(toplevel->desktop_surface,
					      fullscreen != 0);
}

static void
toplevel_handle_set_size(struct wl_client *client,
			 struct wl_resource *resource,
			 int32_t width, int32_t height)
{
	struct automation_toplevel *toplevel =
		wl_resource_get_user_data(resource);

	if (!toplevel)
		return;

	weston_desktop_surface_set_size(toplevel->desktop_surface,
					width, height);
}

static void
toplevel_handle_set_position(struct wl_client *client,
			     struct wl_resource *resource,
			     int32_t x, int32_t y)
{
	struct automation_toplevel *toplevel =
		wl_resource_get_user_data(resource);
	struct weston_view *view;
	struct weston_geometry geom;
	struct weston_coord_global pos;

	if (!toplevel)
		return;

	view = toplevel_get_view(toplevel);
	if (!view)
		return;

	/* x/y address the window geometry corner, like the geometry event */
	geom = weston_desktop_surface_get_geometry(toplevel->desktop_surface);
	pos.c = weston_coord(x - geom.x, y - geom.y);
	weston_view_set_position(view, pos);
	weston_surface_damage(toplevel->surface);
}

static const struct weston_automation_toplevel_v1_interface
toplevel_implementation = {
	toplevel_handle_destroy,
	toplevel_handle_activate,
	toplevel_handle_close,
	toplevel_handle_set_maximized,
	toplevel_handle_set_fullscreen,
	toplevel_handle_set_size,
	toplevel_handle_set_position,
};

static void
toplevel_resource_destroyed(struct wl_resource *resource)
{
	/* unlink from automation_toplevel::resource_list (if still linked) */
	wl_list_remove(wl_resource_get_link(resource));
}

static struct wl_resource *
toplevel_create_resource(struct automation_toplevel *toplevel,
			 struct wl_resource *manager_resource)
{
	struct wl_client *client =
		wl_resource_get_client(manager_resource);
	struct wl_resource *resource;

	resource = wl_resource_create(
		client, &weston_automation_toplevel_v1_interface,
		wl_resource_get_version(manager_resource), 0);
	if (!resource) {
		wl_client_post_no_memory(client);
		return NULL;
	}

	wl_resource_set_implementation(resource, &toplevel_implementation,
				       toplevel,
				       toplevel_resource_destroyed);
	wl_list_insert(&toplevel->resource_list,
		       wl_resource_get_link(resource));

	weston_automation_v1_send_toplevel(manager_resource, resource);

	return resource;
}

/* ------------------------------------------------------------------ */
/* toplevel lifecycle                                                  */

static void
toplevel_handle_surface_destroy(struct wl_listener *listener, void *data)
{
	struct automation_toplevel *toplevel =
		wl_container_of(listener, toplevel, surface_destroy_listener);
	struct wl_resource *resource, *tmp;

	wl_resource_for_each_safe(resource, tmp, &toplevel->resource_list) {
		weston_automation_toplevel_v1_send_closed(resource);
		wl_resource_set_user_data(resource, NULL);
		wl_list_remove(wl_resource_get_link(resource));
		wl_list_init(wl_resource_get_link(resource));
	}

	wl_list_remove(&toplevel->surface_destroy_listener.link);
	wl_list_remove(&toplevel->link);
	free(toplevel->title);
	free(toplevel->app_id);
	free(toplevel);
}

static struct automation_toplevel *
automation_find_toplevel(struct automation *automation,
			 struct weston_desktop_surface *ds)
{
	struct automation_toplevel *toplevel;

	wl_list_for_each(toplevel, &automation->toplevel_list, link)
		if (toplevel->desktop_surface == ds)
			return toplevel;

	return NULL;
}

static void
automation_add_toplevel(struct automation *automation,
			struct weston_desktop_surface *ds,
			struct weston_surface *surface)
{
	struct automation_toplevel *toplevel;
	struct wl_resource *manager_resource;
	struct wl_resource *resource;

	toplevel = calloc(1, sizeof *toplevel);
	if (!toplevel)
		return;

	toplevel->automation = automation;
	toplevel->desktop_surface = ds;
	toplevel->surface = surface;
	toplevel->pid = -1;
	wl_list_init(&toplevel->resource_list);

	toplevel->surface_destroy_listener.notify =
		toplevel_handle_surface_destroy;
	wl_signal_add(&surface->destroy_signal,
		      &toplevel->surface_destroy_listener);

	wl_list_insert(automation->toplevel_list.prev, &toplevel->link);

	/* fill the property cache */
	toplevel->title = strdup(weston_desktop_surface_get_title(ds) ?: "");
	toplevel->app_id = strdup(weston_desktop_surface_get_app_id(ds) ?: "");
	toplevel->pid = weston_desktop_surface_get_pid(ds);
	toplevel->state = toplevel_read_state(toplevel);
	toplevel->have_geometry =
		toplevel_read_geometry(toplevel, &toplevel->x, &toplevel->y,
				       &toplevel->width, &toplevel->height);

	/* announce to every bound automation client */
	wl_resource_for_each(manager_resource, &automation->resource_list) {
		resource = toplevel_create_resource(toplevel,
						    manager_resource);
		if (resource)
			toplevel_send_all(toplevel, resource);
	}
}

static bool
surface_is_xdg_toplevel(struct weston_surface *surface)
{
	if (!weston_surface_is_desktop_surface(surface))
		return false;
	if (!surface->role_name)
		return false;

	return strcmp(surface->role_name, "xdg_toplevel") == 0;
}

/* Discover new toplevels and refresh properties of known ones. */
static void
automation_scan(struct automation *automation)
{
	struct weston_view *view;
	struct automation_toplevel *toplevel, *tmp;
	struct weston_desktop_surface *ds;

	wl_list_for_each(view, &automation->compositor->view_list, link) {
		struct weston_surface *surface = view->surface;

		if (!surface_is_xdg_toplevel(surface))
			continue;

		ds = weston_surface_get_desktop_surface(surface);
		if (!automation_find_toplevel(automation, ds))
			automation_add_toplevel(automation, ds, surface);
	}

	wl_list_for_each_safe(toplevel, tmp, &automation->toplevel_list, link)
		toplevel_refresh(toplevel);
}

static int
scan_timer_handler(void *data)
{
	struct automation *automation = data;

	automation_scan(automation);

	if (!wl_list_empty(&automation->resource_list))
		wl_event_source_timer_update(automation->scan_timer,
					     SCAN_INTERVAL_MS);

	return 0;
}

/* ------------------------------------------------------------------ */
/* manager requests: input injection                                   */

static void
automation_handle_destroy(struct wl_client *client,
			  struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
automation_handle_move_pointer(struct wl_client *client,
			       struct wl_resource *resource,
			       int32_t x, int32_t y)
{
	struct automation *automation = wl_resource_get_user_data(resource);
	struct weston_pointer *pointer =
		weston_seat_get_pointer(&automation->seat);
	struct weston_coord_global pos;
	struct timespec time;

	pos.c = weston_coord(x, y);
	current_time(&time);

	notify_motion_absolute(&automation->seat, &time, pos);
	notify_pointer_frame(&automation->seat);

	weston_automation_v1_send_pointer_position(
		resource,
		wl_fixed_from_double(pointer->pos.c.x),
		wl_fixed_from_double(pointer->pos.c.y));
}

static void
automation_handle_send_button(struct wl_client *client,
			      struct wl_resource *resource,
			      int32_t button, uint32_t state)
{
	struct automation *automation = wl_resource_get_user_data(resource);
	struct timespec time;

	current_time(&time);
	notify_button(&automation->seat, &time, button, state);
	notify_pointer_frame(&automation->seat);
}

static void
automation_handle_send_axis(struct wl_client *client,
			    struct wl_resource *resource,
			    uint32_t axis, wl_fixed_t value)
{
	struct automation *automation = wl_resource_get_user_data(resource);
	struct weston_pointer_axis_event axis_event;
	struct timespec time;

	current_time(&time);
	axis_event.axis = axis;
	axis_event.value = wl_fixed_to_double(value);
	axis_event.has_discrete = false;
	axis_event.discrete = 0;

	notify_axis(&automation->seat, &time, &axis_event);
	notify_pointer_frame(&automation->seat);
}

static void
automation_handle_send_key(struct wl_client *client,
			   struct wl_resource *resource,
			   uint32_t key, uint32_t state)
{
	struct automation *automation = wl_resource_get_user_data(resource);
	struct timespec time;

	current_time(&time);
	notify_key(&automation->seat, &time, key, state,
		   STATE_UPDATE_AUTOMATIC);
}

static const struct weston_automation_v1_interface
automation_implementation = {
	automation_handle_destroy,
	automation_handle_move_pointer,
	automation_handle_send_button,
	automation_handle_send_axis,
	automation_handle_send_key,
};

/* ------------------------------------------------------------------ */
/* global bind                                                         */

static void
automation_manager_resource_destroyed(struct wl_resource *resource)
{
	wl_list_remove(wl_resource_get_link(resource));
}

static void
automation_bind(struct wl_client *client, void *data,
		uint32_t version, uint32_t id)
{
	struct automation *automation = data;
	struct wl_resource *resource;
	struct automation_toplevel *toplevel;
	bool was_idle;

	resource = wl_resource_create(client, &weston_automation_v1_interface,
				      version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}

	if (!client_is_trusted(client)) {
		wl_resource_post_error(resource,
				       WL_DISPLAY_ERROR_IMPLEMENTATION,
				       "weston_automation_v1: "
				       "permission denied");
		return;
	}

	was_idle = wl_list_empty(&automation->resource_list);

	/* Pick up anything that appeared while nobody was bound, before
	 * this resource is registered — otherwise the scan itself would
	 * announce new toplevels to it and the loop below would announce
	 * them a second time. */
	automation_scan(automation);

	wl_resource_set_implementation(resource, &automation_implementation,
				       automation,
				       automation_manager_resource_destroyed);
	wl_list_insert(&automation->resource_list,
		       wl_resource_get_link(resource));

	wl_list_for_each(toplevel, &automation->toplevel_list, link) {
		struct wl_resource *toplevel_resource =
			toplevel_create_resource(toplevel, resource);
		if (toplevel_resource)
			toplevel_send_all(toplevel, toplevel_resource);
	}

	if (was_idle)
		wl_event_source_timer_update(automation->scan_timer,
					     SCAN_INTERVAL_MS);
}

/* ------------------------------------------------------------------ */
/* module lifecycle                                                    */

static void
automation_destroy(struct wl_listener *listener, void *data)
{
	struct automation *automation =
		wl_container_of(listener, automation,
				compositor_destroy_listener);
	struct automation_toplevel *toplevel, *tmp;

	wl_list_for_each_safe(toplevel, tmp, &automation->toplevel_list, link)
		toplevel_handle_surface_destroy(
			&toplevel->surface_destroy_listener, NULL);

	if (automation->scan_timer)
		wl_event_source_remove(automation->scan_timer);
	if (automation->global)
		wl_global_destroy(automation->global);
	if (automation->seat_initialized)
		weston_seat_release(&automation->seat);

	wl_list_remove(&automation->screenshot_authority.link);
	wl_list_remove(&automation->compositor_destroy_listener.link);
	free(automation);
}

WL_EXPORT int
wet_module_init(struct weston_compositor *compositor,
		int *argc, char *argv[])
{
	struct automation *automation;
	struct wl_event_loop *loop;

	automation = calloc(1, sizeof *automation);
	if (!automation)
		return -1;

	automation->compositor = compositor;
	wl_list_init(&automation->toplevel_list);
	wl_list_init(&automation->resource_list);

	if (!weston_compositor_add_destroy_listener_once(
		    compositor, &automation->compositor_destroy_listener,
		    automation_destroy)) {
		free(automation);
		return 0;
	}

	weston_seat_init(&automation->seat, compositor, "automation");
	automation->seat_initialized = true;
	weston_seat_init_pointer(&automation->seat);
	if (weston_seat_init_keyboard(&automation->seat, NULL) < 0)
		goto err;

	loop = wl_display_get_event_loop(compositor->wl_display);
	automation->scan_timer =
		wl_event_loop_add_timer(loop, scan_timer_handler, automation);
	if (!automation->scan_timer)
		goto err;

	automation->global =
		wl_global_create(compositor->wl_display,
				 &weston_automation_v1_interface, 1,
				 automation, automation_bind);
	if (!automation->global)
		goto err;

	weston_compositor_add_screenshot_authority(
		compositor, &automation->screenshot_authority,
		automation_authorize_screenshot);

	weston_log("automation: weston-automation-v1 enabled "
		   "(test sessions only)\n");

	return 0;

err:
	wl_list_remove(&automation->compositor_destroy_listener.link);
	if (automation->scan_timer)
		wl_event_source_remove(automation->scan_timer);
	if (automation->seat_initialized)
		weston_seat_release(&automation->seat);
	free(automation);
	return -1;
}
