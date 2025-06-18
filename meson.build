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

subproj_names = ['watcher']
all_tgts = []

foreach n : subproj_names
    sp = subproject(n)
    default_tgt = sp.get_variable('default_target', [])
    all_tgts += default_tgt
endforeach

alias_target('everything', all_tgts)