pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

ComboBox {
    id: root
    required property var theme
    property var choices: []
    property var value: ""
    signal picked(var selectedValue)
    model: choices
    textRole: "label"
    valueRole: "value"
    currentIndex: {
        for (let i = 0; i < choices.length; i++)
            if (choices[i].value === value) return i
        return -1
    }
    onActivated: index => picked(choices[index].value)
    implicitHeight: 42
    implicitWidth: 210
    leftPadding: 12
    rightPadding: 34
    font.family: root.theme.fontFamily
    font.pixelSize: 13
    Accessible.name: displayText
    contentItem: Text { textFormat: Text.PlainText;
        text: root.displayText
        color: root.enabled ? root.theme.text : root.theme.dim
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        font: root.font
    }
    indicator: Icon {
        x: root.width - width - 10
        y: (root.height-height)/2
        width: 16; height: 16
        name: "chevron"; color: root.theme.muted
    }
    background: Rectangle {
        radius: 9; color: root.hovered ? root.theme.hover : root.theme.input
        border.color: root.visualFocus ? root.theme.accent : root.theme.border
        opacity: root.enabled ? 1 : 0.5
    }
    delegate: ItemDelegate {
        id: choiceDelegate
        required property int index
        required property var modelData
        width: root.width
        height: 40
        highlighted: root.highlightedIndex === index
        contentItem: Text { textFormat: Text.PlainText;
            text: choiceDelegate.modelData.label
            color: root.theme.text
            font: root.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle { radius: 6; color: choiceDelegate.highlighted ? root.theme.hover : "transparent" }
    }
    popup: Popup {
        y: root.height + 6
        width: root.width
        padding: 5
        implicitHeight: Math.min(contentItem.implicitHeight + 10, 320)
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.popup.visible ? root.delegateModel : null
            currentIndex: root.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator { }
        }
        background: Rectangle { color: root.theme.raised; radius: 11; border.color: root.theme.border }
    }
}
