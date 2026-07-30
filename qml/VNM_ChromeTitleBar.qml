import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Rectangle {
    id: titlebar
    objectName: "titlebar"

    property VNM_ChromeTheme theme: VNM_ChromeTheme {}
    property string title: ""
    property bool active: true
    property bool maximized: false
    property bool resize_enabled: true
    property real resize_target_extent: VNM_chrome_geometry.default_resize_target_extent
    property real top_frame_extent: 0
    property real left_frame_extent: 0
    property real right_frame_extent: 0
    property real device_pixel_ratio: Screen.devicePixelRatio
    property bool animated_mark_visible: true
    property string activity_marker_text: ""
    property bool window_frame_top_visible: false
    property real window_frame_width: 0
    property Component leading_action_component: null
    property Component trailing_action_component: null
    // Declarative custom buttons rendered as peers of the window controls
    // (to their left, in list order). Each entry is a plain object:
    //   {
    //     object_name?: string,
    //     glyph?: string, font_family?: string, pixel_size?: real,  // font icon
    //     svg?: url,                                                 // or svg icon
    //     width?: real, tooltip?: string, hover_color?: color,
    //     action?: function,
    //   }
    // A font glyph follows the titlebar theme colour; an svg is drawn as-is.
    property var custom_buttons: []
    property bool minimize_button_visible: true
    property bool maximize_button_visible: true
    property bool close_button_visible: true
    readonly property real top_frame: non_negative(top_frame_extent)
    readonly property real left_frame: non_negative(left_frame_extent)
    readonly property real right_frame: non_negative(right_frame_extent)
    readonly property real top_resize_inward_extent:
        VNM_chrome_geometry.resize_inward_extent(top_frame, resize_target_extent)
    readonly property real top_resize_outward_extent:
        VNM_chrome_geometry.resize_outward_extent(top_frame, resize_target_extent)
    readonly property real left_resize_inward_extent:
        VNM_chrome_geometry.resize_inward_extent(left_frame, resize_target_extent)
    readonly property real left_resize_outward_extent:
        VNM_chrome_geometry.resize_outward_extent(left_frame, resize_target_extent)
    readonly property real right_resize_inward_extent:
        VNM_chrome_geometry.resize_inward_extent(right_frame, resize_target_extent)
    readonly property real right_resize_outward_extent:
        VNM_chrome_geometry.resize_outward_extent(right_frame, resize_target_extent)
    readonly property real snapped_top_resize_near_extent:
        snapped_extent(top_frame + top_resize_inward_extent)
    readonly property real snapped_top_resize_outward_extent:
        snapped_extent(top_resize_outward_extent)
    readonly property real snapped_top_resize_hit_extent:
        snapped_top_resize_near_extent + snapped_top_resize_outward_extent
    readonly property real snapped_left_resize_near_extent:
        snapped_extent(left_frame + left_resize_inward_extent)
    readonly property real snapped_left_resize_outward_extent:
        snapped_extent(left_resize_outward_extent)
    readonly property real snapped_left_resize_hit_extent:
        snapped_left_resize_near_extent + snapped_left_resize_outward_extent
    readonly property real snapped_right_resize_near_extent:
        snapped_extent(right_frame + right_resize_inward_extent)
    readonly property real snapped_right_resize_outward_extent:
        snapped_extent(right_resize_outward_extent)
    readonly property real snapped_right_resize_hit_extent:
        snapped_right_resize_near_extent + snapped_right_resize_outward_extent
    readonly property real content_border_width:
        1 / VNM_chrome_geometry.normalized_device_pixel_ratio(device_pixel_ratio)
    readonly property real window_frame_top_width:
        isFinite(window_frame_width) && window_frame_width > 0
            ? Math.min(
                VNM_chrome_geometry.snapped_logical_edge(
                    window_frame_width,
                    device_pixel_ratio),
                height)
            : 0
    readonly property int move_drag_threshold: 2

    signal move_requested()
    signal resize_requested(int edges)
    signal minimize_requested()
    signal maximize_toggle_requested()
    signal close_requested()

    function non_negative(value) {
        return isFinite(value) ? Math.max(0, value) : 0
    }

    function snapped_extent(value) {
        return VNM_chrome_geometry.snapped_logical_edge(value, device_pixel_ratio)
    }

    function maybe_start_system_move(move_area, mouse) {
        if (move_area.system_move_started || !(mouse.buttons & Qt.LeftButton)) {
            return
        }

        const delta_x = mouse.x - move_area.system_move_press_x
        const delta_y = mouse.y - move_area.system_move_press_y
        if (delta_x * delta_x + delta_y * delta_y
                < move_drag_threshold * move_drag_threshold) {
            return
        }

        move_area.system_move_started = true
        titlebar.move_requested()
        mouse.accepted = true
    }

    height: 32
    color: theme.titlebar
    z: 40

    Rectangle {
        objectName: "titlebar_window_frame_top"

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: titlebar.window_frame_top_width
        color: titlebar.theme.window_frame_border
        visible: titlebar.window_frame_top_visible
            && titlebar.window_frame_top_width > 0
            && titlebar.theme.window_frame_border.a > 0
        enabled: false
        z: 0
    }

    MouseArea {
        id: titlebar_move_area

        property real system_move_press_x: 0
        property real system_move_press_y: 0
        property bool system_move_started: false

        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        z: 1

        onPressed: (mouse) => {
            if (mouse.button === Qt.LeftButton) {
                system_move_press_x = mouse.x
                system_move_press_y = mouse.y
                system_move_started = false
                mouse.accepted = true
            }
        }

        onPositionChanged: (mouse) => {
            titlebar.maybe_start_system_move(titlebar_move_area, mouse)
        }

        onReleased: system_move_started = false
        onCanceled: system_move_started = false

        onDoubleClicked: (mouse) => {
            if (mouse.button === Qt.LeftButton && titlebar.maximize_button_visible) {
                titlebar.maximize_toggle_requested()
                mouse.accepted = true
            }
        }
    }

    RowLayout {
        id: content_layout

        anchors.fill: parent
        anchors.leftMargin: titlebar.snapped_left_resize_near_extent
        spacing: 0
        z: 2

        VNM_AnimatedMark {
            id: animated_mark
            objectName: "vnm_animated_mark"

            theme: titlebar.theme
            mark_size: 20
            move_enabled: true
            move_drag_threshold: titlebar.move_drag_threshold
            visible: titlebar.animated_mark_visible
            Layout.preferredWidth: mark_size
            Layout.preferredHeight: mark_size
            Layout.alignment: Qt.AlignVCenter
            Layout.rightMargin: titlebar.animated_mark_visible ? 8 : 0
            onMove_requested: titlebar.move_requested()
            onMaximize_toggle_requested: titlebar.maximize_toggle_requested()
        }

        Label {
            id: activity_marker_label
            objectName: "activity_marker_label"

            property bool marker_visible: false

            function retain_marker_placeholder_if_needed() {
                if (text.length > 0) {
                    marker_visible = true
                }
            }

            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: marker_visible ? 18 : 0
            Layout.rightMargin: marker_visible ? 6 : 0
            visible: marker_visible
            text: titlebar.activity_marker_text
            color: titlebar.theme.titlebar_activity_marker
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.pointSize: 9.5

            Component.onCompleted: retain_marker_placeholder_if_needed()
            onTextChanged: retain_marker_placeholder_if_needed()
        }

        Loader {
            id: leading_action_loader
            objectName: "leading_action_loader"

            sourceComponent: titlebar.leading_action_component
            active: titlebar.leading_action_component !== null
            Layout.rightMargin: active ? 6 : 0
            Layout.alignment: Qt.AlignVCenter
        }

        Label {
            id: title_label
            objectName: "title_label"

            Layout.alignment: Qt.AlignVCenter
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.rightMargin: 8
            text: titlebar.title
            color: titlebar.theme.titlebar_text
            elide: Text.ElideRight
            font.pointSize: 9.5
        }

        Loader {
            id: trailing_action_loader
            objectName: "trailing_action_loader"

            sourceComponent: titlebar.trailing_action_component
            active: titlebar.trailing_action_component !== null
            Layout.rightMargin: active ? 8 : 0
            Layout.alignment: Qt.AlignVCenter
        }

        Row {
            id: custom_buttons_row
            objectName: "custom_buttons_row"

            Layout.fillHeight: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 0

            Repeater {
                model: titlebar.custom_buttons

                delegate: VNM_ChromeWindowButton {
                    id: custom_button
                    required property var modelData

                    objectName: (modelData.object_name !== undefined)
                        ? modelData.object_name
                        : ""
                    theme: titlebar.theme
                    hover_color: (modelData.hover_color !== undefined)
                        ? modelData.hover_color
                        : titlebar.theme.titlebar_button_hover
                    width: (modelData.width !== undefined) ? modelData.width : 46
                    height: titlebar.height

                    ToolTip.text: (modelData.tooltip !== undefined) ? modelData.tooltip : ""
                    ToolTip.delay: 500
                    ToolTip.visible: hovered && ToolTip.text.length > 0

                    onClicked: {
                        if (typeof modelData.action === "function") {
                            modelData.action()
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: (modelData.glyph !== undefined) && (modelData.glyph.length > 0)
                        text: visible ? modelData.glyph : ""
                        color: titlebar.theme.titlebar_button_icon
                        font.family: (modelData.font_family !== undefined)
                            ? modelData.font_family
                            : Qt.application.font.family
                        font.pixelSize: (modelData.pixel_size !== undefined)
                            ? modelData.pixel_size
                            : 14
                    }

                    Image {
                        anchors.centerIn: parent
                        visible: (modelData.svg !== undefined) && (String(modelData.svg).length > 0)
                        source: visible ? modelData.svg : ""
                        sourceSize.width: (modelData.pixel_size !== undefined)
                            ? modelData.pixel_size
                            : 16
                        sourceSize.height: (modelData.pixel_size !== undefined)
                            ? modelData.pixel_size
                            : 16
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                    }
                }
            }
        }

        RowLayout {
            id: titlebar_buttons
            objectName: "titlebar_buttons"

            Layout.preferredWidth: (titlebar.minimize_button_visible ? 46 : 0)
                + (titlebar.maximize_button_visible ? 46 : 0)
                + (titlebar.close_button_visible ? 46 : 0)
            Layout.fillHeight: true
            spacing: 0

            VNM_ChromeWindowButton {
                id: minimize_button
                objectName: "minimize_button"

                visible: titlebar.minimize_button_visible
                theme: titlebar.theme
                Layout.preferredWidth: visible ? 46 : 0
                Layout.fillHeight: true
                onClicked: titlebar.minimize_requested()

                Canvas {
                    anchors.fill: parent
                    property color stroke_color: titlebar.theme.titlebar_button_icon

                    onStroke_colorChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        ctx.strokeStyle = stroke_color
                        ctx.lineWidth = 1.3
                        ctx.lineCap = "square"

                        const size = 10
                        const x = (width - size) / 2
                        const y = (height - size) / 2 + 3
                        ctx.beginPath()
                        ctx.moveTo(x, y)
                        ctx.lineTo(x + size, y)
                        ctx.stroke()
                    }

                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                }
            }

            VNM_ChromeWindowButton {
                id: maximize_button
                objectName: "maximize_button"

                visible: titlebar.maximize_button_visible
                theme: titlebar.theme
                Layout.preferredWidth: visible ? 46 : 0
                Layout.fillHeight: true
                onClicked: titlebar.maximize_toggle_requested()

                Canvas {
                    anchors.fill: parent
                    property color stroke_color: titlebar.theme.titlebar_button_icon
                    property bool restored: titlebar.maximized

                    onStroke_colorChanged: requestPaint()
                    onRestoredChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        ctx.strokeStyle = stroke_color
                        ctx.lineWidth = 1.3
                        ctx.lineCap = "square"

                        const size = 10
                        let x = (width - size) / 2
                        let y = (height - size) / 2
                        if (restored) {
                            x -= 1
                            y += 1
                            ctx.strokeRect(x + 3.5, y - 2.5, size - 1, size - 1)
                            ctx.strokeRect(x + 0.5, y + 0.5, size - 1, size - 1)
                        }
                        else {
                            ctx.strokeRect(x + 0.5, y + 0.5, size - 1, size - 1)
                        }
                    }

                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                }
            }

            VNM_ChromeWindowButton {
                id: close_button
                objectName: "close_button"

                visible: titlebar.close_button_visible
                theme: titlebar.theme
                hover_color: titlebar.theme.titlebar_close_hover
                pressed_color: titlebar.theme.titlebar_close_pressed
                Layout.preferredWidth: visible ? 46 : 0
                Layout.fillHeight: true
                onClicked: titlebar.close_requested()

                Canvas {
                    anchors.fill: parent
                    property color stroke_color: titlebar.theme.titlebar_button_icon

                    onStroke_colorChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        ctx.strokeStyle = stroke_color
                        ctx.lineWidth = 1.3
                        ctx.lineCap = "square"

                        const size = 8
                        const x = (width - size) / 2
                        const y = (height - size) / 2
                        ctx.beginPath()
                        ctx.moveTo(x, y)
                        ctx.lineTo(x + size, y + size)
                        ctx.moveTo(x, y + size)
                        ctx.lineTo(x + size, y)
                        ctx.stroke()
                    }

                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                }
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: titlebar.content_border_width
        color: titlebar.theme.titlebar_content_border
        visible: titlebar.theme.titlebar_content_border.a > 0
        z: 2
    }

    VNM_ChromeResizeArea {
        objectName: "top_resize_area"
        anchors.left: parent.left
        anchors.leftMargin: titlebar.snapped_left_resize_near_extent
        anchors.right: parent.right
        anchors.rightMargin: titlebar_buttons.width + custom_buttons_row.width
        y: -titlebar.snapped_top_resize_outward_extent
        height: titlebar.snapped_top_resize_hit_extent
        enabled: titlebar.resize_enabled
        edges: Qt.TopEdge
        cursorShape: Qt.SizeVerCursor
        z: 4

        onResize_requested: (edges) => titlebar.resize_requested(edges)
    }

    VNM_ChromeResizeArea {
        objectName: "top_left_resize_area"
        x: -titlebar.snapped_left_resize_outward_extent
        y: -titlebar.snapped_top_resize_outward_extent
        width: titlebar.snapped_left_resize_hit_extent
        height: titlebar.snapped_top_resize_hit_extent
        enabled: titlebar.resize_enabled
        edges: Qt.LeftEdge | Qt.TopEdge
        cursorShape: Qt.SizeFDiagCursor
        z: 5

        onResize_requested: (edges) => titlebar.resize_requested(edges)
    }

    VNM_ChromeResizeArea {
        objectName: "top_right_resize_area"
        x: titlebar.width - titlebar.snapped_right_resize_near_extent
        y: -titlebar.snapped_top_resize_outward_extent
        width: titlebar.snapped_right_resize_hit_extent
        height: titlebar.snapped_top_resize_hit_extent
        enabled: titlebar.resize_enabled
        edges: Qt.RightEdge | Qt.TopEdge
        cursorShape: Qt.SizeBDiagCursor
        z: 5

        onResize_requested: (edges) => titlebar.resize_requested(edges)
    }
}
