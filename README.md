# vnm_qml_chrome

[![CI Linux](https://github.com/Varinomics/vnm_qml_chrome/actions/workflows/ci-linux.yml/badge.svg?branch=master)](https://github.com/Varinomics/vnm_qml_chrome/actions/workflows/ci-linux.yml) [![CI macOS](https://github.com/Varinomics/vnm_qml_chrome/actions/workflows/ci-macos.yml/badge.svg?branch=master)](https://github.com/Varinomics/vnm_qml_chrome/actions/workflows/ci-macos.yml) [![CI Windows](https://github.com/Varinomics/vnm_qml_chrome/actions/workflows/ci-windows.yml/badge.svg?branch=master)](https://github.com/Varinomics/vnm_qml_chrome/actions/workflows/ci-windows.yml) [![CI FreeBSD](https://github.com/Varinomics/vnm_qml_chrome/actions/workflows/ci-freebsd.yml/badge.svg?branch=master)](https://github.com/Varinomics/vnm_qml_chrome/actions/workflows/ci-freebsd.yml)

Requires Qt 6.11.1 or newer.

Set `title_font_family` on `VNM_ChromeTitleBar` or
`VNM_ChromeFrameShell` to use an application-supplied title font. The caller
owns loading and packaging that font; `vnm_qml_chrome` only applies the family
name to the title label and title editor. Leave the family empty to retain each
control's default font.

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
