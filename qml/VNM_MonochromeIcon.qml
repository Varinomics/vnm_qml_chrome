// Monochrome image coverage with live background capture and device-grid geometry.
// Callers provide the background item and resolved display coverage policy.

import QtQuick
import QtQuick.Window

Item {
    id: icon

    property url source
    property real extent: 16
    property color tint: "white"
    property Item behind: null
    // Bind this to the running state when this item or an ancestor has a
    // geometry animation. The explicit lifecycle keeps pauses and low-cadence
    // animations continuous; the quiet fallback below is for unannounced
    // one-off geometry changes.
    property bool scene_animation_running: false
    // Products choose whether ancestor motion participates in settled snapping.
    property bool track_ancestor_motion: true
    // The inner Image carries it, so a test finds the same object it did when
    // this was an Image and a MultiEffect written out in the panel.
    property string icon_object_name: ""
    // The inner Image's load status, under the same prefix as the object name
    // above because both name the Image rather than this item. A caller that
    // has to wait for the source to arrive has nothing else to wait on.
    readonly property alias icon_status: supersampled.status
    readonly property alias shader_status: tinted.status
    readonly property alias shader_log: tinted.log

    readonly property var icon_window: icon.Window.window
    // Resolved display coverage: 0 grayscale, 1 RGB, 2 BGR, 3 VRGB, 4 VBGR.
    // Unknown values use grayscale. The application owns display-policy resolution.
    property int lcd_subpixel_order: 0
    readonly property real effective_device_pixel_ratio: {
        const window_device_pixel_ratio = icon_window
            ? Number(icon_window.devicePixelRatio)
            : 1;
        return Number.isFinite(window_device_pixel_ratio)
            ? Math.max(1.0, window_device_pixel_ratio)
            : 1.0;
    }

    readonly property int device_extent: Math.max(
        1,
        Math.round(extent * effective_device_pixel_ratio))

    property point sampled_scene_origin: Qt.point(0, 0)
    property real sampled_device_pixel_ratio: 1.0
    property point settled_device_origin_snap: Qt.point(0, 0)
    property bool scene_tracking_initialized: false
    property bool scene_motion_pending: false
    readonly property bool scene_motion_active:
        scene_animation_running || scene_motion_pending
    readonly property int scene_settle_quiet_interval: 75

    width: device_extent / effective_device_pixel_ratio
    height: width

    function snapped_scene_coordinate(coordinate, ratio) {
        const numeric_coordinate = Number(coordinate)
        const numeric_ratio = Number(ratio)
        const finite_coordinate = Number.isFinite(numeric_coordinate)
            ? numeric_coordinate
            : 0
        const effective_ratio = Number.isFinite(numeric_ratio)
            ? Math.max(1.0, numeric_ratio)
            : 1.0
        return Math.round(finite_coordinate * effective_ratio) /
            effective_ratio
    }

    function current_scene_origin() {
        return parent !== null
            ? parent.mapToItem(null, x, y)
            : Qt.point(0, 0)
    }

    function snap_for_scene_origin(scene_origin, ratio) {
        const snapped_scene_origin = Qt.point(
            snapped_scene_coordinate(scene_origin.x, ratio),
            snapped_scene_coordinate(scene_origin.y, ratio))
        return mapFromItem(
            null,
            snapped_scene_origin.x,
            snapped_scene_origin.y)
    }

    function scene_sample_changed(scene_origin, ratio) {
        return Math.abs(scene_origin.x - sampled_scene_origin.x) > 0.000001 ||
            Math.abs(scene_origin.y - sampled_scene_origin.y) > 0.000001 ||
            Math.abs(ratio - sampled_device_pixel_ratio) > 0.000001
    }

    function track_scene_geometry() {
        if (!track_ancestor_motion) {
            return
        }
        const scene_origin = current_scene_origin()
        const ratio = effective_device_pixel_ratio

        if (!scene_tracking_initialized) {
            sampled_scene_origin = scene_origin
            sampled_device_pixel_ratio = ratio
            settled_device_origin_snap =
                snap_for_scene_origin(scene_origin, ratio)
            scene_tracking_initialized = true
            scene_motion_pending = false
            return
        }

        if (scene_sample_changed(scene_origin, ratio)) {
            sampled_scene_origin = scene_origin
            sampled_device_pixel_ratio = ratio
            scene_motion_pending = true

            // A changing ancestor must keep the last settled correction. That
            // makes every rendered delta equal the animation's raw delta
            // instead of quantising a slow transition onto the pixel grid.
            if (scene_animation_running) {
                scene_settle_timer.stop()
            } else {
                scene_settle_timer.restart()
            }
            return
        }
    }

    function settle_scene_geometry() {
        if (!scene_motion_pending) {
            return
        }
        if (scene_animation_running) {
            scene_settle_timer.stop()
            return
        }

        const scene_origin = current_scene_origin()
        const ratio = effective_device_pixel_ratio
        if (scene_sample_changed(scene_origin, ratio)) {
            sampled_scene_origin = scene_origin
            sampled_device_pixel_ratio = ratio
            scene_settle_timer.restart()
            return
        }

        settled_device_origin_snap =
            snap_for_scene_origin(scene_origin, ratio)
        scene_motion_pending = false
        scene_settle_timer.stop()
    }

    function handle_scene_animation_running_changed() {
        if (!track_ancestor_motion) {
            return
        }
        track_scene_geometry()
        if (scene_animation_running) {
            scene_motion_pending = true
            scene_settle_timer.stop()
            return
        }

        // The owner's animation lifecycle is authoritative: its transition to
        // stopped commits one final correction immediately and exactly once.
        settle_scene_geometry()
    }

    Component.onCompleted: track_scene_geometry()
    onParentChanged: track_scene_geometry()
    onIcon_windowChanged: track_scene_geometry()
    onEffective_device_pixel_ratioChanged: track_scene_geometry()
    onScene_animation_runningChanged:
        handle_scene_animation_running_changed()

    // afterAnimating runs on the GUI thread before scene-graph sync. Sampling
    // there sees ancestor motion even though mapToItem() cannot establish QML
    // dependencies on ancestor geometry, and follows window/screen changes as
    // Window.window retargets this connection. Sampling follows that lifecycle
    // signal rather than a generic polling timer.
    Connections {
        target: icon.icon_window
        enabled: target !== null && icon.track_ancestor_motion

        function onAfterAnimating() {
            icon.track_scene_geometry()
        }
    }

    // Debounce the lifecycle samples instead of treating a duplicate render
    // as rest: render loops can present the same coordinate more than once
    // while a slow NumberAnimation is still active. This one-shot does not
    // poll; it runs only after three nominal 40 Hz frame intervals with no
    // observed geometry change, then commits exactly one settled correction.
    Timer {
        id: scene_settle_timer

        interval: icon.scene_settle_quiet_interval
        repeat: false
        onTriggered: icon.settle_scene_geometry()
    }

    // Where the shader quad lands, rounded onto the device grid. The shader
    // picks its source column with floor() over the quad's own texture
    // coordinate, so a quad starting on a half device pixel makes that floor
    // land on a boundary partway down: one source column is read twice and its
    // neighbour is skipped, and a straight edge comes out with a step in it.
    // Rounding the extent, which this already did, does not help - the origin
    // is what the sampling grid hangs off. It goes on the quad rather than on x
    // and y, which the caller's anchors own.
    readonly property point device_origin_snap: {
        if (track_ancestor_motion) {
            return settled_device_origin_snap
        }
        // Local geometry is the product-selected snapping boundary. mapToItem
        // deliberately does not subscribe this binding to ancestor movement.
        void icon.x
        void icon.y
        void icon.parent
        if (parent === null) {
            return Qt.point(0, 0)
        }
        const scene = current_scene_origin()
        const ratio = effective_device_pixel_ratio
        return Qt.point(
            (Math.round(scene.x * ratio) - scene.x * ratio) / ratio,
            (Math.round(scene.y * ratio) - scene.y * ratio) / ratio)
    }

    // Where this icon sits inside the item it draws over, so the background
    // can be sampled one texel to one pixel. It follows the snap above, or the
    // background would be reproduced from where the quad is not.
    readonly property rect region_behind: {
        void icon.x;
        void icon.y;
        void icon.width;
        void icon.height;
        void icon.behind;
        void icon.device_origin_snap;
        return behind !== null
            ? mapToItem(
                behind,
                device_origin_snap.x,
                device_origin_snap.y,
                width,
                height)
            : Qt.rect(0, 0, 0, 0);
    }

    // An icon's artwork need not be square - sidebars.svg and treemap.svg are
    // 24 by 20 - and an Image renders an SVG at its own aspect, so its texture
    // is not device_extent * 3 in both axes the way the shader's source_extent
    // says it is. The shader would then map a shorter texture over the full
    // height of the quad and stretch the icon taller than the artwork. Padding
    // it into a square source of exactly that size keeps source_extent honest
    // and letterboxes the icon inside the quad, which is what an ordinary
    // Image does with the same fillMode.
    Item {
        id: supersampled_frame

        // Square, at the icon's own size: the ShaderEffectSource below renders
        // it at whatever resolution it asks for, so the frame only has to be
        // square for the letterboxing to be centred - not large. Keeping it at
        // the icon's size leaves the Image inside it the size the icon is,
        // which is what a caller measuring the named Image expects.
        width: icon.width
        height: icon.height
        visible: false

        Image {
            id: supersampled

            objectName: icon.icon_object_name
            anchors.fill: parent
            smooth: false
            source: icon.source
            // Three samples per device pixel in each axis let the shader
            // resolve channels along the selected horizontal or vertical LCD
            // axis and average the perpendicular axis back to the pixel rate.
            sourceSize.width: icon.device_extent * 3
            sourceSize.height: icon.device_extent * 3
            fillMode: Image.PreserveAspectFit
        }
    }

    ShaderEffectSource {
        id: supersampled_source

        visible: false
        live: true
        recursive: false
        smooth: false
        hideSource: false
        sourceItem: supersampled_frame
        textureSize: Qt.size(icon.device_extent * 3, icon.device_extent * 3)
    }

    ShaderEffectSource {
        id: behind_source

        visible: false
        live: true
        recursive: false
        smooth: false
        hideSource: false
        sourceItem: icon.behind
        sourceRect: icon.region_behind
        textureSize: Qt.size(icon.device_extent, icon.device_extent)
    }

    ShaderEffect {
        id: tinted

        x: icon.device_origin_snap.x
        y: icon.device_origin_snap.y
        width: icon.width
        height: icon.height
        visible: icon.behind !== null
        fragmentShader: "qrc:/vnm_qml_chrome/shaders/monochrome_icon.frag.qsb"

        property Item icon_source: supersampled_source
        property Item background_source: behind_source
        property color foreground_color: icon.tint
        property vector2d source_extent: Qt.vector2d(
            icon.device_extent * 3,
            icon.device_extent * 3)
        property vector2d output_extent: Qt.vector2d(
            icon.device_extent,
            icon.device_extent)
        property int lcd_subpixel_order: icon.lcd_subpixel_order
    }
}
