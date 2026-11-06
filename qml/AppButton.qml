pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: root
    required property var theme
    property string iconName: ""
    property string variant: "secondary"
    property string tip: ""
    property bool compact: false
    implicitHeight: compact ? 34 : 42
    implicitWidth: contentItem.implicitWidth + (compact ? 22 : 28)
    padding: 0
    horizontalPadding: compact ? 11 : 14
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    Accessible.name: text.length ? text : tip
    ToolTip.visible: hovered && tip.length > 0
    ToolTip.text: tip
    ToolTip.delay: 650
    opacity: enabled ? 1 : 0.42
    contentItem: RowLayout {
        spacing: 8
        Icon {
            visible: root.iconName.length > 0
            name: root.iconName
            color: root.variant === "primary" ? root.theme.accentText : root.variant === "danger" ? root.theme.danger : root.theme.text
            implicitWidth: 17
            implicitHeight: 17
            Layout.alignment: Qt.AlignVCenter
        }
        Text { textFormat: Text.PlainText;
            text: root.text
            visible: root.text.length > 0
            color: root.variant === "primary" ? root.theme.accentText : root.variant === "danger" ? root.theme.danger : root.theme.text
            font.family: root.theme.fontFamily
            font.pixelSize: 12
            font.weight: Font.DemiBold
            Layout.alignment: Qt.AlignVCenter
        }
    }
    background: Rectangle {
        radius: root.compact ? 9 : 11
        color: root.down ? (root.variant === "primary" ? root.theme.accentPressed : root.theme.pressed)
                         : root.hovered ? (root.variant === "primary" ? root.theme.accentHover : root.theme.hover)
                         : root.variant === "primary" ? root.theme.accent
                         : root.variant === "ghost" ? "transparent" : root.theme.control
        border.width: root.visualFocus ? 2 : root.variant === "ghost" || root.variant === "primary" ? 0 : 1
        border.color: root.visualFocus ? root.theme.accent : root.theme.border
        Behavior on color { ColorAnimation { duration: root.theme.motionDuration } }
    }
}
