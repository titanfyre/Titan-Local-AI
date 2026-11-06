pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

TextField {
    id: root
    required property var theme
    implicitHeight: 42
    color: root.theme.text
    placeholderTextColor: root.theme.dim
    selectionColor: root.theme.accentSoft
    selectedTextColor: root.theme.text
    font.family: root.theme.fontFamily
    font.pixelSize: 13
    leftPadding: 12
    rightPadding: 12
    selectByMouse: true
    background: Rectangle {
        radius: 9
        color: root.theme.input
        border.color: root.activeFocus ? root.theme.accent : root.theme.border
        border.width: 1
        Behavior on border.color { ColorAnimation { duration: root.theme.motionDuration } }
    }
}
