import QtQuick

Item {
    id: layer
    objectName: "side_resize_layer"

    property bool resize_enabled: true
    property real resize_target_extent: VNM_chrome_geometry.default_resize_target_extent
    property real left_frame_extent: 0
    property real right_frame_extent: 0
    readonly property real left_frame: non_negative(left_frame_extent)
    readonly property real right_frame: non_negative(right_frame_extent)
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

    signal resize_requested(int edges)

    function non_negative(value) {
        return isFinite(value) ? Math.max(0, value) : 0
    }

    function resize_edges_for_y(base_edges, local_y) {
        const corner_height = Math.max(
            0,
            Math.min(
                Math.max(left_resize_hit_extent, right_resize_hit_extent),
                height / 2))
        if (local_y <= corner_height) {
            return base_edges | Qt.TopEdge
        }
        if (local_y >= height - corner_height) {
            return base_edges | Qt.BottomEdge
        }
        return base_edges
    }

    function resize_cursor_for_y(base_edges, local_y) {
        const resolved_edges = resize_edges_for_y(base_edges, local_y)
        const left_side = (base_edges & Qt.LeftEdge) !== 0

        if ((resolved_edges & Qt.TopEdge) !== 0) {
            return left_side ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor
        }
        if ((resolved_edges & Qt.BottomEdge) !== 0) {
            return left_side ? Qt.SizeBDiagCursor : Qt.SizeFDiagCursor
        }
        return Qt.SizeHorCursor
    }

    z: 10

    VNM_ChromeResizeArea {
        objectName: "left_resize_area"
        x: -layer.left_resize_outward_extent
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: layer.left_resize_hit_extent
        enabled: layer.resize_enabled
        edges: Qt.LeftEdge
        resolve_edges: (mouse) => layer.resize_edges_for_y(Qt.LeftEdge, mouse.y)
        hoverEnabled: true
        cursorShape: containsMouse
            ? layer.resize_cursor_for_y(Qt.LeftEdge, mouseY)
            : Qt.SizeHorCursor

        onResize_requested: (edges) => layer.resize_requested(edges)
    }

    VNM_ChromeResizeArea {
        objectName: "right_resize_area"
        x: layer.width
            - layer.right_frame
            - layer.right_resize_inward_extent
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: layer.right_resize_hit_extent
        enabled: layer.resize_enabled
        edges: Qt.RightEdge
        resolve_edges: (mouse) => layer.resize_edges_for_y(Qt.RightEdge, mouse.y)
        hoverEnabled: true
        cursorShape: containsMouse
            ? layer.resize_cursor_for_y(Qt.RightEdge, mouseY)
            : Qt.SizeHorCursor

        onResize_requested: (edges) => layer.resize_requested(edges)
    }
}
