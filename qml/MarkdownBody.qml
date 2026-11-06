pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts

Item {
    id: root
    required property var theme
    property string source: ""
    property bool streaming: false
    property color textColor: theme.text
    property int fontSize: 14
    implicitHeight: bodyLoader.implicitHeight

    function safeMarkdown(text) {
        return text.replace(/<img\b[^>]*>/gi, "")
                   .replace(/!\[([^\]]*)\](?:\([^)]*\)|\[[^\]]*\])?/g, "$1")
    }
    function blocks() {
        const result = []
        const lines = source.split("\n")
        let buffer = [], fence = "", language = ""
        function flush(kind) {
            const text = buffer.join("\n").replace(/\s+$/, "")
            if (text.trim().length) result.push({ kind: kind, text: kind === "code" ? text : safeMarkdown(text), language: language })
            buffer = []
        }
        for (let i = 0; i < lines.length; i++) {
            const match = /^ {0,3}(`{3,}|~{3,})(.*)$/.exec(lines[i])
            if (!fence && match) {
                flush("markdown")
                fence = match[1]
                language = match[2].trim().split(/\s+/)[0]
            } else if (fence && match && match[1][0] === fence[0] && match[1].length >= fence.length && !match[2].trim().length) {
                flush("code")
                fence = ""; language = ""
            } else buffer.push(lines[i])
        }
        flush(fence ? "code" : "markdown")
        return result
    }

    Loader {
        id: bodyLoader
        width: parent.width
        sourceComponent: root.streaming ? liveBody : completedBody
    }
    Component {
        id: liveBody
        TextEdit {
            width: root.width
            text: root.safeMarkdown(root.source)
            textFormat: TextEdit.MarkdownText
            readOnly: true; selectByMouse: true; persistentSelection: true
            wrapMode: TextEdit.Wrap
            color: root.textColor
            selectionColor: root.theme.accentSoft; selectedTextColor: root.theme.text
            font.family: root.theme.fontFamily; font.pixelSize: root.fontSize
            onLinkActivated: link => { if (/^https?:\/\//i.test(link)) Qt.openUrlExternally(link) }
        }
    }
    Component {
        id: completedBody
        Column {
            width: root.width
            spacing: 14
            Repeater {
                model: root.blocks()
                delegate: Loader {
                    id: blockLoader
                    required property var modelData
                    width: root.width
                    sourceComponent: modelData.kind === "code" ? codeBlock : markdownBlock
                    Component {
                        id: markdownBlock
                        TextEdit {
                            width: root.width
                            text: blockLoader.modelData.text
                            textFormat: TextEdit.MarkdownText
                            readOnly: true; selectByMouse: true; persistentSelection: true
                            wrapMode: TextEdit.Wrap
                            color: root.textColor
                            selectionColor: root.theme.accentSoft; selectedTextColor: root.theme.text
                            font.family: root.theme.fontFamily; font.pixelSize: root.fontSize
                            onLinkActivated: link => { if (/^https?:\/\//i.test(link)) Qt.openUrlExternally(link) }
                        }
                    }
                    Component {
                        id: codeBlock
                        Rectangle {
                            width: root.width
                            implicitHeight: codeHeader.height + codeText.implicitHeight + 28
                            radius: 11
                            color: root.theme.input
                            border.color: root.theme.border
                            Item {
                                id: codeHeader
                                width: parent.width; height: 38
                                RowLayout {
                                    anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 7
                                    spacing: 7
                                    Icon { name: "code"; implicitWidth: 13; implicitHeight: 13; color: root.theme.dim }
                                    Text { textFormat: Text.PlainText; text: blockLoader.modelData.language || "Code"; color: root.theme.muted; font.family: root.theme.monoFamily; font.pixelSize: 10 }
                                    Item { Layout.fillWidth: true }
                                    IconButton { theme: root.theme; iconName: "copy"; tip: "Copier le code"; implicitWidth: 26; implicitHeight: 26; onClicked: { codeText.selectAll(); codeText.copy(); codeText.deselect() } }
                                }
                                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: root.theme.border }
                            }
                            TextEdit {
                                id: codeText
                                x: 14; y: codeHeader.height + 12; width: parent.width - 28
                                text: blockLoader.modelData.text
                                textFormat: TextEdit.PlainText
                                readOnly: true; selectByMouse: true; persistentSelection: true
                                wrapMode: TextEdit.WrapAnywhere
                                color: root.theme.text
                                selectionColor: root.theme.accentSoft; selectedTextColor: root.theme.text
                                font.family: root.theme.monoFamily; font.pixelSize: 12
                                tabStopDistance: 28
                            }
                        }
                    }
                }
            }
        }
    }
}
