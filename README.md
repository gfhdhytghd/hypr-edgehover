# hypr-edgehover
## hover on the window, with mouse outside
`hypr-edgehover` is a Hyprland plugin for edge hover forwarding. When the pointer is in an outer monitor gap, on the physical edge over a layer-surface bar, or on a thin overhanging window strip, the plugin keeps the real cursor position unchanged but delivers pointer enter/motion to the adjacent target window as if the pointer were clamped just inside that window edge. Button, axis, and keyboard focus forwarding are configurable per scenario.

The plugin uses Hyprland's cancellable `input.mouse.move` event bus listener. It does not install function hooks and it does not call `refocus(overridePos)`.

## demo


https://github.com/user-attachments/assets/40aa2ccf-4c51-4f4d-948a-f94ff19ae562


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
        gap_pass = hover,click,scroll,keyboard
        layer_pass = hover,keyboard
        layer_namespaces =
        overhang_pass = hover,keyboard
        overhang_threshold = 8
        overhang_edge_width = 0
        steal_edge_width = 2
        zones_top = 0-100
        zones_bottom = 0-100
        zones_left = 0-100
        zones_right = 0-100
    }
}
```

- `enabled` (`int`, default `1`): enable the motion interception.
- `edges` (`str`, default `"lrtb"`): enabled monitor edges. Use any combination of `l`, `r`, `t`, `b`.
- `inset` (`int`, default `1`): virtual pointer position in pixels inside the target window.
- `max_distance` (`int`, default `0`): maximum distance from the monitor edge that may trigger GAP forwarding. `0` means the full outer gap band.
- `keyboard_focus` (`int`, default `-1`): `-1` follows `input:follow_mouse == 1`, `0` never changes keyboard focus, `1` always follows the edge-hover target.
- `gap_pass` (`str`, default `"hover,click,scroll,keyboard"`): forwarded event classes for pure outer-gap forwarding.
- `layer_pass` (`str`, default `"hover,keyboard"`): forwarded event classes when the stock hit is a layer surface such as a bar.
- `layer_namespaces` (`str`, default empty): comma-separated layer-surface namespace allow-list for the LAYER scenario. Empty means all namespaces.
- `overhang_pass` (`str`, default `"hover,keyboard"`): forwarded event classes when the stock hit is a thin overhanging window strip.
- `overhang_threshold` (`int`, default `8`): visible goal thickness in pixels at or below which a window is considered an overhang. Thin windows are skipped as forwarding targets and do not block target selection behind them.
- `overhang_edge_width` (`int`, default `0`): optional physical-edge limit for OVERHANG forwarding. `0` engages over the whole exposed overhang strip; values greater than `0` additionally require the pointer to be within that many pixels of the enabled monitor edge.
- `steal_edge_width` (`int`, default `2`): physical monitor-edge strip width for LAYER forwarding. Layer surfaces only engage when the pointer is within this many pixels of the enabled edge.
- `zones_top`, `zones_bottom`, `zones_left`, `zones_right` (`str`, default `"0-100"`): comma-separated percent ranges that enable forwarding on each edge. Horizontal edges run left to right; vertical edges run top to bottom. Invalid fragments are ignored, and an empty value matches nothing.

Pass-set values are comma-separated subsets of `hover`, `click`, `scroll`, and `keyboard`. If `hover` is absent, that scenario is disabled entirely. `keyboard` also respects `keyboard_focus`; setting `keyboard` in a pass set does not override the `keyboard_focus` policy.

Keyboard focus changes respect the target window's `no_follow_mouse` rule and only run when the focused window changes.

## Known Limitations

Hyprland skips its mouse-move path when the floored cursor coordinates are unchanged. If focus state changes while the cursor remains on the exact same floored edge-gap coordinate, edge-hover delivery may not refresh until the cursor moves to a different floored coordinate.

Bars do not receive hover feedback, tooltips, or hover styling inside active LAYER steal zones, because hover is being forwarded to the target window. Outside the `steal_edge_width` strip, the bar behaves normally.

With `input:off_window_axis_events = 2` or `3`, OVERHANG scroll forwarding can inherit Hyprland's stock off-window axis motion behavior and emit a motion event clamped relative to the thin overhanging window.

Windows whose visible goal edge thickness is less than or equal to `overhang_threshold` are treated as overhang strips and are never selected as edge-hover targets, including in the GAP scenario.

OVERHANG classification and target selection use Hyprland's goal geometry, so scrolling-layout animations do not repeatedly flip between thin-strip and normal-window behavior. Delivery is still clamped against the current surface geometry before pointer motion is sent.

Click forwarding is implemented without cancelling Hyprland's button events, so press/release pairing and mouse keybind handling stay on the stock path.

## Build

```sh
cmake -DCMAKE_BUILD_TYPE=Release -B build-cmake
cmake --build build-cmake -j$(nproc)
ctest --test-dir build-cmake --output-on-failure
```

## hyprpm

```sh
hyprpm add https://github.com/gfhdhytghd/hypr-edgehover
hyprpm enable hypr-edgehover
hyprpm update
```

The configured hyprpm output is `build-cmake/libhypr-edgehover.so`, matching the CMake target output.
