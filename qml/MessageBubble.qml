pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts

Item {
    id: root
    required property var theme
    required property string messageId
    required property string messageRole
    required property string messageText
    required property string reasoningText
    required property string messageState
    required property string toolLabel
    property bool reasoningOpen: false
    property bool copied: false
    readonly property bool isUser: messageRole === "user"
    readonly property bool isTool: messageRole === "tool"
    readonly property real messageWidth: Math.min(800, width - 68)
    implicitHeight: content.implicitHeight
    height: implicitHeight

    TextMetrics { id: userMetrics; font.family: root.theme.fontFamily; font.pixelSize: 14; text: root.messageText }

    Timer { id: copiedTimer; interval: 1800; onTriggered: root.copied = false }
    NumberAnimation { id: appear; target: root; property: "opacity"; from: 0; to: 1; duration: root.theme.reducedMotion ? 0 : 180 }
    Component.onCompleted: appear.start()
    ColumnLayout {
        id: content
        width: root.messageWidth
        x: (root.width - width)/2
        spacing: 10
        RowLayout {
            visible: !root.isUser
            Layout.fillWidth: true
            spacing: 9
            Rectangle {
                implicitWidth: 27; implicitHeight: 27; radius: 9
                color: root.isTool ? root.theme.control : root.theme.accentSoft
                Icon { anchors.centerIn: parent; width: 15; height: 15; name: root.isTool ? "code" : "spark"; color: root.isTool ? root.theme.muted : root.theme.accent }
            }
            Text { textFormat: Text.PlainText;
                text: root.isTool ? (root.toolLabel || "Activité de l’agent") : "Titan"
                font.family: root.theme.fontFamily; font.pixelSize: 12; font.weight: Font.DemiBold
                color: root.isTool ? root.theme.muted : root.theme.text
            }
            Text { textFormat: Text.PlainText; visible: root.isTool; text: root.messageState === "running" ? "EN COURS" : root.messageState === "error" ? "ÉCHEC" : "TERMINÉ"; color: root.messageState === "error" ? root.theme.danger : root.theme.dim; font.family: root.theme.fontFamily; font.pixelSize: 9; font.weight: Font.DemiBold; font.letterSpacing: 1 }
            Item { Layout.fillWidth: true }
            IconButton {
                theme: root.theme; iconName: root.copied ? "check" : "copy"
                tip: root.copied ? "Copié" : "Copier la réponse"
                visible: !root.isTool && root.messageText.length > 0 && root.messageState !== "streaming"
                implicitWidth: 28; implicitHeight: 28
                onClicked: { messageBody.selectAll(); messageBody.copy(); messageBody.deselect(); root.copied = true; copiedTimer.restart() }
            }
        }
        ColumnLayout {
            visible: !root.isUser && root.reasoningText.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: 36
            spacing: 8
            AppButton {
                theme: root.theme; variant: "ghost"; compact: true
                text: root.reasoningOpen ? "Masquer la réflexion" : "Afficher la réflexion"
                iconName: "chevron"
                onClicked: root.reasoningOpen = !root.reasoningOpen
            }
            Rectangle {
                visible: root.reasoningOpen
                Layout.fillWidth: true
                implicitHeight: reasoningBody.implicitHeight + 24
                radius: 10; color: root.theme.panel
                border.color: root.theme.border
                MarkdownBody {
                    id: reasoningBody
                    x: 14; y: 12; width: parent.width - 28
                    theme: root.theme
                    source: root.reasoningOpen ? root.reasoningText : ""
                    streaming: root.messageState === "streaming"
                    textColor: root.theme.muted
                    fontSize: 12
                }
            }
        }
        Rectangle {
            Layout.alignment: root.isUser ? Qt.AlignRight : Qt.AlignLeft
            Layout.fillWidth: !root.isUser
            Layout.leftMargin: root.isUser ? 48 : 36
            Layout.maximumWidth: root.isUser ? root.messageWidth * 0.86 : root.messageWidth
            implicitWidth: root.isUser ? Math.min(root.messageWidth * 0.86, Math.max(80, userMetrics.boundingRect.width + 36)) : root.messageWidth - 36
            implicitHeight: Math.max(root.isUser || root.isTool ? messageBody.implicitHeight : markdownBody.implicitHeight, root.messageState === "streaming" && !root.messageText.length ? 22 : 0) + (root.isUser || root.isTool ? 26 : 0)
            radius: root.isUser ? 17 : root.isTool ? 10 : 0
            color: root.isUser ? root.theme.userBubble : root.isTool ? root.theme.panel : "transparent"
            border.width: root.isTool ? 1 : 0
            border.color: root.messageState === "error" ? root.theme.danger : root.theme.border
            TextEdit {
                id: messageBody
                visible: root.isUser || root.isTool
                x: root.isUser || root.isTool ? 16 : 0
                y: root.isUser || root.isTool ? 12 : 0
                width: parent.width - (root.isUser || root.isTool ? 32 : 0)
                text: root.messageText
                textFormat: TextEdit.PlainText
                readOnly: true
                selectByMouse: true
                persistentSelection: true
                wrapMode: TextEdit.Wrap
                color: root.messageState === "error" ? root.theme.danger : root.isTool ? root.theme.muted : root.theme.text
                selectedTextColor: root.theme.text
                selectionColor: root.theme.accentSoft
                font.family: root.isTool ? root.theme.monoFamily : root.theme.fontFamily
                font.pixelSize: root.isTool ? 11 : 14
                textMargin: 0
                onLinkActivated: link => { if (/^https?:\/\//i.test(link)) Qt.openUrlExternally(link) }
            }
            MarkdownBody {
                id: markdownBody
                theme: root.theme
                visible: !root.isUser && !root.isTool
                width: parent.width
                source: visible ? root.messageText : ""
                streaming: root.messageState === "streaming"
                textColor: root.messageState === "error" ? root.theme.danger : root.theme.text
            }
            BusyDots { theme: root.theme; visible: root.messageState === "streaming" && !root.messageText.length; anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; running: visible }
        }
    }
}
