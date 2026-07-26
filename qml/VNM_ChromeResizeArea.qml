import QtQuick

MouseArea {
    id: control

    property int edges: 0
    property var resolve_edges: null

    signal resize_requested(int edges)

    acceptedButtons: Qt.LeftButton
    visible: enabled

    function resolved_edges(mouse) {
        return typeof resolve_edges === "function"
            ? resolve_edges(mouse)
            : edges
    }

    onPressed: (mouse) => {
        resize_requested(resolved_edges(mouse))
        mouse.accepted = true
    }
}
