# hypr-edgehover

`hypr-edgehover` is a Hyprland plugin for `gaps_out` setups. When the pointer is in an outer monitor gap next to a window, the plugin keeps the real cursor position unchanged but delivers pointer enter/motion to the adjacent window as if the pointer were clamped just inside that window edge. Button and axis events then continue to go to the same pointer focus through Hyprland's normal seat path.

The plugin uses Hyprland's cancellable `input.mouse.move` event bus listener. It does not install function hooks and it does not call `refocus(overridePos)`.

## Configuration

All options live under `plugin:hypr_edgehover:`.

```ini
plugin {
    hypr_edgehover {
        enabled = 1
        edges = lrtb
        inset = 1
        max_distance = 0
        keyboard_focus = -1
    }
}
```

- `enabled` (`int`, default `1`): enable the motion interception.
- `edges` (`str`, default `"lrtb"`): enabled monitor edges. Use any combination of `l`, `r`, `t`, `b`.
- `inset` (`int`, default `1`): virtual pointer position in pixels inside the target window.
- `max_distance` (`int`, default `0`): maximum distance from the monitor edge that may trigger forwarding. `0` means the full outer gap band.
- `keyboard_focus` (`int`, default `-1`): `-1` follows `input:follow_mouse == 1`, `0` never changes keyboard focus, `1` always follows the edge-hover target.

Keyboard focus changes respect the target window's `no_follow_mouse` rule and only run when the focused window changes.

## Build

```sh
cmake -DCMAKE_BUILD_TYPE=Release -B build-cmake
cmake --build build-cmake -j$(nproc)
ctest --test-dir build-cmake --output-on-failure
```

## hyprpm

```sh
hyprpm add <repo-url>
hyprpm enable hypr-edgehover
hyprpm update
```

The configured hyprpm output is `build-cmake/libhypr-edgehover.so`, matching the CMake target output.
