import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.ToolTip {
    id: tooltip

    property VNM_ChromeTheme theme: VNM_ChromeTheme {}

    contentItem: Text {
        objectName: "chrome_tooltip_text"
        text: tooltip.text
        font: tooltip.font
        color: "white"
        wrapMode: Text.Wrap
    }

    background: Rectangle {
        objectName: "chrome_tooltip_background"
        color: tooltip.theme.round_button_background
        radius: 3
        border.width: 0
    }
}
