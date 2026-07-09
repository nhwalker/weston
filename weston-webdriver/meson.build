project('weston-webdriver',
	'c',
	version: '0.1.0',
	default_options: [
		'warning_level=2',
		'c_std=gnu11',
	],
	meson_version: '>= 0.63.0',
	license: 'MIT/Expat',
)

# Only the compositor-side module is built with meson; the WebDriver
# daemon under daemon/ is a Python package installed separately.
subdir('module')
