pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: root
    required property var theme
    required property var backend
    signal configureProvider()
    readonly property var selectedModel: {
        for (let i = 0; i < root.backend.models.length; i++)
            if (root.backend.models[i].id === root.backend.selectedModelId) return root.backend.models[i]
        return null
    }
    readonly property bool managed: (root.backend.settings.provider || "managed") === "managed"
    implicitWidth: Math.min(320, contentItem.implicitWidth + 20)
    implicitHeight: 43
    padding: 10
    hoverEnabled: true
    Accessible.name: "Sélectionner le modèle"
    onClicked: { if (managed) menu.open(); else configureProvider() }
    contentItem: RowLayout {
        spacing: 10
        ColumnLayout {
            spacing: 3
            Text { textFormat: Text.PlainText;
                Layout.maximumWidth: 240
                text: root.managed ? root.selectedModel ? root.selectedModel.name : "Choisir un modèle" : (root.backend.settings.externalModel || "Modèle du service")
                elide: Text.ElideRight
                color: root.theme.text; font.family: root.theme.fontFamily; font.pixelSize: 14; font.weight: Font.DemiBold
            }
            Text { textFormat: Text.PlainText;
                text: root.managed ? root.selectedModel ? [root.selectedModel.quantization, root.selectedModel.sizeLabel].filter(Boolean).join("  ·  ") : "GGUF · sur votre ordinateur" : String(root.backend.settings.provider || "Serveur compatible")
                color: root.theme.dim; font.family: root.theme.fontFamily; font.pixelSize: 10
            }
        }
        Icon { implicitWidth: 15; implicitHeight: 15; name: "chevron"; color: root.theme.muted }
    }
    background: Rectangle { radius: 10; color: root.hovered ? root.theme.hover : "transparent"; border.width: root.visualFocus ? 1 : 0; border.color: root.theme.accent }
    Popup {
        id: menu
        y: root.height + 8
        width: 390
        padding: 8
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { radius: 13; color: root.theme.raised; border.color: root.theme.border }
        contentItem: ColumnLayout {
            spacing: 5
            RowLayout {
                Layout.fillWidth: true; Layout.margins: 9
                Text { textFormat: Text.PlainText; text: "MODÈLES SUR CET APPAREIL"; color: root.theme.dim; font.family: root.theme.fontFamily; font.pixelSize: 9; font.weight: Font.DemiBold; font.letterSpacing: 1 }
                Item { Layout.fillWidth: true }
                IconButton { theme: root.theme; iconName: "refresh"; tip: "Rechercher les modèles"; implicitWidth: 25; implicitHeight: 25; onClicked: root.backend.refreshModels() }
            }
            ListView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(360, contentHeight)
                clip: true
                model: root.backend.models
                spacing: 4
                delegate: ItemDelegate {
                    id: modelItem
                    required property var modelData
                    width: ListView.view.width
                    height: modelData.warning ? 89 : 65
                    enabled: modelData.loadable !== false && !root.backend.busy
                    opacity: enabled ? 1 : 0.5
                    onClicked: { root.backend.selectModel(modelData.id); menu.close() }
                    Accessible.name: modelData.name + (modelData.warning ? ". " + modelData.warning : "")
                    contentItem: RowLayout {
                        spacing: 11
                        Rectangle {
                            implicitWidth: 34; implicitHeight: 34; radius: 10
                            color: root.theme.accentSoft
                            Icon { anchors.centerIn: parent; width: 18; height: 18; name: "cpu"; color: root.theme.accent }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 5
                            Text { textFormat: Text.PlainText; text: modelItem.modelData.name; Layout.fillWidth: true; elide: Text.ElideRight; color: root.theme.text; font.family: root.theme.fontFamily; font.pixelSize: 12; font.weight: Font.DemiBold }
                            Text { textFormat: Text.PlainText; text: [modelItem.modelData.quantization, modelItem.modelData.sizeLabel, modelItem.modelData.supportsThinking ? "Réflexion" : ""].filter(Boolean).join(" · "); color: root.theme.muted; font.family: root.theme.fontFamily; font.pixelSize: 10 }
                            Text { textFormat: Text.PlainText; visible: !!modelItem.modelData.warning; text: modelItem.modelData.warning || ""; Layout.fillWidth: true; elide: Text.ElideRight; color: root.theme.warning; font.family: root.theme.fontFamily; font.pixelSize: 10 }
                        }
                        Icon { name: "check"; implicitWidth: 17; implicitHeight: 17; color: root.theme.accent; visible: modelItem.modelData.id === root.backend.selectedModelId }
                    }
                    background: Rectangle { radius: 8; color: modelItem.hovered ? root.theme.hover : modelItem.modelData.id === root.backend.selectedModelId ? root.theme.accentSoft : "transparent" }
                }
                ScrollBar.vertical: ScrollBar { }
            }
            Text { textFormat: Text.PlainText; visible: root.backend.models.length === 0; text: "Aucun modèle GGUF détecté.\nChoisissez le dossier qui contient vos modèles."; Layout.margins: 14; Layout.fillWidth: true; wrapMode: Text.WordWrap; lineHeight: 1.5; color: root.theme.muted; font.family: root.theme.fontFamily; font.pixelSize: 12 }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: root.theme.border }
            AppButton { theme: root.theme; text: "Choisir le dossier des modèles"; iconName: "folder"; variant: "ghost"; Layout.fillWidth: true; onClicked: { root.backend.chooseModelsFolder(); menu.close() } }
        }
    }
}
