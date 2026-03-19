plugin_snap_shell = shared_library(
	'snap-shell',
	'snap-shell.c',
	include_directories: common_inc,
	dependencies: [ dep_libweston_public ],
	name_prefix: '',
	install: true,
	install_dir: dir_module_weston
)
env_modmap += 'snap-shell.so=@0@;'.format(plugin_snap_shell.full_path())
