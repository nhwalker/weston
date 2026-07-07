project('weston-desktop-shell',
	'c',
	version: '14.0.2',
	default_options: [
		'warning_level=3',
		'c_std=gnu99',
	],
	meson_version: '>= 0.63.0',
	license: 'MIT/Expat',
)
# Note: unlike the Weston tree, b_lundef is deliberately left off. The
# shell plugin is dlopen()ed by the weston executable and calls frontend
# API (wet_*, text_backend_*, screenshooter_create) whose symbols only
# exist inside the running weston process, so the module must link with
# undefined symbols.

libweston_major = 14

cc = meson.get_compiler('c')

global_args = cc.get_supported_arguments(
	'-Wmissing-prototypes',
	'-Wno-unused-parameter',
	'-Wno-shift-negative-value',
	'-Wno-missing-field-initializers',
	'-Wno-pedantic',
	'-Wundef',
	'-fvisibility=hidden',
)
add_project_arguments(global_args, language: 'c')

config_h = configuration_data()
config_h.set('_GNU_SOURCE', '1')
config_h.set('_ALL_SOURCE', '1')
config_h.set('_FILE_OFFSET_BITS', '64')

# The Weston compositor library and the weston frontend plugin API, both
# provided by an installed Weston/libweston of the matching major version.
dep_libweston = dependency('libweston-@0@'.format(libweston_major))
dep_weston = dependency('weston')

dep_wayland_client = dependency('wayland-client', version: '>= 1.20.0')
dep_wayland_server = dependency('wayland-server', version: '>= 1.20.0')
dep_pixman = dependency('pixman-1')
dep_xkbcommon = dependency('xkbcommon', version: '>= 0.3.0')
if dep_xkbcommon.version().version_compare('>= 0.5.0')
	config_h.set('HAVE_XKBCOMMON_COMPOSE', '1')
endif
dep_libm = cc.find_library('m')

# Compile-only views of libweston/wayland-server for client-side code:
# the helper clients include a few libweston headers (config-parser.h,
# matrix.h, zalloc.h) but must not link the compositor library.
dep_libweston_headers = dep_libweston.partial_dependency(
	compile_args: true, includes: true)
dep_wayland_server_headers = dep_wayland_server.partial_dependency(
	compile_args: true, includes: true)

dir_prefix = get_option('prefix')
dir_bin = dir_prefix / get_option('bindir')
dir_data = dir_prefix / get_option('datadir')
dir_libexec = dir_prefix / get_option('libexecdir')

# Shell plugins must be installed where the weston frontend looks for
# them: $libdir/weston, unless overridden. None of the installed
# pkg-config files exposes that path, so derive it from libdir.
if get_option('moduledir') != ''
	dir_module_weston = get_option('moduledir')
else
	dir_module_weston = dir_prefix / get_option('libdir') / 'weston'
endif

config_h.set_quoted('WESTON_SHELL_CLIENT', get_option('shell-client-default'))
config_h.set_quoted('BINDIR', dir_bin)
config_h.set_quoted('DATADIR', dir_data)

foreach func : [ 'mkostemp', 'strchrnul', 'posix_fallocate', 'memfd_create' ]
	if cc.has_function(func)
		config_h.set('HAVE_' + func.underscorify().to_upper(), '1')
	endif
endforeach

if get_option('resize-pool')
	config_h.set('USE_RESIZE_POOL', '1')
endif

common_inc = include_directories('.')

subdir('protocol')
subdir('shared')
subdir('desktop-shell')
if get_option('clients')
	subdir('clients')
	subdir('data')
endif

configure_file(output: 'config.h', configuration: config_h)
