/*
 * Vendored declarations of libweston symbols that are exported
 * (WL_EXPORT) from libweston-14 but only declared in Weston's private
 * headers (libweston/backend.h, libweston/libweston-internal.h).
 *
 * ABI CONTRACT / VERSION PIN
 * --------------------------
 * These functions are NOT part of the stable public libweston API.
 * They are the same entry points backends and the in-tree weston-test
 * plugin use, and their symbols are exported from libweston.so, so an
 * out-of-tree module can call them - but there is NO cross-major-version
 * guarantee. The prototypes below were copied from Weston 14.0.x
 * (libweston-14). When moving to a new libweston major, re-verify each
 * declaration against that version's libweston/backend.h and
 * libweston/libweston-internal.h before bumping the pkg-config
 * dependency.
 *
 * Everything else this module uses comes from public installed headers.
 */

#ifndef WESTON_AUTOMATION_PRIVATE_14_H
#define WESTON_AUTOMATION_PRIVATE_14_H

#include <libweston/libweston.h>
#include <libweston/version.h>

#if WESTON_VERSION_MAJOR != 14
#error "weston-private-14.h is pinned to libweston-14; re-verify prototypes"
#endif

/* from libweston/backend.h (Weston 14.0.x) */

void
notify_axis(struct weston_seat *seat, const struct timespec *time,
	    struct weston_pointer_axis_event *event);

void
notify_axis_source(struct weston_seat *seat, uint32_t source);

void
notify_button(struct weston_seat *seat, const struct timespec *time,
	      int32_t button, enum wl_pointer_button_state state);

void
notify_key(struct weston_seat *seat, const struct timespec *time, uint32_t key,
	   enum wl_keyboard_key_state state,
	   enum weston_key_state_update update_state);

void
notify_keyboard_focus_in(struct weston_seat *seat, struct wl_array *keys,
			 enum weston_key_state_update update_state);

void
notify_keyboard_focus_out(struct weston_seat *seat);

void
notify_motion(struct weston_seat *seat, const struct timespec *time,
	      struct weston_pointer_motion_event *event);

void
notify_motion_absolute(struct weston_seat *seat, const struct timespec *time,
		       struct weston_coord_global pos);

void
notify_pointer_frame(struct weston_seat *seat);

/* from libweston/libweston-internal.h (Weston 14.0.x) */

void
weston_seat_init(struct weston_seat *seat, struct weston_compositor *ec,
		 const char *seat_name);

void
weston_seat_release(struct weston_seat *seat);

int
weston_seat_init_pointer(struct weston_seat *seat);

int
weston_seat_init_keyboard(struct weston_seat *seat, struct xkb_keymap *keymap);

void
weston_seat_release_keyboard(struct weston_seat *seat);

void
weston_seat_release_pointer(struct weston_seat *seat);

#endif /* WESTON_AUTOMATION_PRIVATE_14_H */
