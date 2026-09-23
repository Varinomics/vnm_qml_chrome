import QtQuick
import QtQuick.Shapes
import VNM_Chrome.Private 1.0 as Private

Item {
    id: control

    // Preserve logical layout and border weight while aligning the painted
    // perimeter to the nominal DPR grid, not the RHI's rounded viewport.
    // Origin and span are rounded separately to keep the painted path size
    // stable during translation. The near edge can shift by half a physical
    // pixel and the far edge by one, plus native antialiasing fringe. The owner
    // must leave unoccluded paint room; intentional ancestor clips still apply.
    property color color: "transparent"
    property alias fill_gradient: border_path.fillGradient
    property color border_color: "transparent"
    property real border_width: 0
    property alias radius: border_rectangle.radius
    property alias topLeftRadius: border_rectangle.topLeftRadius
    property alias topRightRadius: border_rectangle.topRightRadius
    property alias bottomLeftRadius: border_rectangle.bottomLeftRadius
    property alias bottomRightRadius: border_rectangle.bottomRightRadius

    readonly property real effective_border_width: Math.max(0, border_width)
    readonly property real device_pixel_ratio: geometry.device_pixel_ratio
    readonly property bool snapping_active: geometry.snapping_active

    Private.VNM_DevicePixelGeometryItem {
        id: geometry

        // CurveRenderer loses its path nodes with the scene graph. Reattach the
        // retained path so unchanged controls regenerate without needing hover.
        onScene_graph_recreated: {
            painted_shape.data = [];
            painted_shape.data = [border_path];
        }

        Shape {
            id: painted_shape

            objectName: "painted_shape"
            anchors.fill: parent
            // CurveRenderer supplies coverage antialiasing without window MSAA.
            preferredRendererType: Shape.CurveRenderer
            antialiasing: true

            ShapePath {
                id: border_path

                objectName: "border_shape_path"
                fillColor: control.color
                strokeColor: control.effective_border_width > 0
                    && control.border_color.a > 0
                    ? control.border_color
                    : "transparent"
                strokeWidth: strokeColor.a > 0
                    ? control.effective_border_width
                    : 0

                PathRectangle {
                    id: border_rectangle

                    objectName: "border_path_rectangle"
                    width: painted_shape.width
                    height: painted_shape.height
                    radius: 0
                    strokeAdjustment: border_path.strokeWidth
                }
            }
        }
    }
}
