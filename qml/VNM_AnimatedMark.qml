import QtQuick

Item {
    id: mark

    property VNM_ChromeTheme theme: VNM_ChromeTheme {}
    property real mark_size: 20
    property bool move_enabled: false
    property bool alt_click_enabled: false
    property int move_drag_threshold: 2
    property bool alt_reveal_forced: false
    property bool pid_reveal_enabled: true
    property bool stay_on_top_enabled: true
    property bool stay_on_top_active: false
    // Reveal lifecycle: "" -> "forming" -> "elongating" -> "revealed"
    // -> "retracting" -> "".
    property string pid_phase: ""
    property bool pid_pill_growing: true
    readonly property bool pid_pill_active:
        pid_phase === "elongating"
        || pid_phase === "revealed"
        || pid_phase === "retracting"
    readonly property real pid_text_left_margin:
        (mark_size - hover_circle_radius_inset) / 2 + 2
    readonly property real pid_text_right_margin: 8
    readonly property real pid_caption_spacing: 4
    readonly property real pid_layout_width: pid_pill.x + pid_pill.width
    readonly property real pid_pill_target_width: Math.max(
        mark_size,
        Math.ceil(pid_caption_metrics.advanceWidth
                + pid_caption_spacing
                + pid_metrics.advanceWidth)
            + pid_text_left_margin
            + pid_text_right_margin)
    readonly property bool alt_hover_active:
        icon_hover.hovered && VNM_system_window.alt_modifier_active
    // Single pose arbiter. The pill lifecycle dominates. A latched eye remains
    // visible until another plain left click, including across Alt hover/title
    // editing. The hover circle additionally requires the Alt pose (including
    // its un-rotation tail) to be fully gone.
    readonly property string pose: {
        if (pid_phase !== "") {
            return "pill"
        }
        if (stay_on_top_active) {
            return "stay_on_top"
        }
        if (alt_reveal_forced || alt_hover_active) {
            return "alt"
        }
        if (icon_hover.hovered
            && Math.abs(icon_rotor.rotation) <= 0.01
            && (!icon_press_area.pressed
                || pid_reveal_enabled
                || stay_on_top_enabled)) {
            return "hover"
        }
        return "idle"
    }
    property bool hover_active: pose === "hover"
    property bool alt_reveal_active:
        pose === "alt" || Math.abs(icon_rotor.rotation) > 0.01
    readonly property real orange_scale: 290 / 193
    readonly property int alt_reveal_duration: 213
    readonly property real hover_circle_radius_inset: 0.5
    readonly property real hover_circle_x_offset: 0.5

    signal move_requested()
    signal alt_click_requested()
    signal stay_on_top_change_requested(bool active)

    function maybe_start_system_move(mouse) {
        if (!move_enabled
                || mark.pid_pill_active
                || icon_press_area.system_move_started
                || !(mouse.buttons & Qt.LeftButton)
                || (mouse.modifiers & Qt.AltModifier)) {
            return
        }

        const delta_x = mouse.x - icon_press_area.system_move_press_x
        const delta_y = mouse.y - icon_press_area.system_move_press_y
        if (delta_x * delta_x + delta_y * delta_y
                < move_drag_threshold * move_drag_threshold) {
            return
        }

        icon_press_area.system_move_started = true
        icon_press_area.stay_on_top_press_candidate = false
        mark.cancel_pid_reveal()
        move_requested()
        mouse.accepted = true
    }

    function handle_primary_press(mouse) {
        icon_press_area.stay_on_top_press_candidate = false

        if (mark.pid_phase !== ""
            && !(mouse.modifiers & Qt.AltModifier)) {
            // Ctrl+click toggles the revealed pill back to the bare mark.
            if (mouse.button === Qt.LeftButton
                && (mouse.modifiers & Qt.ControlModifier)
                && mark.pid_pill_active) {
                mark.request_pid_retract()
            }
            mouse.accepted = true
            return
        }

        if (mark.pid_phase !== ""
            && (mouse.modifiers & Qt.AltModifier)) {
            // Title editing owns the Alt click, but no pending PID phase may
            // survive long enough to take focus back from its editor.
            mark.dismiss_pid_reveal()
        }

        if (mark.alt_click_enabled
            && mouse.button === Qt.LeftButton
            && (mouse.modifiers & Qt.AltModifier)) {
            mark.alt_click_requested()
            mouse.accepted = true
            return
        }

        if (mouse.modifiers & Qt.AltModifier) {
            // Alt is reserved for the mark's own Alt behavior. When that
            // is disabled, swallow the press so it cannot fall through
            // to the title bar and start a move drag.
            mouse.accepted = true
            return
        }

        if (mouse.button !== Qt.LeftButton) {
            return
        }

        icon_press_area.system_move_press_x = mouse.x
        icon_press_area.system_move_press_y = mouse.y
        icon_press_area.system_move_started = false

        if (mouse.modifiers & Qt.ControlModifier) {
            // Keep the old press-driven reveal timing, but reserve it for
            // Ctrl+click. A subsequent move drag cancels the forming pill.
            mark.request_pid_reveal()
        }
        else if (mouse.modifiers === Qt.NoModifier) {
            icon_press_area.stay_on_top_press_candidate =
                mark.stay_on_top_enabled
        }

        mouse.accepted = true
    }

    function handle_primary_release(mouse) {
        const release_inside = mouse.x >= 0
            && mouse.x < icon_press_area.width
            && mouse.y >= 0
            && mouse.y < icon_press_area.height
        const toggle_stay_on_top =
            icon_press_area.stay_on_top_press_candidate
            && !icon_press_area.system_move_started
            && release_inside
            && mouse.button === Qt.LeftButton
            && mouse.modifiers === Qt.NoModifier

        icon_press_area.system_move_started = false

        if (toggle_stay_on_top) {
            mark.stay_on_top_change_requested(!mark.stay_on_top_active)
            mouse.accepted = true
        }
        icon_press_area.stay_on_top_press_candidate = false
    }

    function cancel_primary_press() {
        icon_press_area.system_move_started = false
        icon_press_area.stay_on_top_press_candidate = false
        mark.cancel_pid_reveal()
    }

    function handle_pill_press(mouse) {
        icon_press_area.stay_on_top_press_candidate = false
        if (mouse.button === Qt.LeftButton
            && (mouse.modifiers & Qt.ControlModifier)) {
            mark.request_pid_retract()
            mouse.accepted = true
            return
        }
        mouse.accepted = false
    }

    function circle_settled() {
        if (mark.state !== "normal_hover") {
            return false
        }

        const target_radius = orange_mark.width / 2
            - mark.hover_circle_radius_inset / mark.orange_scale
        return Math.abs(orange_mark.scale - mark.orange_scale) < 0.01
            && Math.abs(orange_mark.circle_x_offset - mark.hover_circle_x_offset) < 0.05
            && Math.abs(orange_mark.radius - target_radius) < 0.05
    }

    function request_pid_reveal() {
        if (!pid_reveal_enabled || pid_phase !== "" || alt_reveal_active) {
            return
        }

        pid_edit.previous_focus_item = null
        pid_phase = "forming"
        icon_press_area.stay_on_top_press_candidate = false
        pid_circle_settle_timer.start()
    }

    function cancel_pid_reveal() {
        if (pid_phase !== "forming") {
            return
        }

        pid_circle_settle_timer.stop()
        pid_edit.previous_focus_item = null
        pid_phase = ""
    }

    function dismiss_pid_reveal() {
        if (pid_phase === "forming") {
            cancel_pid_reveal()
            return
        }

        if (pid_phase === "elongating" || pid_phase === "revealed") {
            request_pid_retract()
        }
    }

    function begin_pid_elongation() {
        pid_pill.width = mark_size
        pid_edit.opacity = 0
        pid_phase = "elongating"
        pid_pill_growing = true
        animate_pid_pill_to_current_target()
        pid_edit_fade_animation.to = 1
        pid_edit_fade_animation.start()
    }

    function request_pid_retract() {
        if (pid_phase !== "revealed" && pid_phase !== "elongating") {
            return
        }

        pid_phase = "retracting"
        pid_pill_growing = false
        pid_edit_fade_animation.stop()
        animate_pid_pill_to_current_target()
        if (pid_phase === "retracting") {
            pid_edit_fade_animation.to = 0
            pid_edit_fade_animation.start()
        }
    }

    function animate_pid_pill_to_current_target() {
        if (pid_phase !== "elongating" && pid_phase !== "retracting") {
            return
        }

        const target_width = pid_phase === "elongating"
            ? pid_pill_target_width
            : mark_size
        pid_pill_grow_animation.retargeting = true
        pid_pill_grow_animation.stop()
        pid_pill_grow_animation.retargeting = false

        if (Math.abs(pid_pill.width - target_width) <= 0.5) {
            pid_pill.width = target_width
            settle_pid_pill_animation()
            return
        }

        pid_pill_grow_animation.to = target_width
        pid_pill_grow_animation.start()
    }

    function settle_pid_pill_animation() {
        if (pid_phase === "elongating" && pid_pill_growing) {
            pid_phase = "revealed"
            const active_focus_item = mark.Window.window
                ? mark.Window.window.activeFocusItem
                : null
            pid_edit.previous_focus_item = active_focus_item !== pid_edit
                ? active_focus_item
                : null
            pid_edit.forceActiveFocus()
            return
        }

        if (pid_phase === "retracting" && !pid_pill_growing) {
            const restore_previous_focus = pid_edit.activeFocus
            const previous_focus_item = pid_edit.previous_focus_item
            pid_edit_fade_animation.stop()
            pid_edit.focus = false
            pid_edit.previous_focus_item = null
            pid_edit.opacity = 0
            pid_phase = ""
            if (restore_previous_focus && previous_focus_item) {
                previous_focus_item.forceActiveFocus()
            }
        }
    }

    function on_pid_pill_animation_stopped() {
        if (pid_pill_grow_animation.retargeting
            || (pid_phase !== "elongating" && pid_phase !== "retracting")) {
            return
        }

        const target_width = pid_phase === "elongating"
            ? pid_pill_target_width
            : mark_size
        if (Math.abs(pid_pill.width - target_width) > 0.5) {
            animate_pid_pill_to_current_target()
            return
        }

        pid_pill.width = target_width
        settle_pid_pill_animation()
    }

    width: mark_size
    height: mark_size
    state: (hover_active
            || stay_on_top_active
            || icon_press_area.stay_on_top_press_candidate
            || pid_phase !== "")
        ? "normal_hover"
        : ""

    onPid_reveal_enabledChanged: {
        if (!pid_reveal_enabled) {
            dismiss_pid_reveal()
        }
    }

    onPid_pill_target_widthChanged: {
        if (pid_phase === "revealed") {
            pid_pill.width = pid_pill_target_width
        }
        else if (pid_phase === "elongating") {
            animate_pid_pill_to_current_target()
        }
    }

    onMark_sizeChanged: {
        if (pid_phase === "retracting") {
            animate_pid_pill_to_current_target()
        }
    }

    onStay_on_top_enabledChanged: {
        if (!stay_on_top_enabled) {
            icon_press_area.stay_on_top_press_candidate = false
        }
    }

    HoverHandler {
        id: icon_hover

        onHoveredChanged: {
            if (!hovered) {
                mark.cancel_pid_reveal()
            }
        }
    }

    Item {
        id: icon_clip

        x: 0
        y: -mark.mark_size
        width: mark.mark_size * 2
        height: mark.mark_size * 3
        clip: true

        Rectangle {
            id: icon_under

            x: 0
            y: mark.mark_size
            width: mark.mark_size
            height: mark.mark_size
            color: mark.theme.mark_underlay
            antialiasing: true
            opacity: mark.alt_reveal_active ? 1 : 0

            Behavior on opacity {
                NumberAnimation {
                    duration: 120
                    easing.type: Easing.OutQuad
                }
            }
        }

        Item {
            id: icon_rotor
            objectName: "vnm_mark_rotor"

            readonly property bool alt_pose_active:
                !mark.stay_on_top_active
                && (mark.alt_reveal_forced || mark.alt_hover_active)
                && mark.pid_phase === ""

            x: alt_pose_active ? -mark.mark_size * 0.245998 : 0
            y: mark.mark_size
            width: mark.mark_size
            height: mark.mark_size
            scale: alt_pose_active ? 1.055 : 1.0
            rotation: alt_pose_active ? 45 : 0
            transformOrigin: Item.Center

            Behavior on x {
                NumberAnimation {
                    duration: mark.alt_reveal_duration
                    easing.type: Easing.Linear
                }
            }

            Behavior on scale {
                NumberAnimation {
                    duration: mark.alt_reveal_duration
                    easing.type: Easing.Linear
                }
            }

            Behavior on rotation {
                NumberAnimation {
                    duration: mark.alt_reveal_duration
                    easing.type: Easing.Linear
                }
            }

            Item {
                id: normal_mark

                anchors.fill: parent
                readonly property bool animating:
                    Math.abs(orange_mark.scale - 1.0) > 0.01
                    || orange_mark.radius > 0.01
                    || Math.abs(orange_mark.circle_x_offset) > 0.01
                visible: (!mark.alt_reveal_active || animating) && !mark.pid_pill_active

                Rectangle {
                    id: grey_mark
                    objectName: "vnm_mark_grey"

                    anchors.fill: parent
                    color: mark.theme.mark_grey
                    antialiasing: true
                    opacity: 1
                }

                Rectangle {
                    id: orange_mark
                    objectName: "vnm_mark_orange"

                    property real circle_x_offset: 0

                    width: parent.width * 193 / 290
                    height: parent.height * 193 / 290
                    anchors.left: parent.left
                    anchors.leftMargin: circle_x_offset
                    anchors.bottom: parent.bottom
                    color: mark.theme.mark_orange
                    radius: 0
                    scale: 1
                    transformOrigin: Item.BottomLeft
                    antialiasing: true
                }
            }

            Item {
                id: alt_mark

                anchors.fill: parent
                visible: mark.alt_reveal_active && !normal_mark.animating

                Rectangle {
                    anchors.fill: parent
                    color: mark.theme.mark_grey
                    antialiasing: true
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    width: alt_mark.width * 193 / 290
                    height: alt_mark.height * 193 / 290
                    color: mark.theme.mark_orange
                    antialiasing: true
                }
            }
        }
    }

    MouseArea {
        id: icon_press_area
        objectName: "vnm_mark_press_area"

        property real system_move_press_x: 0
        property real system_move_press_y: 0
        property bool system_move_started: false
        property bool stay_on_top_press_candidate: false

        anchors.fill: parent
        acceptedButtons: Qt.LeftButton

        onPressed: (mouse) => mark.handle_primary_press(mouse)

        onPositionChanged: (mouse) => {
            mark.maybe_start_system_move(mouse)
        }

        onReleased: (mouse) => mark.handle_primary_release(mouse)
        onCanceled: mark.cancel_primary_press()

        onDoubleClicked: (mouse) => {
            if (mouse.button === Qt.LeftButton) {
                icon_press_area.stay_on_top_press_candidate = false
                mouse.accepted = true
            }
        }
    }

    Shortcut {
        sequence: "Escape"
        // Alt+click already retracts the PID before starting title editing;
        // the active title editor must retain its own Escape handling.
        enabled: mark.pid_phase !== "" && !mark.alt_reveal_forced
        context: Qt.WindowShortcut
        onActivated: mark.dismiss_pid_reveal()
    }

    Image {
        id: stay_on_top_eye
        objectName: "vnm_mark_stay_on_top_eye"

        width: mark.mark_size * 0.8
        height: width
        anchors.centerIn: parent
        z: 1
        source: "vnm_mark_eye.svg"
        fillMode: Image.PreserveAspectFit
        smooth: true
        // Rasterize the SVG at twice the rendered size; scaling the 640px
        // natural render down to mark size aliases badly.
        sourceSize.width: width * 2
        sourceSize.height: height * 2
        opacity: mark.stay_on_top_active && mark.pid_phase === "" ? 1 : 0
        enabled: false

        Behavior on opacity {
            NumberAnimation {
                duration: 180
                easing.type: Easing.InOutQuad
            }
        }
    }

    Rectangle {
        id: pid_pill
        objectName: "vnm_mark_pid_pill"

        x: mark.hover_circle_x_offset
        y: 0
        z: 2
        width: mark.mark_size
        height: mark.mark_size
        radius: (mark.mark_size - mark.hover_circle_radius_inset) / 2
        // Fixed brand orange; white text is the legible foreground on it.
        color: "#c44d28"
        antialiasing: true
        visible: mark.pid_pill_active

        // Holds the caption and the number and clips both while the pill is
        // still narrower than they are. It carries the geometry the number
        // alone used to have, so a pill at its bare mark_size is no wider than
        // it ever was.
        Item {
            id: pid_text_area

            anchors.left: parent.left
            anchors.leftMargin: mark.pid_text_left_margin
            anchors.right: parent.right
            anchors.rightMargin: mark.pid_text_right_margin
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            clip: true

            // Names what the number is. It is a caption, not payload: the
            // selection Ctrl+C copies stays the bare process ID.
            Text {
                id: pid_caption
                objectName: "vnm_mark_pid_caption"

                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: "PID:"
                color: "white"
                font: pid_edit.font
                opacity: pid_edit.opacity
            }

            TextInput {
                id: pid_edit
                objectName: "vnm_mark_pid_edit"

                property Item previous_focus_item: null

                anchors.left: pid_caption.right
                anchors.leftMargin: mark.pid_caption_spacing
                anchors.verticalCenter: parent.verticalCenter
                readOnly: true
                selectByMouse: true
                persistentSelection: true
                text: String(VNM_system_window.process_id)
                color: "white"
                selectionColor: mark.theme.titlebar
                selectedTextColor: "white"
                font.pointSize: 9.5
                opacity: 0

                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_C
                        && (event.modifiers & Qt.ControlModifier)) {
                        if (pid_edit.selectedText.length === 0) {
                            pid_edit.selectAll()
                        }
                        pid_edit.copy()
                        mark.request_pid_retract()
                        event.accepted = true
                    }
                }
            }
        }

        // Above the PID text so Ctrl+click retracts the pill from anywhere on
        // it. Other presses are refused and fall through to the text input,
        // keeping click-and-drag selection intact.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            propagateComposedEvents: true
            cursorShape: Qt.IBeamCursor
            onPressed: (mouse) => mark.handle_pill_press(mouse)
        }
    }

    TextMetrics {
        id: pid_caption_metrics

        font: pid_caption.font
        text: pid_caption.text
    }

    TextMetrics {
        id: pid_metrics

        font: pid_edit.font
        text: pid_edit.text
    }

    NumberAnimation {
        id: pid_pill_grow_animation

        property bool retargeting: false

        target: pid_pill
        property: "width"
        duration: 240
        easing.type: Easing.InOutQuad
        onStopped: mark.on_pid_pill_animation_stopped()
    }

    NumberAnimation {
        id: pid_edit_fade_animation

        target: pid_edit
        property: "opacity"
        duration: 240
        easing.type: Easing.InOutQuad
    }

    Timer {
        id: pid_circle_settle_timer

        interval: 30
        repeat: true
        onTriggered: {
            if (mark.pid_phase !== "forming") {
                stop()
                return
            }

            if (mark.circle_settled()) {
                stop()
                mark.begin_pid_elongation()
            }
        }
    }

    states: [
        State {
            name: "normal_hover"

            PropertyChanges {
                target: orange_mark
                circle_x_offset: mark.hover_circle_x_offset
                scale: mark.orange_scale
                radius: orange_mark.width / 2
                    - mark.hover_circle_radius_inset / mark.orange_scale
            }

            PropertyChanges {
                target: grey_mark
                opacity: 0
            }
        }
    ]

    transitions: [
        Transition {
            from: ""
            to: "normal_hover"

            ParallelAnimation {
                SequentialAnimation {
                    NumberAnimation {
                        target: orange_mark
                        property: "scale"
                        duration: 190
                        easing.type: Easing.InQuad
                    }

                    NumberAnimation {
                        target: orange_mark
                        property: "radius"
                        duration: 140
                        easing.type: Easing.OutQuad
                    }
                }

                NumberAnimation {
                    target: orange_mark
                    property: "circle_x_offset"
                    duration: 330
                    easing.type: Easing.InOutQuad
                }

                SequentialAnimation {
                    PauseAnimation {
                        duration: 190
                    }

                    PropertyAction {
                        target: grey_mark
                        property: "opacity"
                    }
                }
            }
        },

        Transition {
            from: "normal_hover"
            to: ""

            ParallelAnimation {
                SequentialAnimation {
                    NumberAnimation {
                        target: orange_mark
                        property: "radius"
                        duration: 140
                        easing.type: Easing.InQuad
                    }

                    NumberAnimation {
                        target: orange_mark
                        property: "scale"
                        duration: 190
                        easing.type: Easing.OutQuad
                    }
                }

                NumberAnimation {
                    target: orange_mark
                    property: "circle_x_offset"
                    duration: 330
                    easing.type: Easing.InOutQuad
                }

                SequentialAnimation {
                    PauseAnimation {
                        duration: 140
                    }

                    PropertyAction {
                        target: grey_mark
                        property: "opacity"
                    }
                }
            }
        }
    ]
}
