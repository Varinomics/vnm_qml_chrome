import QtQuick

Item {
    id: layer
    objectName: "bottom_resize_layer"

    property bool resize_enabled: true
    property real resize_target_extent: VNM_chrome_geometry.default_resize_target_extent
    property real left_frame_extent: 0
    property real right_frame_extent: 0
    property real bottom_frame_extent: 0
    readonly property real left_frame: non_negative(left_frame_extent)
    readonly property real right_frame: non_negative(right_frame_extent)
    readonly property real bottom_frame: non_negative(bottom_frame_extent)
    readonly property real left_resize_inward_extent:
        VNM_chrome_geometry.resize_inward_extent(left_frame, resize_target_extent)
    readonly property real left_resize_outward_extent:
        VNM_chrome_geometry.resize_outward_extent(left_frame, resize_target_extent)
    readonly property real left_resize_hit_extent:
        left_resize_inward_extent + left_frame + left_resize_outward_extent
    readonly property real right_resize_inward_extent:
        VNM_chrome_geometry.resize_inward_extent(right_frame, resize_target_extent)
    readonly property real right_resize_outward_extent:
        VNM_chrome_geometry.resize_outward_extent(right_frame, resize_target_extent)
    readonly property real right_resize_hit_extent:
        right_resize_inward_extent + right_frame + right_resize_outward_extent
    readonly property real bottom_resize_inward_extent:
        VNM_chrome_geometry.resize_inward_extent(bottom_frame, resize_target_extent)
    readonly property real bottom_resize_outward_extent:
        VNM_chrome_geometry.resize_outward_extent(bottom_frame, resize_target_extent)
    readonly property real bottom_resize_hit_extent:
        bottom_resize_inward_extent + bottom_frame + bottom_resize_outward_extent

    signal resize_requested(int edges)

    function non_negative(value) {
        return isFinite(value) ? Math.max(0, value) : 0
    }

    implicitHeight: bottom_resize_hit_extent
    z: 30

    VNM_ChromeResizeArea {
        objectName: "bottom_resize_area"
        anchors.left: parent.left
        anchors.leftMargin: layer.left_frame + layer.left_resize_inward_extent
        anchors.right: parent.right
        anchors.rightMargin: layer.right_frame + layer.right_resize_inward_extent
        y: layer.height
            - layer.bottom_frame
            - layer.bottom_resize_inward_extent
        height: layer.bottom_resize_hit_extent
        enabled: layer.resize_enabled
        edges: Qt.BottomEdge
        cursorShape: Qt.SizeVerCursor
        z: 1

        onResize_requested: (edges) => layer.resize_requested(edges)
    }

    VNM_ChromeResizeArea {
        objectName: "bottom_left_resize_area"
        x: -layer.left_resize_outward_extent
        y: layer.height
            - layer.bottom_frame
            - layer.bottom_resize_inward_extent
        width: layer.left_resize_hit_extent
        height: layer.bottom_resize_hit_extent
        enabled: layer.resize_enabled
        edges: Qt.LeftEdge | Qt.BottomEdge
        cursorShape: Qt.SizeBDiagCursor
        z: 2

        onResize_requested: (edges) => layer.resize_requested(edges)
    }

    VNM_ChromeResizeArea {
        objectName: "bottom_right_resize_area"
        x: layer.width
            - layer.right_frame
            - layer.right_resize_inward_extent
        y: layer.height
            - layer.bottom_frame
            - layer.bottom_resize_inward_extent
        width: layer.right_resize_hit_extent
        height: layer.bottom_resize_hit_extent
        enabled: layer.resize_enabled
        edges: Qt.RightEdge | Qt.BottomEdge
        cursorShape: Qt.SizeFDiagCursor
        z: 2

        onResize_requested: (edges) => layer.resize_requested(edges)
    }
}
