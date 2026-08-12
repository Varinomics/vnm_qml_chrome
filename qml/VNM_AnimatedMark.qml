import QtQuick

Item {
    id: mark

    property VNM_ChromeTheme theme: VNM_ChromeTheme {}
    property real mark_size: 20
    property bool move_enabled: false
    property bool alt_click_enabled: false
    property int move_drag_threshold: 2
    property bool alt_reveal_forced: false
    property bool hover_active:
        icon_hover.hovered && !alt_reveal_active && !icon_press_area.pressed
    property bool alt_reveal_active:
        !icon_press_area.pressed
        && (alt_reveal_forced
            || alt_hover.hovered
            || Math.abs(icon_rotor.rotation) > 0.01)
    readonly property real orange_scale: 290 / 193
    readonly property int alt_reveal_duration: 213
    readonly property real hover_circle_radius_inset: 0.5
    readonly property real hover_circle_x_offset: 0.5

    signal move_requested()
    signal maximize_toggle_requested()
    signal alt_click_requested()

    function maybe_start_system_move(mouse) {
        if (!move_enabled
                || icon_press_area.system_move_started
                || !(mouse.buttons & Qt.LeftButton)) {
            return
        }

        const delta_x = mouse.x - icon_press_area.system_move_press_x
        const delta_y = mouse.y - icon_press_area.system_move_press_y
        if (delta_x * delta_x + delta_y * delta_y
                < move_drag_threshold * move_drag_threshold) {
            return
        }

        icon_press_area.system_move_started = true
        move_requested()
        mouse.accepted = true
    }

    width: mark_size
    height: mark_size
    state: hover_active ? "normal_hover" : ""

    HoverHandler {
        id: icon_hover
    }

    HoverHandler {
        id: alt_hover
        acceptedModifiers: Qt.AltModifier
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
                mark.alt_reveal_forced || alt_hover.hovered

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
                visible: !mark.alt_reveal_active || animating

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

        property real system_move_press_x: 0
        property real system_move_press_y: 0
        property bool system_move_started: false

        anchors.fill: parent
        acceptedButtons: Qt.LeftButton

        onPressed: (mouse) => {
            if (mark.alt_click_enabled &&
                mouse.button === Qt.LeftButton &&
                (mouse.modifiers & Qt.AltModifier)) {
                mark.alt_click_requested()
                mouse.accepted = true
                return
            }

            if (mouse.button === Qt.LeftButton) {
                system_move_press_x = mouse.x
                system_move_press_y = mouse.y
                system_move_started = false
                mouse.accepted = true
            }
        }

        onPositionChanged: (mouse) => {
            mark.maybe_start_system_move(mouse)
        }

        onReleased: system_move_started = false
        onCanceled: system_move_started = false

        onDoubleClicked: (mouse) => {
            if (mark.move_enabled && mouse.button === Qt.LeftButton) {
                mark.maximize_toggle_requested()
                mouse.accepted = true
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
