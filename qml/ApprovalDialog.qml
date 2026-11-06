pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    required property var theme
    required property var backend
    readonly property var approval: root.backend.pendingApproval || ({})
    readonly property bool hasApproval: Object.keys(approval).length > 0
    onHasApprovalChanged: { if (hasApproval) open(); else close() }
    Component.onCompleted: if (hasApproval) open()
    anchors.centerIn: parent
    width: Math.min(660, parent.width - 64)
    height: Math.min(600, parent.height - 80)
    padding: 26
    modal: true
    focus: true
    closePolicy: Popup.NoAutoClose
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: root.theme.motionDuration } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: root.theme.motionDuration } }
    background: Rectangle { radius: 18; color: root.theme.raised; border.color: root.theme.border }
    Overlay.modal: Rectangle { color: root.theme.overlay }
    contentItem: ColumnLayout {
        spacing: 18
        Rectangle {
            implicitWidth: 43; implicitHeight: 43; radius: 13; color: root.theme.accentSoft
            Icon { anchors.centerIn: parent; name: "shield"; width: 24; height: 24; color: root.theme.accent }
        }
        Text { textFormat: Text.PlainText; text: "À vous de donner le feu vert"; color: root.theme.text; font.family: root.theme.fontFamily; font.pixelSize: 23; font.weight: Font.DemiBold; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        Text { textFormat: Text.PlainText; text: String(root.approval.summary || "L’agent demande votre accord pour cette action."); color: root.theme.muted; font.family: root.theme.fontFamily; font.pixelSize: 13; Layout.fillWidth: true; wrapMode: Text.WordWrap; lineHeight: 1.5 }
        RowLayout {
            spacing: 7
            Icon { name: "code"; implicitWidth: 14; implicitHeight: 14; color: root.theme.accent }
            Text { textFormat: Text.PlainText; text: String(root.approval.tool || "Action de l’agent"); color: root.theme.accent; font.family: root.theme.monoFamily; font.pixelSize: 11 }
        }
        ScrollView {
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            background: Rectangle { color: root.theme.input; radius: 10; border.color: root.theme.border }
            TextArea {
                text: typeof root.approval.preview === "string" ? root.approval.preview : JSON.stringify(root.approval.preview || {}, null, 2)
                readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere
                textFormat: TextEdit.PlainText
                color: root.theme.text; selectionColor: root.theme.accentSoft
                font.family: root.theme.monoFamily; font.pixelSize: 11
                padding: 15
                Accessible.name: "Aperçu de l’action à approuver"
            }
        }
        Text { textFormat: Text.PlainText; text: "Cette autorisation vaut uniquement pour l’action affichée."; color: root.theme.dim; font.family: root.theme.fontFamily; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            AppButton { theme: root.theme; text: "Refuser"; onClicked: root.backend.answerApproval(false) }
            AppButton { theme: root.theme; text: "Autoriser l’action"; iconName: "check"; variant: "primary"; onClicked: root.backend.answerApproval(true) }
        }
    }
}
