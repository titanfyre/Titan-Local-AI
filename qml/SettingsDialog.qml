pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    required property var theme
    required property var backend
    property int section: 0
    function setting(key, fallback) { return root.backend.settings[key] !== undefined ? root.backend.settings[key] : fallback }
    function save(key, value) { root.backend.updateSetting(key, value) }
    function thinkingChoices() {
        const labels = { auto: "Automatique", off: "Désactivée", on: "Activée", low: "Faible", medium: "Moyenne", high: "Élevée" }
        return setting("availableThinkingLevels", ["auto", "off"]).map(function(level) { return {label: labels[level] || level, value: level} })
    }
    anchors.centerIn: parent
    width: Math.min(940, parent.width - 64)
    height: Math.min(760, parent.height - 64)
    padding: 0
    modal: true
    dim: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    title: "Paramètres"
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: root.theme.motionDuration } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: root.theme.motionDuration } }
    background: Rectangle { color: root.theme.background; radius: 18; border.color: root.theme.border }
    Overlay.modal: Rectangle { color: root.theme.overlay }
    header: Item {
        implicitHeight: 77
        RowLayout {
            anchors.fill: parent; anchors.margins: 24
            Text { textFormat: Text.PlainText; text: "Paramètres"; font.family: root.theme.fontFamily; font.pixelSize: 21; font.weight: Font.DemiBold; color: root.theme.text }
            Item { Layout.fillWidth: true }
            Text { textFormat: Text.PlainText; text: "Enregistrés automatiquement"; font.family: root.theme.fontFamily; font.pixelSize: 11; color: root.theme.dim }
            IconButton { theme: root.theme; iconName: "close"; tip: "Fermer les paramètres"; onClicked: root.close() }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: root.theme.border }
    }
    contentItem: RowLayout {
        spacing: 0
        Item {
            Layout.preferredWidth: 196
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 15; spacing: 5
                Repeater {
                    model: [{title:"Général", icon:"sun"}, {title:"Modèles & moteur", icon:"cpu"}, {title:"Génération", icon:"spark"}, {title:"Agent & accès", icon:"shield"}]
                    delegate: Button {
                        id: navigation
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        implicitHeight: 43
                        leftPadding: 12; rightPadding: 12
                        onClicked: root.section = index
                        Accessible.name: modelData.title
                        contentItem: RowLayout {
                            spacing: 10
                            Icon { name: navigation.modelData.icon; implicitWidth: 16; implicitHeight: 16; color: root.section === navigation.index ? root.theme.accent : root.theme.muted }
                            Text { textFormat: Text.PlainText; text: navigation.modelData.title; Layout.fillWidth: true; color: root.section === navigation.index ? root.theme.text : root.theme.muted; font.family: root.theme.fontFamily; font.pixelSize: 12; font.weight: root.section === navigation.index ? Font.DemiBold : Font.Normal }
                        }
                        background: Rectangle { radius: 9; color: root.section === navigation.index ? root.theme.accentSoft : navigation.hovered ? root.theme.hover : "transparent" }
                    }
                }
                Item { Layout.fillHeight: true }
                Text { textFormat: Text.PlainText; text: "TITAN LOCAL AI\nVotre machine. Vos choix."; Layout.leftMargin: 12; lineHeight: 1.6; color: root.theme.dim; font.family: root.theme.fontFamily; font.pixelSize: 10; font.letterSpacing: 0.3 }
            }
        }
        Rectangle { Layout.fillHeight: true; implicitWidth: 1; color: root.theme.border }
        ScrollView {
            id: settingsScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                width: settingsScroll.availableWidth
                spacing: 22
                Item { implicitHeight: 2 }
                ColumnLayout {
                    Layout.fillWidth: true; Layout.leftMargin: 30; Layout.rightMargin: 30
                    spacing: 22
                    visible: root.section === 0
                    Text { textFormat: Text.PlainText; text: "Un espace à votre image"; color: root.theme.text; font.family: root.theme.fontFamily; font.pixelSize: 20; font.weight: Font.DemiBold }
                    Text { textFormat: Text.PlainText; text: "Les préférences sont conservées sur cet ordinateur."; color: root.theme.muted; font.family: root.theme.fontFamily; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 9
                        Text { textFormat: Text.PlainText; text: "Apparence"; color: root.theme.text; font.family: root.theme.fontFamily; font.pixelSize: 12; font.weight: Font.DemiBold }
                        Choice { theme: root.theme; Layout.fillWidth: true; value: root.setting("theme", "dark"); choices: [{label:"Sombre · Minuit",value:"dark"},{label:"Clair · Papier",value:"light"},{label:"Suivre le système",value:"system"}]; onPicked: value => root.save("theme",value) }
                    }
                    ToggleRow { theme: root.theme; Layout.fillWidth: true; title: "Réduire les animations"; description: "Une interface plus calme, avec des transitions instantanées."; checked: root.setting("reducedMotion",false); onToggled: value => root.save("reducedMotion",value) }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: root.theme.border }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 10
                        Text { textFormat: Text.PlainText; text: "Données locales"; color: root.theme.text; font.family: root.theme.fontFamily; font.pixelSize: 14; font.weight: Font.DemiBold }
                        Text { textFormat: Text.PlainText; text: "Vos conversations, vos réglages et vos autorisations restent enregistrés dans le dossier de données de Titan sur ce PC. Les modèles GGUF restent dans le dossier que vous avez choisi."; Layout.fillWidth: true; wrapMode: Text.WordWrap; lineHeight: 1.5; color: root.theme.muted; font.family: root.theme.fontFamily; font.pixelSize: 12 }
                        Text { textFormat: Text.PlainText; text: root.setting("provider","managed") === "managed" ? "Avec le moteur intégré, les réponses du modèle sont calculées sur cet ordinateur. Le navigateur de l’agent peut accéder à Internet lorsque vous l’utilisez." : "Les messages sont transmis au service local configuré. Ce service peut lui-même relayer les requêtes selon ses réglages. Le navigateur de l’agent peut accéder à Internet lorsque vous l’utilisez."; Layout.fillWidth: true; wrapMode: Text.WordWrap; lineHeight: 1.5; color: root.theme.muted; font.family: root.theme.fontFamily; font.pixelSize: 12 }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true; Layout.leftMargin: 30; Layout.rightMargin: 30
                    spacing: 21
                    visible: root.section === 1
                    Text { textFormat: Text.PlainText; text: "Le moteur de votre assistant"; color: root.theme.text; font.family: root.theme.fontFamily; font.pixelSize: 20; font.weight: Font.DemiBold }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 9
                        Text { textFormat: Text.PlainText; text: "Fournisseur de génération de texte"; color: root.theme.text; font.family: root.theme.fontFamily; font.pixelSize: 12; font.weight: Font.DemiBold }
                        Choice { theme: root.theme; Layout.fillWidth: true; value: root.setting("provider","managed"); choices: [{label:"Titan · llama.cpp intégré",value:"managed"},{label:"Ollama",value:"ollama"},{label:"LM Studio",value:"lmstudio"},{label:"Serveur compatible OpenAI",value:"custom"}]; onPicked: value => root.save("provider",value) }
                    }
                    ColumnLayout {
                        visible: root.setting("provider","managed") === "managed"
                        Layout.fillWidth: true; spacing: 12
                        Text { textFormat: Text.PlainText; text: "Dossier des modèles de texte GGUF"; color: root.theme.text; font.family: root.theme.fontFamily; font.pixelSize: 12; font.weight: Font.DemiBold }
                        Rectangle {
                            Layout.fillWidth: true; implicitHeight: modelsDirectory.implicitHeight + 26
                            color: root.theme.panel; radius: 10; border.color: root.theme.border
                            Text { textFormat: Text.PlainText; id: modelsDirectory; anchors.left: parent.left; anchors.right: parent.right; anchors.margins: 13; y: 13; text: root.backend.modelsPath || "Aucun dossier sélectionné"; color: root.theme.muted; font.family: root.theme.monoFamily; font.pixelSize: 11; wrapMode: Text.WrapAnywhere }
                        }
                        RowLayout {
                            spacing: 8
                            AppButton { theme: root.theme; text: "Choisir un dossier"; iconName: "folder"; compact: true; onClicked: root.backend.chooseModelsFolder() }
                            AppButton { theme: root.theme; text: "Actualiser"; iconName: "refresh"; variant: "ghost"; compact: true; onClicked: root.backend.refreshModels() }
                        }
                        Text { textFormat: Text.PlainText; text: "Les fichiers .gguf sont détectés automatiquement. Aucun modèle n’est téléchargé sans votre intervention."; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: root.theme.muted; font.family: root.theme.fontFamily; font.pixelSize: 11; lineHeight: 1.4 }
                        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: root.theme.border }
                        RowLayout {
                            Layout.fillWidth: true
                            Rectangle { implicitWidth: 7; implicitHeight: 7; radius: 4; color: root.backend.runtimeReady ? root.theme.accent : root.theme.warning }
                            Text { textFormat: Text.PlainText; text: root.backend.runtimeStatus || "Moteur en attente"; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: root.theme.muted; font.family: root.theme.fontFamily; font.pixelSize: 12 }
                        }
                        ProgressBar { visible: root.backend.downloadProgress >= 0 && root.backend.downloadProgress < 1; Layout.fillWidth: true; value: root.backend.downloadProgress; background: Rectangle { color: root.theme.control; radius: 3; implicitHeight: 5 } contentItem: Item { implicitHeight: 5; Rectangle { width: parent.width * root.backend.downloadProgress; height: 5; radius: 3; color: root.theme.accent } } }
                        RowLayout {
                            AppButton { theme: root.theme; text: "Installer le moteur"; iconName: "download"; compact: true; enabled: !root.backend.busy; onClicked: root.backend.installRuntime() }
                            AppButton { theme: root.theme; text: "Exécutable local…"; variant: "ghost"; compact: true; onClicked: root.backend.chooseRuntimeExecutable() }
                        }
                    }
                    ColumnLayout {
                        visible: root.setting("provider","managed") !== "managed"
                        Layout.fillWidth: true; spacing: 10
                        Text { textFormat: Text.PlainText; text: "Adresse du serveur"; color: root.theme.text; font.family: root.theme.fontFamily; font.pixelSize: 12; font.weight: Font.DemiBold }
                        AppField { theme: root.theme; Layout.fillWidth: true; placeholderText: "http://127.0.0.1:11434/v1"; text: root.setting("endpoint",""); Accessible.name: "Adresse du serveur d’inférence"; onEditingFinished: root.save("endpoint",text.trim()) }
                        Text { textFormat: Text.PlainText; text: "Identifiant du modèle"; color: root.theme.text; font.family: root.theme.fontFamily; font.pixelSize: 12; font.weight: Font.DemiBold }
                        AppField { theme: root.theme; Layout.fillWidth: true; placeholderText: "Nom du modèle exposé par le serveur"; text: root.setting("externalModel",""); Accessible.name: "Identifiant du modèle externe"; onEditingFinished: root.save("externalModel",text.trim()) }
                        AppButton { theme: root.theme; text: "Connecter le fournisseur"; iconName: "arrow"; variant: "primary"; onClicked: root.backend.connectProvider() }
                        Text { textFormat: Text.PlainText; text: "Titan utilise la génération de texte du service local. Vérifiez ses réglages si ce service relaie vos messages vers un autre fournisseur."; Layout.fillWidth: true; wrapMode: Text.WordWrap; lineHeight: 1.5; color: root.theme.muted; font.family: root.theme.fontFamily; font.pixelSize: 11 }
                    }
                    GridLayout {
                        columns: 2; Layout.fillWidth: true; columnSpacing: 16; rowSpacing: 8
                        Text { textFormat: Text.PlainText; text: "Threads CPU (0 : auto)"; color: root.theme.muted; font.family: root.theme.fontFamily; font.pixelSize: 11 }
                        Text { textFormat: Text.PlainText; text: "Couches GPU (−1 : auto)"; color: root.theme.muted; font.family: root.theme.fontFamily; font.pixelSize: 11 }
                        AppField { theme: root.theme; Layout.fillWidth: true; text: String(root.setting("threads",0)); validator: IntValidator { bottom: 0; top: 256 } inputMethodHints: Qt.ImhDigitsOnly; Accessible.name: "Threads CPU"; onEditingFinished: if (acceptableInput) root.save("threads",Number(text)) }
                        AppField { theme: root.theme; Layout.fillWidth: true; text: String(root.setting("gpuLayers",-1)); validator: IntValidator { bottom: -1; top: 999 } inputMethodHints: Qt.ImhFormattedNumbersOnly; Accessible.name: "Couches GPU"; onEditingFinished: if (acceptableInput) root.save("gpuLayers",Number(text)) }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true; Layout.leftMargin: 30; Layout.rightMargin: 30
                    spacing: 22
                    visible: root.section === 2
                    Text { textFormat: Text.PlainText; text: "Affinez les réponses"; color: root.theme.text; font.family: root.theme.fontFamily; font.pixelSize: 20; font.weight: Font.DemiBold }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 10
                        Text { textFormat: Text.PlainText; text: "Niveau de réflexion"; color: root.theme.text; font.family: root.theme.fontFamily; font.pixelSize: 12; font.weight: Font.DemiBold }
                        Choice { theme: root.theme; Layout.fillWidth: true; enabled: root.backend.thinkingSupported; value: root.setting("thinkingLevel","auto"); choices: root.thinkingChoices(); onPicked: value => root.save("thinkingLevel",value) }
                        Text { textFormat: Text.PlainText; text: root.backend.thinkingSupported ? "La disponibilité et l’effet de chaque niveau dépendent du modèle chargé." : "Le modèle actuel n’annonce pas de prise en charge de la réflexion."; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: root.theme.muted; font.family: root.theme.fontFamily; font.pixelSize: 11; lineHeight: 1.4 }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            Text { textFormat: Text.PlainText; text: "Température"; color: root.theme.text; font.family: root.theme.fontFamily; font.pixelSize: 12; font.weight: Font.DemiBold }
                            Item { Layout.fillWidth: true }
                            Text { textFormat: Text.PlainText; text: temperature.value.toFixed(2); color: root.theme.accent; font.family: root.theme.monoFamily; font.pixelSize: 12 }
                        }
                        AppSlider { id: temperature; theme: root.theme; Layout.fillWidth: true; from: 0; to: 2; stepSize: 0.05; value: root.setting("temperature",0.7); onMoved: root.save("temperature",Number(value.toFixed(2))); Accessible.name: "Température" }
                        Text { textFormat: Text.PlainText; text: "Précise à gauche, plus créative à droite."; color: root.theme.dim; font.family: root.theme.fontFamily; font.pixelSize: 11 }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            Text { textFormat: Text.PlainText; text: "Top P"; color: root.theme.text; font.family: root.theme.fontFamily; font.pixelSize: 12; font.weight: Font.DemiBold }
                            Item { Layout.fillWidth: true }
                            Text { textFormat: Text.PlainText; text: topP.value.toFixed(2); color: root.theme.accent; font.family: root.theme.monoFamily; font.pixelSize: 12 }
                        }
                        AppSlider { id: topP; theme: root.theme; Layout.fillWidth: true; from: 0.05; to: 1; stepSize: 0.05; value: root.setting("topP",0.9); onMoved: root.save("topP",Number(value.toFixed(2))); Accessible.name: "Top P" }
                    }
                    GridLayout {
                        columns: 2; Layout.fillWidth: true; columnSpacing: 16; rowSpacing: 9
                        Text { textFormat: Text.PlainText; text: "Fenêtre de contexte"; color: root.theme.muted; font.family: root.theme.fontFamily; font.pixelSize: 11 }
                        Text { textFormat: Text.PlainText; text: "Tokens de réponse max."; color: root.theme.muted; font.family: root.theme.fontFamily; font.pixelSize: 11 }
                        AppField { theme: root.theme; Layout.fillWidth: true; text: String(root.setting("contextSize",8192)); validator: IntValidator { bottom: 512; top: 1048576 } inputMethodHints: Qt.ImhDigitsOnly; Accessible.name: "Fenêtre de contexte"; onEditingFinished: if (acceptableInput) root.save("contextSize",Number(text)) }
                        AppField { theme: root.theme; Layout.fillWidth: true; text: String(root.setting("maxTokens",4096)); validator: IntValidator { bottom: 1; top: 1048576 } inputMethodHints: Qt.ImhDigitsOnly; Accessible.name: "Nombre maximal de tokens de réponse"; onEditingFinished: if (acceptableInput) root.save("maxTokens",Number(text)) }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 10
                        Text { textFormat: Text.PlainText; text: "Instructions système"; color: root.theme.text; font.family: root.theme.fontFamily; font.pixelSize: 12; font.weight: Font.DemiBold }
                        TextArea {
                            id: systemPrompt
                            Layout.fillWidth: true; Layout.preferredHeight: 132
                            text: root.setting("systemPrompt","")
                            placeholderText: "Précisez le ton, la langue ou les préférences de votre assistant…"
                            wrapMode: TextEdit.Wrap; selectByMouse: true
                            color: root.theme.text; placeholderTextColor: root.theme.dim; selectionColor: root.theme.accentSoft
                            font.family: root.theme.fontFamily; font.pixelSize: 12
                            padding: 12
                            Accessible.name: "Instructions système"
                            onActiveFocusChanged: if (!activeFocus) root.save("systemPrompt",text)
                            background: Rectangle { color: root.theme.input; radius: 10; border.color: systemPrompt.activeFocus ? root.theme.accent : root.theme.border }
                        }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true; Layout.leftMargin: 30; Layout.rightMargin: 30
                    spacing: 22
                    visible: root.section === 3
                    Text { textFormat: Text.PlainText; text: "Gardez la main sur l’agent"; color: root.theme.text; font.family: root.theme.fontFamily; font.pixelSize: 20; font.weight: Font.DemiBold }
                    ToggleRow { theme: root.theme; Layout.fillWidth: true; title: "Confirmer les actions sensibles"; description: "Afficher un aperçu avant une écriture de fichier ou une interaction avec le navigateur."; checked: root.setting("approvalsEnabled",true); onToggled: value => root.save("approvalsEnabled",value) }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 10
                        Text { textFormat: Text.PlainText; text: "Mode des outils"; color: root.theme.text; font.family: root.theme.fontFamily; font.pixelSize: 12; font.weight: Font.DemiBold }
                        Choice { theme: root.theme; Layout.fillWidth: true; value: root.setting("toolMode","auto"); choices: [{label:"Automatique · Selon le modèle",value:"auto"},{label:"Appels natifs",value:"native"},{label:"Repli JSON",value:"json"},{label:"Désactiver les outils",value:"off"}]; onPicked: value => root.save("toolMode",value) }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 22
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 5
                            Text { textFormat: Text.PlainText; text: "Étapes maximales par demande"; color: root.theme.text; font.family: root.theme.fontFamily; font.pixelSize: 12; font.weight: Font.DemiBold }
                            Text { textFormat: Text.PlainText; text: "Limite la durée des séquences d’actions autonomes."; color: root.theme.muted; font.family: root.theme.fontFamily; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        }
                        AppField { theme: root.theme; Layout.preferredWidth: 80; text: String(root.setting("maxSteps",12)); validator: IntValidator { bottom: 1; top: 100 } inputMethodHints: Qt.ImhDigitsOnly; Accessible.name: "Nombre maximal d’étapes"; onEditingFinished: if (acceptableInput) root.save("maxSteps",Number(text)) }
                    }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: root.theme.border }
                    RowLayout {
                        Layout.fillWidth: true
                        Text { textFormat: Text.PlainText; text: "Dossiers autorisés"; color: root.theme.text; font.family: root.theme.fontFamily; font.pixelSize: 14; font.weight: Font.DemiBold; Layout.fillWidth: true }
                        AppButton { theme: root.theme; text: "Ajouter"; iconName: "plus"; compact: true; onClicked: root.backend.chooseGrantFolder() }
                    }
                    Text { textFormat: Text.PlainText; visible: root.backend.folderGrants.length === 0; text: "Aucun dossier partagé. Ajoutez un dossier pour que l’agent puisse y lire et rechercher des fichiers."; Layout.fillWidth: true; wrapMode: Text.WordWrap; lineHeight: 1.5; color: root.theme.muted; font.family: root.theme.fontFamily; font.pixelSize: 12 }
                    Repeater {
                        model: root.backend.folderGrants
                        delegate: Rectangle {
                            id: permissionCard
                            required property var modelData
                            Layout.fillWidth: true; implicitHeight: permissionContents.implicitHeight + 24
                            color: root.theme.panel; radius: 10; border.color: root.theme.border
                            ColumnLayout {
                                id: permissionContents
                                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 12; spacing: 6
                                RowLayout {
                                    Layout.fillWidth: true
                                    Icon { name: "folder"; color: root.theme.accent; implicitWidth: 16; implicitHeight: 16 }
                                    Text { textFormat: Text.PlainText; text: permissionCard.modelData.path; Layout.fillWidth: true; elide: Text.ElideMiddle; color: root.theme.text; font.family: root.theme.fontFamily; font.pixelSize: 11 }
                                    IconButton { theme: root.theme; iconName: "trash"; tip: "Retirer l’accès à ce dossier"; implicitWidth: 28; implicitHeight: 28; onClicked: root.backend.removeGrant(permissionCard.modelData.path) }
                                }
                                ToggleRow { theme: root.theme; Layout.fillWidth: true; title: "Autoriser aussi l’écriture"; checked: permissionCard.modelData.writable; onToggled: value => root.backend.setGrantWritable(permissionCard.modelData.path,value) }
                            }
                        }
                    }
                    Text { textFormat: Text.PlainText; text: "L’agent accède uniquement aux dossiers autorisés. Les autorisations sont toujours modifiées depuis cette interface."; Layout.fillWidth: true; wrapMode: Text.WordWrap; lineHeight: 1.5; color: root.theme.dim; font.family: root.theme.fontFamily; font.pixelSize: 11 }
                }
                Item { implicitHeight: 20 }
            }
        }
    }
}
