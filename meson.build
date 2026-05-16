project(
    'wayland-clipboard-hist',
    ['cpp', 'c'],
    meson_version: '>=0.62.0',
    default_options: [
        'cpp_std=c++23',
        'warning_level=3',
        'werror=false',
        'optimization=3',
    ],
)

subproj_names = ['watcher', 'copier']
all_tgts = []
nlohmann_json = dependency('nlohmann_json', required: true)
clipboard_common_dep = dependency('clipboard_common', fallback: ['common', 'clipboard_common_dep'])

foreach n : subproj_names
    sp = subproject(n)
    default_tgt = sp.get_variable('default_target', [])
    all_tgts += default_tgt
endforeach

alias_target('everything', all_tgts)

history_io_test = executable(
    'history-io-test',
    [
        'tests/history_io_test.cpp',
    ],
    dependencies: [nlohmann_json, clipboard_common_dep],
)

test('history and io helpers', history_io_test)
