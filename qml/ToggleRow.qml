pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var theme
    property string title: ""
    property string description: ""
    property bool checked: false
    signal toggled(bool value)
    implicitHeight: Math.max(48, labels.implicitHeight + 14)
    RowLayout {
        anchors.fill: parent
        spacing: 24
        ColumnLayout {
            id: labels
            Layout.fillWidth: true
            spacing: 5
            Text { textFormat: Text.PlainText; text: root.title; Layout.fillWidth: true; wrapMode: Text.WordWrap; font.family: root.theme.fontFamily; font.pixelSize: 13; font.weight: Font.Medium; color: root.theme.text }
            Text { textFormat: Text.PlainText; visible: text.length > 0; text: root.description; Layout.fillWidth: true; wrapMode: Text.WordWrap; font.family: root.theme.fontFamily; font.pixelSize: 11; color: root.theme.muted; lineHeight: 1.3 }
        }
        Switch {
            id: toggle
            checked: root.checked
            onToggled: root.toggled(checked)
            Accessible.name: root.title
            implicitWidth: 40
            implicitHeight: 28
            padding: 0
            indicator: Rectangle {
                width: 38; height: 22; radius: 11
                anchors.centerIn: parent
                color: toggle.checked ? root.theme.accent : root.theme.control
                border.color: toggle.visualFocus ? root.theme.accent : toggle.checked ? root.theme.accent : root.theme.border
                border.width: toggle.visualFocus ? 2 : 1
                Rectangle {
                    width: 16; height: 16; radius: 8; y: 3
                    x: toggle.checked ? 19 : 3
                    color: toggle.checked ? root.theme.accentText : root.theme.muted
                    Behavior on x { NumberAnimation { duration: root.theme.motionDuration; easing.type: Easing.OutCubic } }
                }
                Behavior on color { ColorAnimation { duration: root.theme.motionDuration } }
            }
        }
    }
}
