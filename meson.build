project(
  'wl-paste-cpp',
  ['cpp', 'c'],
  meson_version: '>=0.62.0',
  default_options: ['cpp_std=c++23', 'warning_level=3', 'werror=false', 'optimization=3'],
)

# core Wayland
wl_client = dependency('wayland-client')

# wlr protocols (provides wlr-data-control-unstable-v1.xml)
wlr_proto = dependency('wlr-protocols', method: 'pkg-config', required: true)
wlr_pkgdatadir = wlr_proto.get_variable('pkgdatadir')

# Use the unstable meson module for Wayland
wl_mod = import('unstable-wayland')

# Build path to the XML and generate the C bindings
data_control_xml = join_paths(
  wlr_pkgdatadir,
  'unstable',
  'wlr-data-control-unstable-v1.xml',
)
data_control = wl_mod.scan_xml(data_control_xml) # generates the protocol C + headers :contentReference[oaicite:1]{index=1}

nlohmann_json = dependency('nlohmann_json', required: true)

dependencies = [
  wl_client,
  nlohmann_json,
]

executable(
  'wl-paste-cpp',
  [
    'src/main.cpp',
    'src/utils.cpp',
    'src/WaylandClipboard.cpp',
    'src/WaylandConnection.cpp',
  ],
  data_control,
  dependencies: dependencies,
  install: true,
  cpp_args: ['-Wall', '-Wextra', '-pedantic'],
)