pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

Button {
    id: root
    required property var theme
    property string iconName: "plus"
    property string tip: ""
    property bool selected: false
    property color iconColor: selected ? root.theme.accent : root.theme.muted
    implicitWidth: 34
    implicitHeight: 34
    padding: Math.max(3, (Math.min(width, height) - 18) / 2)
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    Accessible.name: tip
    ToolTip.visible: hovered && tip.length > 0
    ToolTip.text: tip
    ToolTip.delay: 650
    contentItem: Icon { name: root.iconName; color: root.enabled ? root.iconColor : root.theme.dim }
    background: Rectangle {
        radius: 8
        color: root.down ? root.theme.pressed : root.selected ? root.theme.accentSoft : root.hovered ? root.theme.hover : "transparent"
        border.width: root.visualFocus ? 2 : 0
        border.color: root.theme.accent
        Behavior on color { ColorAnimation { duration: root.theme.motionDuration } }
    }
}
