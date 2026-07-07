# Helper code shared between the weston tree's programs. None of this is
# installed by Weston, so the helper clients carry their own copy. The
# shell plugin needs none of it compiled: its shared/ uses are
# header-only, and the weston_config_* symbols it calls are exported by
# libweston itself.
srcs_shared_client = [
	'config-parser.c',
	'os-compatibility.c',
	'file-util.c',
	'process-util.c',
	'matrix.c',
]

# config-parser.c and matrix.c include libweston and wayland-server
# headers, but call nothing from those libraries.
deps_shared_client = [
	dep_libweston_headers,
	dep_wayland_server_headers,
	dep_libm,
]

lib_shared_client = static_library(
	'shared-client',
	srcs_shared_client,
	include_directories: common_inc,
	dependencies: deps_shared_client,
	install: false,
)

dep_shared_client = declare_dependency(
	link_with: lib_shared_client,
	include_directories: common_inc,
	dependencies: deps_shared_client,
)

srcs_cairo_shared = [
	'image-loader.c',
	'cairo-util.c',
	'frame.c',
]

deps_cairo_shared = [
	dep_shared_client,
	dependency('cairo'),
	dependency('libpng'),
	dep_pixman,
	dep_libm,
]

dep_pango = dependency('pango', required: false)
dep_pangocairo = dependency('pangocairo', required: false)
dep_fontconfig = dependency('fontconfig', required: false)
dep_glib = dependency('glib-2.0', version: '>= 2.36', required: false)

if dep_pango.found() and dep_pangocairo.found() and dep_fontconfig.found() and dep_glib.found()
	deps_cairo_shared += [ dep_pango, dep_pangocairo, dep_fontconfig, dep_glib ]
	config_h.set('HAVE_PANGO', '1')
endif

lib_cairo_shared = static_library(
	'cairo-shared',
	srcs_cairo_shared,
	include_directories: common_inc,
	dependencies: deps_cairo_shared,
	install: false,
)

dep_lib_cairo_shared = declare_dependency(
	link_with: lib_cairo_shared,
	dependencies: deps_cairo_shared,
)
