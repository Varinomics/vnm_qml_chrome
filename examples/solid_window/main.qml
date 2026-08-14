import QtQuick
import QtQuick.Window
import VNM_Chrome

Window {
    id: root

    width: 720
    height: 420
    visible: true
    flags: Qt.Window | Qt.FramelessWindowHint
    title: "VNM Chrome Example"
    color: "#1b1d20"

    readonly property real device_pixel_ratio: root.screen
        ? root.screen.devicePixelRatio
        : 1
    readonly property real frame_width: 1
    readonly property bool frame_visible: visibility !== Window.FullScreen

    VNM_ChromeTheme {
        id: chrome_theme

        titlebar: "#202124"
        titlebar_content_border: "#35363a"
        window_frame_border: "#35363a"
    }

    function toggle_window_maximized() {
        if (visibility === Window.Maximized) {
            showNormal()
        }
        else {
            showMaximized()
        }
    }

    VNM_ChromeFrameShell {
        id: chrome_shell

        anchors.fill: parent
        theme: chrome_theme
        title: root.title
        active: root.active
        maximized: root.visibility === Window.Maximized
        mark_pid_reveal_enabled: true
        resize_enabled: root.visibility === Window.Windowed
        device_pixel_ratio: root.device_pixel_ratio
        frame_color: root.color
        frame_outer_edge: root.frame_visible ? root.frame_width : 0
        frame_outer_edge_color: chrome_theme.window_frame_border
        frame_gap: root.frame_visible ? 3 : 0
        frame_inner_edge: root.frame_visible ? root.frame_width : 0
        frame_inner_edge_color: chrome_theme.window_frame_border

        onMove_requested: VNM_system_window.start_system_move(root)
        onResize_requested: (edges) =>
            VNM_system_window.start_system_resize(root, edges)
        onMinimize_requested: root.showMinimized()
        onMaximize_toggle_requested: root.toggle_window_maximized()
        onClose_requested: root.close()

        Rectangle {
            anchors.fill: parent
            color: "#263238"
        }
    }

    VNM_NativeWindowFrame {
        window: root
        frame_visible: false
        resize_enabled: chrome_shell.resize_enabled
        resize_outward_margins.left: chrome_shell.left_resize_outward_extent
        resize_outward_margins.top: chrome_shell.top_resize_outward_extent
        resize_outward_margins.right: chrome_shell.right_resize_outward_extent
        resize_outward_margins.bottom: chrome_shell.bottom_resize_outward_extent
    }
}
