project(
    'clipboard-common',
    ['cpp'],
    meson_version: '>=0.62.0',
    default_options: [
        'cpp_std=c++23',
        'warning_level=3',
        'werror=false',
        'optimization=3',
    ],
)

nlohmann_json = dependency('nlohmann_json', required: true)
clipboard_common_inc = include_directories('.')
clipboard_common_lib = static_library(
    'clipboard-common',
    [
        'ClipboardHistory.cpp',
        'PosixIO.cpp',
        'StringUtils.cpp',
    ],
    dependencies: [nlohmann_json],
    include_directories: clipboard_common_inc,
)

clipboard_common_dep = declare_dependency(
    link_with: clipboard_common_lib,
    include_directories: clipboard_common_inc,
    dependencies: [nlohmann_json],
)
