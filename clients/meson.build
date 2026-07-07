srcs_toytoolkit = [
	'window.c',
	color_management_v1_client_protocol_h,
	color_management_v1_protocol_c,
	xdg_shell_client_protocol_h,
	xdg_shell_protocol_c,
	text_cursor_position_client_protocol_h,
	text_cursor_position_protocol_c,
	relative_pointer_unstable_v1_client_protocol_h,
	relative_pointer_unstable_v1_protocol_c,
	pointer_constraints_unstable_v1_client_protocol_h,
	pointer_constraints_unstable_v1_protocol_c,
	tablet_unstable_v2_client_protocol_h,
	tablet_unstable_v2_protocol_c,
	viewporter_client_protocol_h,
	viewporter_protocol_c,
]

deps_toytoolkit = [
	dep_wayland_client,
	dep_lib_cairo_shared,
	dep_xkbcommon,
	dependency('wayland-cursor'),
]

lib_toytoolkit = static_library(
	'toytoolkit',
	srcs_toytoolkit,
	include_directories: common_inc,
	dependencies: deps_toytoolkit,
	install: false,
)

dep_toytoolkit = declare_dependency(
	link_with: lib_toytoolkit,
	dependencies: deps_toytoolkit,
)

exe_shell_desktop = executable(
	'weston-desktop-shell',
	'desktop-shell.c',
	weston_desktop_shell_client_protocol_h,
	weston_desktop_shell_protocol_c,
	tablet_unstable_v2_client_protocol_h,
	tablet_unstable_v2_protocol_c,
	include_directories: common_inc,
	dependencies: dep_toytoolkit,
	install_dir: get_option('libexecdir'),
	install: true,
)

exe_keyboard = executable(
	'weston-keyboard',
	'keyboard.c',
	text_input_unstable_v1_client_protocol_h,
	text_input_unstable_v1_protocol_c,
	input_method_unstable_v1_client_protocol_h,
	input_method_unstable_v1_protocol_c,
	include_directories: common_inc,
	dependencies: dep_toytoolkit,
	install_dir: get_option('libexecdir'),
	install: true,
)
