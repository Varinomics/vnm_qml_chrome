import QtQuick
import QtQuick.Window

Item {
    id: shell
    objectName: "chrome_frame_shell"

    default property alias content: content_host.data

    property VNM_ChromeTheme theme: VNM_ChromeTheme {}
    property color frame_color: "#12171e"
    property real frame_outer_edge: 1
    property color frame_outer_edge_color: "#2a313c"
    property real frame_gap: 3
    property real frame_inner_edge: 1
    property color frame_inner_edge_color: "#2a313c"
    property real resize_target_extent: VNM_chrome_geometry.default_resize_target_extent
    // Screen.devicePixelRatio never re-notifies while a window stays on one
    // QScreen, and a Windows scale change updates that screen in place, so a
    // shell bound to it keeps the ratio of the previous scale for the lifetime
    // of the window. The window ratio is the live one. The screen ratio still
    // serves items that have no window yet.
    readonly property real window_device_pixel_ratio:
        Window.window ? Window.window.devicePixelRatio : Screen.devicePixelRatio
    property real device_pixel_ratio: window_device_pixel_ratio
    property real render_target_physical_width: 0
    property real render_target_physical_height: 0
    property real titlebar_height: 32
    property bool resize_enabled: true

    property alias title: titlebar.title
    property alias title_font_family: titlebar.title_font_family
    property alias title_editing_enabled: titlebar.title_editing_enabled
    property alias active: titlebar.active
    property alias maximized: titlebar.maximized
    property alias activity_marker_text: titlebar.activity_marker_text
    property alias mark_pid_reveal_enabled: titlebar.mark_pid_reveal_enabled
    property alias mark_stay_on_top_enabled: titlebar.mark_stay_on_top_enabled
    readonly property bool window_stays_on_top: titlebar.window_stays_on_top
    property alias titlebar_content_left_inset: titlebar.content_left_inset
    property alias leading_action_component: titlebar.leading_action_component
    property alias trailing_action_component: titlebar.trailing_action_component
    property alias custom_buttons: titlebar.custom_buttons
    property alias minimize_button_visible: titlebar.minimize_button_visible
    property alias maximize_button_visible: titlebar.maximize_button_visible
    property alias close_button_visible: titlebar.close_button_visible

    readonly property Item titlebar_item: titlebar
    readonly property rect content_interior_rect: Qt.rect(
        content_interior_x,
        content_interior_y,
        content_interior_width,
        content_interior_height)
    readonly property real content_interior_x: content_left
    readonly property real content_interior_y: content_top
    readonly property real content_interior_width: Math.max(0, content_right - content_left)
    readonly property real content_interior_height: Math.max(0, content_bottom - content_top)

    readonly property real outer_edge: non_negative(frame_outer_edge)
    readonly property real inner_edge: non_negative(frame_inner_edge)
    readonly property real side_gap: non_negative(frame_gap)
    readonly property real titlebar_extent: non_negative(titlebar_height)
    readonly property size detected_render_target_physical_size:
        VNM_system_window.native_window_physical_size(
            Window.window,
            width,
            height,
            device_pixel_ratio)
    readonly property real effective_render_target_physical_width:
        usable_physical_extent(
            width,
            positive_or_default(
                render_target_physical_width,
                detected_render_target_physical_size.width))
    readonly property real effective_render_target_physical_height:
        usable_physical_extent(
            height,
            positive_or_default(
                render_target_physical_height,
                detected_render_target_physical_size.height))
    readonly property real outer_right_far_edge:
        physical_far_edge(width, effective_render_target_physical_width)
    readonly property real outer_bottom_far_edge:
        physical_far_edge(height, effective_render_target_physical_height)
    readonly property real outer_top_far_edge:
        Math.min(snapped_edge(outer_edge), outer_bottom_far_edge)
    readonly property real outer_left_far_edge:
        Math.min(snapped_edge(outer_edge), outer_right_far_edge)
    readonly property real outer_right_edge_width:
        effective_render_target_physical_width > 0
            ? Math.min(snapped_edge(outer_edge), outer_right_far_edge)
            : outer_edge
    readonly property real outer_bottom_edge_width:
        effective_render_target_physical_height > 0
            ? Math.min(snapped_edge(outer_edge), outer_bottom_far_edge)
            : outer_edge
    readonly property real outer_right_near_edge:
        effective_render_target_physical_width > 0
            ? Math.max(0, outer_right_far_edge - outer_right_edge_width)
            : Math.max(0, width - outer_edge)
    readonly property real outer_bottom_near_edge:
        effective_render_target_physical_height > 0
            ? Math.max(0, outer_bottom_far_edge - outer_bottom_edge_width)
            : Math.max(0, height - outer_edge)
    readonly property real side_inner_left:
        snapped_edge(outer_edge + side_gap)
    readonly property real side_gap_width:
        Math.max(0, side_inner_left - outer_left_far_edge)
    readonly property real inner_edge_width:
        Math.max(0, content_left - side_inner_left)
    readonly property real inner_right_far_edge:
        effective_render_target_physical_width > 0
            ? Math.max(0, outer_right_near_edge - side_gap_width)
            : snapped_edge(width - outer_edge - side_gap)
    readonly property real side_inner_right:
        effective_render_target_physical_width > 0
            ? Math.max(0, inner_right_far_edge - inner_edge_width)
            : snapped_edge(width - outer_edge - side_gap - inner_edge)
    readonly property real content_left:
        snapped_edge(outer_edge + side_gap + inner_edge)
    readonly property real content_right:
        side_inner_right
    readonly property real content_top:
        snapped_edge(titlebar_extent + inner_edge)
    readonly property real content_bottom:
        effective_render_target_physical_height > 0
            ? Math.max(0, inner_bottom_far_edge - inner_edge_width)
            : snapped_edge(height - outer_edge - side_gap - inner_edge)
    readonly property real inner_bottom_far_edge:
        effective_render_target_physical_height > 0
            ? Math.max(0, outer_bottom_near_edge - side_gap_width)
            : snapped_edge(height - outer_edge - side_gap)
    readonly property real left_resize_frame_extent: content_left
    readonly property real right_resize_frame_extent:
        Math.max(0, outer_right_far_edge - content_right)
    readonly property real top_resize_frame_extent: outer_top_far_edge
    readonly property real bottom_resize_frame_extent:
        Math.max(0, outer_bottom_far_edge - content_bottom)
    readonly property real left_resize_outward_extent:
        VNM_chrome_geometry.resize_outward_extent(
            left_resize_frame_extent,
            resize_target_extent)
    readonly property real right_resize_outward_extent:
        VNM_chrome_geometry.resize_outward_extent(
            right_resize_frame_extent,
            resize_target_extent)
    readonly property real top_resize_outward_extent:
        VNM_chrome_geometry.resize_outward_extent(
            top_resize_frame_extent,
            resize_target_extent)
    readonly property real bottom_resize_outward_extent:
        VNM_chrome_geometry.resize_outward_extent(
            bottom_resize_frame_extent,
            resize_target_extent)

    signal move_requested()
    signal resize_requested(int edges)
    signal minimize_requested()
    signal maximize_toggle_requested()
    signal close_requested()
    signal title_edit_accepted(string title)

    function non_negative(value) {
        return isFinite(value) ? Math.max(0, value) : 0
    }

    function positive_or_default(value, default_value) {
        return isFinite(value) && value > 0
            ? value
            : non_negative(default_value)
    }

    // A physical extent sampled at a ratio other than the one in force cannot
    // refine any edge, so the frame treats it as absent and lays out from the
    // logical extent alone. Rejecting it here rather than per edge keeps every
    // derived edge on one branch, which is what makes the frame degrade to a
    // whole-pixel rounding difference instead of a rescaled interior.
    function usable_physical_extent(logical_extent, physical_extent) {
        return VNM_chrome_geometry.physical_extent_matches_logical(
                logical_extent,
                physical_extent,
                device_pixel_ratio)
            ? physical_extent
            : 0
    }

    function physical_far_edge(logical_edge, physical_extent) {
        return VNM_chrome_geometry.physical_far_edge(
            logical_edge,
            physical_extent,
            device_pixel_ratio)
    }

    function snapped_edge(value) {
        return VNM_chrome_geometry.snapped_logical_edge(value, device_pixel_ratio)
    }

    VNM_ChromeTheme {
        id: titlebar_theme

        titlebar: shell.theme.titlebar
        titlebar_text: shell.theme.titlebar_text
        titlebar_button_icon: shell.theme.titlebar_button_icon
        titlebar_button_hover: shell.theme.titlebar_button_hover
        titlebar_button_pressed: shell.theme.titlebar_button_pressed
        titlebar_close_hover: shell.theme.titlebar_close_hover
        titlebar_close_pressed: shell.theme.titlebar_close_pressed
        titlebar_activity_marker: shell.theme.titlebar_activity_marker
        titlebar_content_border: "transparent"
        window_frame_border: shell.frame_outer_edge_color
        round_button_background: shell.theme.round_button_background
        round_button_border: shell.theme.round_button_border
        round_button_text: shell.theme.round_button_text
        mark_grey: shell.theme.mark_grey
        mark_orange: shell.theme.mark_orange
        mark_underlay: shell.theme.mark_underlay
    }

    Item {
        id: frame_fill
        objectName: "chrome_frame_shell_frame_fill"

        property color color: shell.frame_color

        anchors.fill: parent
        enabled: false
        z: -100

        Rectangle {
            objectName: "chrome_frame_shell_frame_fill_top"

            x: 0
            y: 0
            width: parent.width
            height: shell.content_top
            color: frame_fill.color
            visible: width > 0 && height > 0 && color.a > 0
        }

        Rectangle {
            objectName: "chrome_frame_shell_frame_fill_bottom"

            x: 0
            y: shell.content_bottom
            width: parent.width
            height: Math.max(0, parent.height - y)
            color: frame_fill.color
            visible: width > 0 && height > 0 && color.a > 0
        }

        Rectangle {
            objectName: "chrome_frame_shell_frame_fill_left"

            x: 0
            y: shell.content_top
            width: shell.content_left
            height: Math.max(0, shell.content_bottom - y)
            color: frame_fill.color
            visible: width > 0 && height > 0 && color.a > 0
        }

        Rectangle {
            objectName: "chrome_frame_shell_frame_fill_right"

            x: shell.content_right
            y: shell.content_top
            width: Math.max(0, parent.width - x)
            height: Math.max(0, shell.content_bottom - y)
            color: frame_fill.color
            visible: width > 0 && height > 0 && color.a > 0
        }
    }

    Item {
        id: content_host
        objectName: "chrome_frame_shell_content"

        x: shell.content_interior_x
        y: shell.content_interior_y
        width: shell.content_interior_width
        height: shell.content_interior_height
        clip: true
        z: 0
    }

    VNM_ChromeTitleBar {
        id: titlebar
        objectName: "chrome_frame_shell_titlebar"

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: shell.titlebar_extent
        theme: titlebar_theme
        resize_enabled: shell.resize_enabled
        resize_target_extent: shell.resize_target_extent
        top_frame_extent: shell.top_resize_frame_extent
        left_frame_extent: shell.left_resize_frame_extent
        right_frame_extent: shell.right_resize_frame_extent
        device_pixel_ratio: shell.device_pixel_ratio
        window_frame_top_visible: shell.outer_edge > 0
        window_frame_width: shell.outer_edge

        onMove_requested: shell.move_requested()
        onResize_requested: (edges) => shell.resize_requested(edges)
        onMinimize_requested: shell.minimize_requested()
        onMaximize_toggle_requested: shell.maximize_toggle_requested()
        onClose_requested: shell.close_requested()
        onTitle_edit_accepted: (title) => shell.title_edit_accepted(title)
    }

    Item {
        id: inner_edge_layer
        objectName: "chrome_frame_shell_inner_edge_layer"

        anchors.fill: parent
        enabled: false
        z: 900

        Rectangle {
            objectName: "chrome_frame_shell_inner_edge_top"

            x: shell.side_inner_left
            y: shell.snapped_edge(shell.titlebar_extent)
            width: Math.max(0, shell.inner_right_far_edge - x)
            height: Math.max(0, shell.content_top - y)
            color: shell.frame_inner_edge_color
            visible: shell.inner_edge > 0 && width > 0 && height > 0 &&
                shell.frame_inner_edge_color.a > 0
        }

        Rectangle {
            objectName: "chrome_frame_shell_inner_edge_bottom"

            x: shell.side_inner_left
            y: shell.content_bottom
            width: Math.max(0, shell.inner_right_far_edge - x)
            height: Math.max(0, shell.inner_bottom_far_edge - y)
            color: shell.frame_inner_edge_color
            visible: shell.inner_edge > 0 && width > 0 && height > 0 &&
                shell.frame_inner_edge_color.a > 0
        }

        Rectangle {
            objectName: "chrome_frame_shell_inner_edge_left"

            x: shell.side_inner_left
            y: shell.snapped_edge(shell.titlebar_extent)
            width: Math.max(0, shell.content_left - x)
            height: Math.max(0, shell.inner_bottom_far_edge - y)
            color: shell.frame_inner_edge_color
            visible: shell.inner_edge > 0 && width > 0 && height > 0 && shell.frame_inner_edge_color.a > 0
        }

        Rectangle {
            objectName: "chrome_frame_shell_inner_edge_right"

            x: shell.side_inner_right
            y: shell.snapped_edge(shell.titlebar_extent)
            width: Math.max(0, shell.inner_right_far_edge - x)
            height: Math.max(0, shell.inner_bottom_far_edge - y)
            color: shell.frame_inner_edge_color
            visible: shell.inner_edge > 0 && width > 0 && height > 0 && shell.frame_inner_edge_color.a > 0
        }
    }

    VNM_ChromeSideResizeLayer {
        objectName: "chrome_frame_shell_side_resize_layer"

        anchors.fill: parent
        resize_enabled: shell.resize_enabled
        resize_target_extent: shell.resize_target_extent
        left_frame_extent: shell.left_resize_frame_extent
        right_frame_extent: shell.right_resize_frame_extent
        onResize_requested: (edges) => shell.resize_requested(edges)
    }

    VNM_ChromeBottomResizeLayer {
        objectName: "chrome_frame_shell_bottom_resize_layer"

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: implicitHeight
        resize_enabled: shell.resize_enabled
        resize_target_extent: shell.resize_target_extent
        left_frame_extent: shell.left_resize_frame_extent
        right_frame_extent: shell.right_resize_frame_extent
        bottom_frame_extent: shell.bottom_resize_frame_extent
        onResize_requested: (edges) => shell.resize_requested(edges)
    }

    Item {
        id: outer_edge_layer
        objectName: "chrome_frame_shell_outer_frame"

        property bool frame_visible: shell.outer_edge > 0
        property real frame_width: shell.outer_edge
        property bool top_edge_visible: false
        property bool bottom_edge_visible: true
        property bool left_edge_visible: true
        property bool right_edge_visible: true

        anchors.fill: parent
        enabled: false
        visible: frame_visible && shell.frame_outer_edge_color.a > 0
        z: 1000

        Rectangle {
            objectName: "chrome_window_frame_top"

            x: 0
            y: 0
            width: Math.max(0, shell.outer_right_far_edge)
            height: Math.max(0, shell.outer_top_far_edge)
            color: shell.frame_outer_edge_color
            visible: outer_edge_layer.visible
                && outer_edge_layer.top_edge_visible
                && width > 0
                && height > 0
        }

        Rectangle {
            objectName: "chrome_window_frame_bottom"

            x: 0
            y: shell.outer_bottom_near_edge
            width: Math.max(0, shell.outer_right_far_edge)
            height: Math.max(0, shell.outer_bottom_far_edge - y)
            color: shell.frame_outer_edge_color
            visible: outer_edge_layer.visible
                && outer_edge_layer.bottom_edge_visible
                && width > 0
                && height > 0

        }

        Rectangle {
            objectName: "chrome_window_frame_left"

            x: 0
            y: 0
            width: Math.max(0, shell.outer_left_far_edge)
            height: Math.max(0, shell.outer_bottom_far_edge)
            color: shell.frame_outer_edge_color
            visible: outer_edge_layer.visible
                && outer_edge_layer.left_edge_visible
                && width > 0
                && height > 0
        }

        Rectangle {
            objectName: "chrome_window_frame_right"

            x: shell.outer_right_near_edge
            y: 0
            width: Math.max(0, shell.outer_right_far_edge - x)
            height: Math.max(0, shell.outer_bottom_far_edge)
            color: shell.frame_outer_edge_color
            visible: outer_edge_layer.visible
                && outer_edge_layer.right_edge_visible
                && width > 0
                && height > 0

        }
    }
}
