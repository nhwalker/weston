if get_option('shell-layout')
	srcs_shell_layout = [
		'layout-shell.c',
	]
	deps_shell_layout = [
		dep_libexec_weston,
		dep_libshared,
		dep_libweston_public,
	]
	plugin_shell_layout = shared_library(
		'layout-shell',
		srcs_shell_layout,
		include_directories: common_inc,
		dependencies: deps_shell_layout,
		name_prefix: '',
		install: true,
		install_dir: dir_module_weston,
		install_rpath: '$ORIGIN'
	)
	env_modmap += 'layout-shell.so=@0@;'.format(plugin_shell_layout.full_path())
endif
