# vnm_qml_chrome

[![CI Linux](https://github.com/Varinomics/vnm_qml_chrome/actions/workflows/ci-linux.yml/badge.svg?branch=master)](https://github.com/Varinomics/vnm_qml_chrome/actions/workflows/ci-linux.yml) [![CI macOS](https://github.com/Varinomics/vnm_qml_chrome/actions/workflows/ci-macos.yml/badge.svg?branch=master)](https://github.com/Varinomics/vnm_qml_chrome/actions/workflows/ci-macos.yml) [![CI Windows](https://github.com/Varinomics/vnm_qml_chrome/actions/workflows/ci-windows.yml/badge.svg?branch=master)](https://github.com/Varinomics/vnm_qml_chrome/actions/workflows/ci-windows.yml) [![CI FreeBSD](https://github.com/Varinomics/vnm_qml_chrome/actions/workflows/ci-freebsd.yml/badge.svg?branch=master)](https://github.com/Varinomics/vnm_qml_chrome/actions/workflows/ci-freebsd.yml)

Requires Qt 6.12 or newer.

The module uses `qt_add_qml_module(QML_FILES ... DISCARD_QML_CONTENTS)` in
every configuration. Keep `QT_QML_NO_CACHEGEN=OFF` (the default); configure
rejects disabling generation. Qt compiles supported bindings to native code
and retains compiled bytecode for the others. The embedded compiled units
are the only deployed QML representation, and require the matching Qt
build at runtime. Rebuild the library and its consumers when changing Qt.
Source text is unavailable for fallback or QML debugging that disables compiled
units, including in Debug builds. Strings, metadata and compiled code remain
inspectable; this is not encryption.

Hosts that require no generated QML disk cache should set
`QML_DISK_CACHE=aot` and remove `QML_FORCE_DISK_CACHE` and
`QML_DISABLE_DISK_CACHE` before constructing any Qt application or QML engine.
Preserve the caller's environment separately for child processes. This library
does not change the host process's cache policy. Link the CMake target
`vnm_qml_chrome::vnm_qml_chrome` so Qt's compiled units and resource initializers
are retained.

`VNM_Snapped_rectangle` is available from `VNM_Chrome 1.0` for control
backgrounds that need their painted edges aligned to the device-pixel grid. It
keeps the requested logical geometry and exposes `color`, `border_color`,
`border_width`, `radius`, and the independent corner-radius properties.
`device_pixel_ratio` and `snapping_active` report the active correction state.
The component supports axis-aligned uniform parent transforms and uses its
logical geometry when the transform is unsupported.

Set `title_font_family` on `VNM_ChromeTitleBar` or
`VNM_ChromeFrameShell` to use an application-supplied title font. The caller
owns loading and packaging that font; `vnm_qml_chrome` only applies the family
name to the title label and title editor. Leave the family empty to retain each
control's default font. Titles, the activity marker, and process-ID text follow
the local application or ApplicationWindow font at the title-role proportion
19/18, retaining its point or logical-pixel unit. Application font changes
update the chrome; Qt handles display scaling. Default titlebar, editor, and
process-ID text geometry grows with the measured font, while explicit caller
height overrides remain authoritative. Shared button tooltips use white text
on an unframed dark background independently of the platform Controls style.

A plain left click on the Varinomics mark toggles an always-on-top eye.
Ctrl+left-click reveals the process ID; a second Ctrl+left-click retracts it.
Set `stay_on_top_enabled: false` or
`pid_reveal_enabled: false` on a direct `VNM_AnimatedMark`; the corresponding
`VNM_ChromeTitleBar` / `VNM_ChromeFrameShell` properties are
`mark_stay_on_top_enabled` and `mark_pid_reveal_enabled`.

`VNM_ChromeTitleBar` and `VNM_ChromeFrameShell` apply the topmost window hint
automatically. A direct `VNM_AnimatedMark` emits
`stay_on_top_change_requested(active)` so its owner can apply the request. The
owner must also feed the effective window state back through
`stay_on_top_active`; binding it to the window flags keeps the eye synchronized
with the initial state, accepted requests, and external flag changes:

```qml
import QtQuick
import QtQuick.Window
import VNM_Chrome

Window {
    id: window

    VNM_AnimatedMark {
        stay_on_top_active:
            (window.flags & Qt.WindowStaysOnTopHint) !== 0

        onStay_on_top_change_requested: (active) =>
            VNM_system_window.set_window_stays_on_top(window, active)
    }
}
```

The eye is the bundled `vnm_mark_eye.svg` (Font Awesome Free; see its
[provenance manifest](THIRD_PARTY/font_awesome_eye.toml) and
[third-party notice](THIRD_PARTY_NOTICES.md#font-awesome-free-regular-eye-icon)),
so no font lookup is involved. On Windows, eye mode also
rejects unsolicited foreground-window requests while the application is active;
explicit user switching remains possible. Other platforms receive the
always-on-top hint, subject to their window manager.

The Windows foreground guard is the sole same-process coordinator for
`LockSetForegroundWindow`. Win32 provides neither an ownership token nor a
lock-state query, so other code in the process must not call that API
independently; safe acquisition and release cannot otherwise be coordinated.

`VNM_ChromeTitleBar` and `VNM_ChromeFrameShell` can opt into user-edited
window titles with `title_editing_enabled: true`. Alt+left-click starts editing;
Enter or moving focus away emits `title_edit_accepted(title)` so the owning
application can apply the accepted value. While editing, the available title
region is presented as a text box. The Varinomics mark stays in its Alt pose
unless the always-on-top eye is active; the latched eye intentionally takes
precedence. The capability is disabled by default.

While an accepted title replaces the application's own, the owner sets
`title_reset_available: true`. The text box then shows a Font Awesome
circle-xmark badge (see its [provenance manifest](THIRD_PARTY/font_awesome_circle_xmark.toml));
clicking it ends editing without accepting the draft and emits
`title_reset_requested()`, so the owner can restore its own title and clear
the flag.

`VNM_MonochromeIcon` accepts `source`, logical `extent`, `tint`, and `behind`
(the painted item beneath the icon). It captures the image at three samples
per device pixel in each axis, preserves the artwork aspect, and composites
coverage against a live background capture. A background is required for visible
output because LCD coverage cannot use ordinary single-alpha blending.
`icon_status`, `shader_status`, and `shader_log` expose loading diagnostics;
`icon_object_name` names the inner Image without changing its logical size.

The application supplies resolved `lcd_subpixel_order`: 0 for grayscale,
1/2 for RGB/BGR, or 3/4 for vertical RGB/BGR. Unknown values use grayscale.
The component does not read a process-global display policy. `track_ancestor_motion`
selects geometry behavior: true retains the last settled device-grid correction
through ancestor animation, then commits a final correction; false recalculates
only on local geometry, parent, or device-ratio changes. Bind
`scene_animation_running` to the owning geometry animation when ancestor tracking
is enabled, so pauses retain a continuous correction. This geometry choice is
independent of grayscale/LCD coverage.
