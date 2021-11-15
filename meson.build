project(
	'wlr-idle-proxy',
	'c',
	version: '1.6',
	license: 'WTFPL',
	meson_version: '>=0.50.0',
	default_options: [
		'c_std=c11',
	],
)

cc = meson.get_compiler('c')

dbus = dependency('dbus-1')
wayland_client = dependency('wayland-client')
wayland_protos = dependency('wayland-protocols', version: '>=1.21')

subdir('protocol')

src_files = [
  'main.c',
]

executable(
	'wlr-idle-proxy',
	files(src_files),
	dependencies: [
		client_protos,
                dbus,
		wayland_client,
	],
	install: true,
)

conf_data = configuration_data()
conf_data.set('bindir', join_paths(get_option('prefix'), get_option('bindir')))
