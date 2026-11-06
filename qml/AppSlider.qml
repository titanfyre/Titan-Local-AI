pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

Slider {
    id: root
    required property var theme
    implicitHeight: 30
    leftPadding: 2
    rightPadding: 2
    background: Rectangle {
        x: root.leftPadding
        y: root.topPadding + (root.availableHeight - height) / 2
        implicitHeight: 4
        width: root.availableWidth
        radius: 2
        color: root.theme.control
        Rectangle { width: root.visualPosition * parent.width; height: parent.height; radius: 2; color: root.theme.accent }
    }
    handle: Rectangle {
        x: root.leftPadding + root.visualPosition * (root.availableWidth - width)
        y: root.topPadding + (root.availableHeight - height) / 2
        implicitWidth: 16
        implicitHeight: 16
        radius: 8
        color: root.pressed ? root.theme.accentHover : root.theme.accent
        border.width: root.visualFocus ? 3 : 2
        border.color: root.visualFocus ? root.theme.text : root.theme.input
    }
}
