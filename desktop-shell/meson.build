srcs_shell_desktop = [
	'shell.c',
	'input-panel.c',
	weston_desktop_shell_server_protocol_h,
	weston_desktop_shell_protocol_c,
	input_method_unstable_v1_server_protocol_h,
	input_method_unstable_v1_protocol_c,
]

# Built as shared_module(), not shared_library(): the plugin is loaded
# with dlopen() by the weston executable and legitimately references
# frontend symbols (wet_*, text_backend_*, screenshooter_create) that
# resolve only inside that process.
plugin_shell_desktop = shared_module(
	'desktop-shell',
	srcs_shell_desktop,
	include_directories: common_inc,
	dependencies: [ dep_libm, dep_libweston, dep_weston ],
	name_prefix: '',
	install: true,
	install_dir: dir_module_weston,
)
