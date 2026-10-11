import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic

Basic.RoundButton {
    id: control

    property VNM_ChromeTheme theme: VNM_ChromeTheme {}
    property string label: ""
    property string tooltip_text: ""

    width: 22
    height: 22
    padding: 0
    hoverEnabled: true
    VNM_ChromeToolTip {
        objectName: "round_button_tooltip"
        parent: control
        theme: control.theme
        delay: 350
        visible: control.hovered && control.tooltip_text.length > 0
        text: control.tooltip_text
    }

    background: Rectangle {
        radius: width / 2
        color: control.pressed
            ? control.theme.titlebar_button_pressed
            : control.theme.round_button_background
        border.width: 1
        border.color: control.theme.round_button_border
    }

    contentItem: Text {
        text: control.label
        font.pixelSize: 8
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: control.theme.round_button_text
    }
}
