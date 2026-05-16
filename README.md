# wl-paste-cpp

Small Wayland clipboard history tools built on `wlr-data-control-unstable-v1`.

## Binaries

- `wl-copy-slurp` watches the current Wayland selection and stores recent clipboard entries.
- `wl-copy-picker [picker command]` restores an entry from history. With no picker command, it restores the newest entry.

The project is intended for compositors that expose the wlroots data control protocol. The history file is stored at `$XDG_DATA_HOME/clipboard_history.json`, or `$HOME/.local/share/clipboard_history.json` when `XDG_DATA_HOME` is unset.

## Usage

Run the watcher in a long-lived session:

```sh
wl-copy-slurp
```

Restore the newest entry:

```sh
wl-copy-picker
```

Choose from history with a dmenu-compatible picker:

```sh
wl-copy-picker 'wofi --dmenu'
wl-copy-picker 'fuzzel --dmenu'
```

Picker rows are prefixed with their history position so duplicate text entries can be selected unambiguously.

## Development

Build and test with the flake-provided environment:

```sh
nix develop --command meson setup build --wipe
nix develop --command meson compile -C build
nix develop --command meson test -C build --print-errorlogs
```

Or build the package directly:

```sh
nix build .#wl-paste-cpp
```
