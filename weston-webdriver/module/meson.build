dep_wayland_server = dependency('wayland-server', version: '>= 1.20')
dep_libweston = dependency('libweston-14')

dep_scanner = dependency('wayland-scanner', native: true)
prog_scanner = find_program(
	dep_scanner.get_variable(pkgconfig: 'wayland_scanner'),
	native: true,
)

automation_xml = files('../protocol/weston-automation.xml')

automation_server_header = custom_target(
	'weston-automation-server-protocol.h',
	input: automation_xml,
	output: 'weston-automation-server-protocol.h',
	command: [ prog_scanner, 'server-header', '@INPUT@', '@OUTPUT@' ],
)

automation_protocol_code = custom_target(
	'weston-automation-protocol.c',
	input: automation_xml,
	output: 'weston-automation-protocol.c',
	command: [ prog_scanner, 'private-code', '@INPUT@', '@OUTPUT@' ],
)

# Weston loads modules from <libdir>/weston (see weston(1) MODULEDIR);
# derive it from the libweston pkg-config file so the module lands next
# to the installed compositor's own plugins.
dir_weston_modules = dep_libweston.get_variable(pkgconfig: 'libdir') / 'weston'

automation_module = shared_library(
	'automation',
	[
		'weston-automation.c',
		automation_server_header,
		automation_protocol_code,
	],
	dependencies: [ dep_wayland_server, dep_libweston ],
	name_prefix: '',
	install: true,
	install_dir: dir_weston_modules,
)
