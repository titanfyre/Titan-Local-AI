pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: window
    width: 1500
    height: 940
    minimumWidth: 960
    minimumHeight: 660
    visible: true
    title: "Titan Local AI"
    color: ui.background
    font.family: ui.fontFamily
    font.pixelSize: 13
    palette.window: ui.background
    palette.windowText: ui.text
    palette.text: ui.text
    palette.base: ui.input
    palette.button: ui.control
    palette.buttonText: ui.text
    palette.highlight: ui.accent
    palette.highlightedText: ui.accentText
    property bool sidebarVisible: true
    property bool workspaceVisible: true
    property string selectedSessionId: ""
    property string conversationTitle: "Nouvelle conversation"
    property bool followConversation: true
    readonly property bool managedProvider: setting("provider", "managed") === "managed"
    readonly property bool hasApproval: Object.keys(app.pendingApproval || {}).length > 0
    readonly property bool canSend: !app.busy && app.runtimeReady && !hasApproval && (!managedProvider || app.selectedModelId.length > 0)
    readonly property var filteredSessions: {
        const query = historySearch.text.trim().toLocaleLowerCase()
        return app.sessions.filter(function(session) { return !query.length || String(session.title).toLocaleLowerCase().indexOf(query) >= 0 })
    }

    function setting(key, fallback) { return app.settings[key] !== undefined ? app.settings[key] : fallback }
    function thinkingChoices() {
        const labels = { auto: "Réflexion auto", off: "Sans réflexion", on: "Réflexion activée", low: "Réflexion faible", medium: "Réflexion moyenne", high: "Réflexion élevée" }
        return setting("availableThinkingLevels", ["auto", "off"]).map(function(level) { return {label: labels[level] || level, value: level} })
    }
    function submitPrompt() {
        const prompt = composer.text.trim()
        if (!prompt.length || !canSend) return
        followConversation = true
        app.sendPrompt(prompt)
        composer.clear()
        composer.forceActiveFocus()
        scrollToEnd.restart()
    }
    function newConversation() {
        if (app.busy) return
        app.newChat()
        selectedSessionId = ""
        conversationTitle = "Nouvelle conversation"
        followConversation = true
        composer.clear()
        composer.forceActiveFocus()
    }
    function suggest(text) { composer.text = text; composer.forceActiveFocus(); composer.cursorPosition = composer.length }
    function openSettings(section) { settings.section = section; settings.open() }

    QtObject {
        id: ui
        readonly property bool dark: window.setting("theme", "dark") === "system" ? Qt.styleHints.colorScheme === Qt.Dark : window.setting("theme", "dark") !== "light"
        readonly property bool reducedMotion: window.setting("reducedMotion", false)
        readonly property int motionDuration: reducedMotion ? 0 : 140
        readonly property string fontFamily: "Segoe UI"
        readonly property string monoFamily: "Cascadia Code"
        readonly property color background: dark ? "#10161f" : "#f8fafb"
        readonly property color sidebar: dark ? "#0c121a" : "#eef3f5"
        readonly property color panel: dark ? "#151e29" : "#eff4f6"
        readonly property color raised: dark ? "#1a2531" : "#ffffff"
        readonly property color control: dark ? "#202c39" : "#e8eef2"
        readonly property color input: dark ? "#121b25" : "#ffffff"
        readonly property color hover: dark ? "#263441" : "#e2ebef"
        readonly property color pressed: dark ? "#304352" : "#d5e2e8"
        readonly property color userBubble: dark ? "#253540" : "#e0eee9"
        readonly property color border: dark ? "#273340" : "#dce5e9"
        readonly property color text: dark ? "#e8edf3" : "#1b2d38"
        readonly property color muted: dark ? "#97a7ba" : "#607583"
        readonly property color dim: dark ? "#60758b" : "#81929d"
        readonly property color accent: dark ? "#81e6c5" : "#087f68"
        readonly property color accentHover: dark ? "#a4f4d9" : "#0c947a"
        readonly property color accentPressed: dark ? "#64cfae" : "#056650"
        readonly property color accentSoft: dark ? "#1b3936" : "#dcefe8"
        readonly property color accentText: dark ? "#102b25" : "#ffffff"
        readonly property color danger: dark ? "#f3a4a4" : "#bf4555"
        readonly property color warning: dark ? "#e6c48b" : "#97702b"
        readonly property color overlay: dark ? "#ad050b12" : "#7742515a"
    }

    Shortcut { sequence: "Ctrl+N"; enabled: !app.busy; onActivated: window.newConversation() }
    Shortcut { sequence: "Ctrl+,"; onActivated: window.openSettings(0) }
    Shortcut { sequence: "Ctrl+L"; onActivated: composer.forceActiveFocus() }
    Shortcut { sequence: "Escape"; enabled: app.busy && !window.hasApproval; onActivated: app.stop() }

    Timer { id: scrollToEnd; interval: 24; onTriggered: if (window.followConversation) conversation.positionViewAtEnd() }
    Connections {
        target: app.messages
        function onRowsInserted() { if (window.followConversation) scrollToEnd.restart() }
        function onDataChanged() { if (window.followConversation) scrollToEnd.restart() }
        function onModelReset() { window.followConversation = true; scrollToEnd.restart() }
    }

    SplitView {
        id: mainSurface
        anchors.fill: parent
        orientation: Qt.Horizontal
        handle: Rectangle {
            id: splitHandle
            implicitWidth: 1
            implicitHeight: 1
            color: SplitHandle.pressed ? ui.accent : SplitHandle.hovered ? ui.dim : ui.border
            containmentMask: Item { x: -3; width: 7; height: splitHandle.height }
        }

        Rectangle {
            id: sidebar
            visible: window.sidebarVisible
            SplitView.preferredWidth: 262
            SplitView.minimumWidth: 220
            SplitView.maximumWidth: 350
            color: ui.sidebar
            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 17; anchors.rightMargin: 17; anchors.topMargin: 24; anchors.bottomMargin: 15
                spacing: 17
                RowLayout {
                    Layout.fillWidth: true; Layout.leftMargin: 6; Layout.bottomMargin: 12
                    spacing: 11
                    Rectangle {
                        implicitWidth: 33; implicitHeight: 35; radius: 11; color: ui.accent
                        Text { textFormat: Text.PlainText; anchors.centerIn: parent; anchors.verticalCenterOffset: -1; text: "t"; font.family: ui.fontFamily; font.pixelSize: 31; font.weight: Font.Bold; color: ui.accentText }
                    }
                    Text { textFormat: Text.PlainText; text: "titan"; color: ui.text; font.family: ui.fontFamily; font.pixelSize: 27; font.weight: Font.DemiBold; font.letterSpacing: -1.2 }
                    Rectangle {
                        implicitWidth: 39; implicitHeight: 17; radius: 5; color: ui.accentSoft
                        Text { textFormat: Text.PlainText; anchors.centerIn: parent; text: "LOCAL"; color: ui.accent; font.family: ui.fontFamily; font.pixelSize: 8; font.weight: Font.Bold; font.letterSpacing: 0.8 }
                    }
                    Item { Layout.fillWidth: true }
                }
                AppButton { theme: ui; Layout.fillWidth: true; text: "Nouvelle conversation"; iconName: "plus"; variant: "primary"; tip: "Nouvelle conversation · Ctrl+N"; enabled: !app.busy; onClicked: window.newConversation() }
                AppField {
                    id: historySearch
                    theme: ui
                    Layout.fillWidth: true
                    implicitHeight: 37
                    leftPadding: 34
                    placeholderText: "Rechercher une conversation"
                    font.pixelSize: 11
                    Accessible.name: "Rechercher dans l’historique"
                    Icon { x: 11; anchors.verticalCenter: parent.verticalCenter; width: 14; height: 14; name: "search"; color: ui.dim }
                }
                RowLayout {
                    Layout.fillWidth: true; Layout.leftMargin: 8; Layout.rightMargin: 8
                    Text { textFormat: Text.PlainText; text: "CONVERSATIONS"; color: ui.dim; font.family: ui.fontFamily; font.pixelSize: 9; font.weight: Font.DemiBold; font.letterSpacing: 1.2 }
                    Item { Layout.fillWidth: true }
                    Text { textFormat: Text.PlainText; text: app.sessions.length; color: ui.dim; font.family: ui.monoFamily; font.pixelSize: 10 }
                }
                ListView {
                    id: history
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 5
                    model: window.filteredSessions
                    ScrollBar.vertical: ScrollBar { width: 4 }
                    delegate: Button {
                        id: sessionButton
                        required property var modelData
                        width: ListView.view.width
                        height: 43
                        enabled: !app.busy
                        leftPadding: 10; rightPadding: 6
                        hoverEnabled: true
                        onClicked: { if (app.busy) return; app.openSession(modelData.id); window.selectedSessionId = modelData.id; window.conversationTitle = modelData.title; window.followConversation = true; scrollToEnd.restart() }
                        Accessible.name: modelData.title
                        ToolTip.visible: hovered
                        ToolTip.text: modelData.title
                        ToolTip.delay: 900
                        contentItem: RowLayout {
                            spacing: 9
                            Icon { implicitWidth: 15; implicitHeight: 15; name: "chat"; color: sessionButton.modelData.id === window.selectedSessionId ? ui.accent : ui.dim }
                            Text { textFormat: Text.PlainText; text: sessionButton.modelData.title || "Conversation"; Layout.fillWidth: true; elide: Text.ElideRight; color: sessionButton.modelData.id === window.selectedSessionId ? ui.text : ui.muted; font.family: ui.fontFamily; font.pixelSize: 12 }
                            IconButton { theme: ui; iconName: "trash"; tip: "Supprimer cette conversation"; implicitWidth: 25; implicitHeight: 27; visible: sessionButton.hovered || sessionButton.activeFocus; onClicked: app.deleteSession(sessionButton.modelData.id) }
                        }
                        background: Rectangle { radius: 9; color: sessionButton.modelData.id === window.selectedSessionId ? ui.panel : sessionButton.hovered ? ui.hover : "transparent"; border.width: sessionButton.visualFocus ? 1 : 0; border.color: ui.accent }
                    }
                    Column {
                        anchors.top: parent.top; anchors.topMargin: 32; anchors.horizontalCenter: parent.horizontalCenter
                        width: parent.width - 20; spacing: 12; visible: history.count === 0
                        Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "chat"; width: 25; height: 25; color: ui.dim }
                        Text { textFormat: Text.PlainText; width: parent.width; text: historySearch.text.length ? "Aucune conversation trouvée." : "Un espace pour vos idées.\nVos conversations apparaîtront ici."; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; color: ui.dim; font.family: ui.fontFamily; font.pixelSize: 11; lineHeight: 1.5 }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true; implicitHeight: 65; radius: 12; color: ui.panel
                    RowLayout {
                        anchors.fill: parent; anchors.margins: 12; spacing: 10
                        Icon { implicitWidth: 20; implicitHeight: 20; name: "lock"; color: ui.accent }
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 4
                            Text { textFormat: Text.PlainText; text: window.managedProvider ? "L’intelligence, chez vous." : "Votre service configuré."; color: ui.text; font.family: ui.fontFamily; font.pixelSize: 11; font.weight: Font.DemiBold }
                            Text { textFormat: Text.PlainText; text: window.managedProvider ? "Inférence locale" : "Service local"; color: ui.dim; font.family: ui.fontFamily; font.pixelSize: 9 }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    AppButton { theme: ui; text: "Paramètres"; iconName: "settings"; variant: "ghost"; compact: true; tip: "Paramètres · Ctrl+,"; onClicked: window.openSettings(0) }
                    Item { Layout.fillWidth: true }
                    IconButton { theme: ui; iconName: ui.dark ? "sun" : "moon"; tip: ui.dark ? "Activer le thème clair" : "Activer le thème sombre"; onClicked: app.updateSetting("theme",ui.dark ? "light" : "dark") }
                }
            }
        }

        Rectangle {
            id: centerPane
            SplitView.fillWidth: true
            SplitView.minimumWidth: 470
            color: ui.background
            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                Item {
                    Layout.fillWidth: true; implicitHeight: 78
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 20; anchors.rightMargin: 23
                        spacing: 12
                        IconButton { theme: ui; iconName: "sidebar"; tip: window.sidebarVisible ? "Masquer l’historique" : "Afficher l’historique"; selected: !window.sidebarVisible; onClicked: window.sidebarVisible = !window.sidebarVisible }
                        ModelPicker { theme: ui; backend: app; Layout.maximumWidth: Math.max(180, centerPane.width - 230); onConfigureProvider: window.openSettings(1) }
                        Item { Layout.fillWidth: true }
                        Rectangle {
                            visible: centerPane.width > 650
                            implicitWidth: window.managedProvider ? 125 : 108; implicitHeight: 24; radius: 7; color: ui.panel; border.color: ui.border
                            Row { anchors.centerIn: parent; spacing: 5; Rectangle { width: 5; height: 5; y: 4; radius: 3; color: app.runtimeReady ? ui.accent : ui.dim } Text { textFormat: Text.PlainText; text: window.managedProvider ? "Inférence locale" : "Service local"; color: ui.muted; font.family: ui.fontFamily; font.pixelSize: 9; font.weight: Font.DemiBold; font.letterSpacing: 0.3 } }
                        }
                        IconButton { theme: ui; iconName: "panel"; tip: window.workspaceVisible ? "Masquer l’espace de travail" : "Afficher l’espace de travail"; selected: window.workspaceVisible; onClicked: window.workspaceVisible = !window.workspaceVisible }
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: ui.border; opacity: 0.7 }
                }

                Item {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    ListView {
                        id: conversation
                        anchors.fill: parent
                        visible: count > 0
                        clip: true
                        model: app.messages
                        spacing: 29
                        topMargin: 30
                        bottomMargin: 30
                        boundsBehavior: Flickable.StopAtBounds
                        cacheBuffer: 600
                        ScrollBar.vertical: ScrollBar { width: 5 }
                        onMovementStarted: window.followConversation = false
                        onMovementEnded: if (atYEnd) window.followConversation = true
                        onContentHeightChanged: if (window.followConversation) scrollToEnd.restart()
                        delegate: MessageBubble {
                            theme: ui
                            width: ListView.view.width
                        }
                    }
                    ScrollView {
                        id: welcomeScroll
                        anchors.fill: parent
                        visible: conversation.count === 0
                        clip: true
                        contentWidth: availableWidth
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                        ColumnLayout {
                            width: welcomeScroll.availableWidth
                            spacing: 0
                            Item { Layout.preferredHeight: Math.max(24, Math.min(78, (welcomeScroll.height - 480) / 2)) }
                            ColumnLayout {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredWidth: Math.min(680, welcomeScroll.availableWidth - 72)
                                Layout.maximumWidth: Math.min(680, welcomeScroll.availableWidth - 72)
                                spacing: 19
                                Rectangle {
                                    Layout.alignment: Qt.AlignHCenter
                                    implicitWidth: 60; implicitHeight: 60; radius: 20
                                    color: ui.accentSoft
                                    border.color: ui.dark ? "#2b5048" : "#c5e3d8"
                                    Icon { anchors.centerIn: parent; width: 30; height: 30; name: "spark"; color: ui.accent; strokeWidth: 1.35 }
                                }
                                Text { textFormat: Text.PlainText; text: "UN PEU D’INTELLIGENCE. BEAUCOUP DE POSSIBILITÉS."; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; color: ui.accent; font.family: ui.fontFamily; font.pixelSize: centerPane.width < 600 ? 8 : 9; font.weight: Font.DemiBold; font.letterSpacing: 1.45 }
                                Text { textFormat: Text.PlainText;
                                    text: window.managedProvider && app.models.length === 0 ? "Votre IA. Votre machine.\nVotre prochain grand projet." : "Qu’allons-nous créer ?"
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    color: ui.text; font.family: ui.fontFamily; font.pixelSize: centerPane.width < 650 ? 28 : 34; font.weight: Font.DemiBold; font.letterSpacing: -1
                                    lineHeight: 1.22
                                }
                                Text { textFormat: Text.PlainText; text: window.managedProvider && app.models.length === 0 ? "Choisissez un modèle local pour donner vie à vos idées." : "Un partenaire pour réfléchir, écrire, coder et explorer vos fichiers."; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; color: ui.muted; font.family: ui.fontFamily; font.pixelSize: 12; lineHeight: 1.5 }

                                Rectangle {
                                    visible: window.managedProvider && (app.models.length === 0 || !app.runtimeReady)
                                    Layout.fillWidth: true
                                    Layout.topMargin: 7
                                    implicitHeight: setupContents.implicitHeight + 34
                                    radius: 15; color: ui.panel; border.color: ui.border
                                    ColumnLayout {
                                        id: setupContents
                                        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 18
                                        spacing: 12
                                        RowLayout {
                                            spacing: 10
                                            Icon { name: app.models.length === 0 ? "folder" : "cpu"; implicitWidth: 20; implicitHeight: 20; color: ui.accent }
                                            Text { textFormat: Text.PlainText; text: app.models.length === 0 ? "Installez-vous, on prépare votre espace." : "Un dernier réglage, et c’est parti."; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: ui.text; font.family: ui.fontFamily; font.pixelSize: 13; font.weight: Font.DemiBold }
                                        }
                                        Text { textFormat: Text.PlainText; text: app.models.length === 0 ? "Déposez vos fichiers GGUF dans le dossier des modèles, ou choisissez un dossier existant. Titan les détecte automatiquement." : (app.runtimeStatus || "Choisissez un modèle en haut de la fenêtre pour le charger, ou installez le moteur local."); Layout.fillWidth: true; wrapMode: Text.WordWrap; lineHeight: 1.5; color: ui.muted; font.family: ui.fontFamily; font.pixelSize: 11 }
                                        Flow {
                                            Layout.fillWidth: true
                                            spacing: 8
                                            AppButton { theme: ui; text: "Choisir un dossier"; iconName: "folder"; compact: true; variant: app.models.length === 0 ? "primary" : "secondary"; onClicked: app.chooseModelsFolder() }
                                            AppButton { theme: ui; text: "Ouvrir le dossier"; iconName: "external"; compact: true; variant: "ghost"; onClicked: app.openModelsFolder() }
                                            AppButton { theme: ui; text: "Installer le moteur"; iconName: "download"; compact: true; visible: !app.runtimeReady; enabled: !app.busy && !(app.downloadProgress >= 0 && app.downloadProgress < 1); onClicked: app.installRuntime() }
                                        }
                                        ProgressBar { visible: app.downloadProgress >= 0 && app.downloadProgress < 1; Layout.fillWidth: true; value: app.downloadProgress; background: Rectangle { color: ui.control; implicitHeight: 4; radius: 2 } contentItem: Item { implicitHeight: 4; Rectangle { width: parent.width * app.downloadProgress; height: 4; radius: 2; color: ui.accent } } }
                                        AppButton { theme: ui; text: "Utiliser Ollama, LM Studio ou un autre serveur"; iconName: "arrow"; variant: "ghost"; compact: true; onClicked: window.openSettings(1) }
                                    }
                                }

                                GridLayout {
                                    Layout.fillWidth: true
                                    Layout.topMargin: 8
                                    columns: 2
                                    columnSpacing: 11; rowSpacing: 11
                                    Repeater {
                                        model: [
                                            {title:"Donner forme à une idée",detail:"Un premier jet, un plan, une nouvelle piste",icon:"spark",prompt:"Aide-moi à structurer une idée de projet. Pose-moi les questions utiles pour commencer."},
                                            {title:"Explorer mes documents",detail:"Résumer, retrouver, faire des liens",icon:"file",prompt:"Aide-moi à explorer les fichiers de mes dossiers autorisés. Commence par m’en présenter le contenu."},
                                            {title:"Construire quelque chose",detail:"Du code clair, une solution concrète",icon:"code",prompt:"J’aimerais construire un projet avec toi. Aide-moi à préciser mon objectif et à définir les premières étapes."},
                                            {title:"Ouvrir de nouveaux horizons",detail:"Rechercher et naviguer avec l’agent",icon:"globe",prompt:"Aide-moi à effectuer une recherche dans le navigateur. Demande-moi le sujet et le résultat attendu."}
                                        ]
                                        delegate: Button {
                                            id: suggestion
                                            required property var modelData
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 92
                                            hoverEnabled: true
                                            onClicked: window.suggest(modelData.prompt)
                                            Accessible.name: modelData.title
                                            contentItem: ColumnLayout {
                                                spacing: 8
                                                RowLayout {
                                                    Layout.fillWidth: true
                                                    Icon { name: suggestion.modelData.icon; implicitWidth: 17; implicitHeight: 17; color: ui.accent }
                                                    Item { Layout.fillWidth: true }
                                                    Icon { name: "arrow"; implicitWidth: 14; implicitHeight: 14; color: suggestion.hovered ? ui.accent : ui.dim }
                                                }
                                                Text { textFormat: Text.PlainText; text: suggestion.modelData.title; Layout.fillWidth: true; elide: Text.ElideRight; color: ui.text; font.family: ui.fontFamily; font.pixelSize: 12; font.weight: Font.DemiBold }
                                                Text { textFormat: Text.PlainText; text: suggestion.modelData.detail; Layout.fillWidth: true; elide: Text.ElideRight; color: ui.dim; font.family: ui.fontFamily; font.pixelSize: 10 }
                                            }
                                            padding: 14
                                            background: Rectangle { radius: 12; color: suggestion.hovered ? ui.panel : "transparent"; border.color: suggestion.visualFocus ? ui.accent : ui.border; Behavior on color { ColorAnimation { duration: ui.motionDuration } } }
                                        }
                                    }
                                }
                            }
                            Item { Layout.preferredHeight: 28 }
                        }
                    }
                    AppButton {
                        theme: ui; text: "Derniers messages"; iconName: "chevron"; compact: true
                        anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 12
                        visible: conversation.count > 0 && !conversation.atYEnd
                        onClicked: { window.followConversation = true; conversation.positionViewAtEnd() }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 864
                    Layout.alignment: Qt.AlignHCenter
                    Layout.leftMargin: 28; Layout.rightMargin: 28
                    Layout.bottomMargin: 15
                    spacing: 10
                    Rectangle {
                        visible: app.errorMessage.length > 0
                        Layout.fillWidth: true
                        implicitHeight: errorRow.implicitHeight + 20
                        radius: 11; color: ui.panel; border.color: ui.danger
                        RowLayout {
                            id: errorRow
                            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 10
                            spacing: 9
                            Icon { name: "info"; implicitWidth: 18; implicitHeight: 18; color: ui.danger }
                            Text { textFormat: Text.PlainText; text: app.errorMessage; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: ui.danger; font.family: ui.fontFamily; font.pixelSize: 11; maximumLineCount: 5; elide: Text.ElideRight }
                            IconButton { theme: ui; iconName: "settings"; tip: "Ouvrir les paramètres du moteur"; onClicked: window.openSettings(1) }
                            IconButton { theme: ui; iconName: "close"; tip: "Fermer le message d’erreur"; onClicked: app.clearError() }
                        }
                    }
                    Rectangle {
                        id: composerFrame
                        Layout.fillWidth: true
                        implicitHeight: composerColumn.implicitHeight + 10
                        radius: 18
                        color: ui.raised
                        border.color: composer.activeFocus ? (ui.dark ? "#44645e" : ui.accent) : ui.border
                        Behavior on border.color { ColorAnimation { duration: ui.motionDuration } }
                        ColumnLayout {
                            id: composerColumn
                            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                            anchors.margins: 9
                            spacing: 4
                            ScrollView {
                                id: composerScroll
                                Layout.fillWidth: true
                                Layout.preferredHeight: Math.min(170,Math.max(55,composer.contentHeight + 17))
                                clip: true
                                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                                TextArea {
                                    id: composer
                                    placeholderText: window.hasApproval ? "Une action attend votre accord…" : app.busy ? "Préparez votre prochain message…" : "Écrivez, explorez, créez…"
                                    color: ui.text; placeholderTextColor: ui.dim
                                    selectionColor: ui.accentSoft; selectedTextColor: ui.text
                                    font.family: ui.fontFamily; font.pixelSize: 14
                                    wrapMode: TextEdit.Wrap
                                    selectByMouse: true
                                    leftPadding: 10; rightPadding: 10; topPadding: 9; bottomPadding: 9
                                    background: null
                                    Accessible.name: "Message à Titan"
                                    Keys.onPressed: event => {
                                        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && !(event.modifiers & Qt.ShiftModifier)) {
                                            window.submitPrompt()
                                            event.accepted = true
                                        }
                                    }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 5
                                IconButton { theme: ui; iconName: "folder"; tip: "Partager un dossier avec l’agent"; onClicked: app.chooseGrantFolder() }
                                Rectangle { implicitWidth: 1; implicitHeight: 16; color: ui.border }
                                Choice { theme: ui; visible: app.thinkingSupported; implicitWidth: centerPane.width < 620 ? 140 : 168; implicitHeight: 32; font.pixelSize: 11; value: window.setting("thinkingLevel","auto"); choices: window.thinkingChoices(); onPicked: value => app.updateSetting("thinkingLevel",value) }
                                AppButton { theme: ui; compact: true; visible: centerPane.width >= 650 || !app.thinkingSupported; text: window.setting("toolMode","auto") === "off" ? "Outils désactivés" : "Outils"; iconName: "code"; variant: "ghost"; tip: "Configurer les outils de l’agent"; onClicked: window.openSettings(3) }
                                IconButton { theme: ui; visible: centerPane.width < 650 && app.thinkingSupported; iconName: "code"; tip: "Configurer les outils de l’agent"; onClicked: window.openSettings(3) }
                                Item { Layout.fillWidth: true }
                                Text { textFormat: Text.PlainText; visible: centerPane.width > 780 && !app.busy; text: "Maj + Entrée pour une nouvelle ligne"; color: ui.dim; font.family: ui.fontFamily; font.pixelSize: 9 }
                                AppButton { theme: ui; visible: app.busy; compact: true; text: centerPane.width < 620 ? "" : "Arrêter"; iconName: "stop"; tip: "Arrêter la génération · Échap"; onClicked: app.stop() }
                                Button {
                                    id: sendButton
                                    visible: !app.busy
                                    implicitWidth: 35; implicitHeight: 35
                                    padding: 8
                                    enabled: window.canSend && composer.text.trim().length > 0
                                    onClicked: window.submitPrompt()
                                    Accessible.name: "Envoyer le message"
                                    ToolTip.visible: hovered; ToolTip.delay: 600
                                    ToolTip.text: !app.runtimeReady ? "Chargez un modèle pour commencer" : "Envoyer · Entrée"
                                    contentItem: Icon { name: "send"; color: sendButton.enabled ? ui.accentText : ui.dim }
                                    background: Rectangle { radius: 11; color: sendButton.enabled ? sendButton.down ? ui.accentPressed : sendButton.hovered ? ui.accentHover : ui.accent : ui.control; border.width: sendButton.visualFocus ? 2 : 0; border.color: ui.text; Behavior on color { ColorAnimation { duration: ui.motionDuration } } }
                                }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true; Layout.leftMargin: 5; Layout.rightMargin: 5
                        spacing: 7
                        Rectangle { implicitWidth: 5; implicitHeight: 5; radius: 3; color: window.hasApproval ? ui.warning : app.runtimeReady ? ui.accent : ui.dim }
                        Text { textFormat: Text.PlainText; text: window.hasApproval ? "Votre accord est demandé" : app.status || (app.runtimeReady ? "Prêt à vous accompagner" : "En attente d’un modèle"); Layout.fillWidth: true; elide: Text.ElideRight; color: ui.dim; font.family: ui.fontFamily; font.pixelSize: 9 }
                        Text { textFormat: Text.PlainText; text: "VOTRE ESPACE. VOTRE IA."; color: ui.dim; font.family: ui.fontFamily; font.pixelSize: 8; font.weight: Font.DemiBold; font.letterSpacing: 0.8 }
                    }
                }
            }
        }

        Rectangle {
            id: workspace
            visible: window.workspaceVisible
            SplitView.preferredWidth: 284
            SplitView.minimumWidth: 240
            SplitView.maximumWidth: 390
            color: ui.sidebar
            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 78
                    Layout.leftMargin: 19; Layout.rightMargin: 14
                    Text { textFormat: Text.PlainText; text: "Espace de travail"; Layout.fillWidth: true; color: ui.text; font.family: ui.fontFamily; font.pixelSize: 13; font.weight: Font.DemiBold }
                    IconButton { theme: ui; iconName: "close"; tip: "Fermer l’espace de travail"; onClicked: window.workspaceVisible = false }
                }
                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: ui.border; opacity: 0.7 }
                ScrollView {
                    id: workspaceScroll
                    Layout.fillWidth: true; Layout.fillHeight: true
                    contentWidth: availableWidth
                    clip: true
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    ColumnLayout {
                        width: workspaceScroll.availableWidth
                        spacing: 22
                        Item { implicitHeight: 0 }
                        ColumnLayout {
                            Layout.fillWidth: true; Layout.leftMargin: 18; Layout.rightMargin: 18
                            spacing: 13
                            RowLayout {
                                Layout.fillWidth: true
                                Icon { name: "folder"; implicitWidth: 16; implicitHeight: 16; color: ui.muted }
                                Text { textFormat: Text.PlainText; text: "Dossiers partagés"; Layout.fillWidth: true; color: ui.text; font.family: ui.fontFamily; font.pixelSize: 12; font.weight: Font.DemiBold }
                                Text { textFormat: Text.PlainText; text: String(app.folderGrants.length).padStart(2,"0"); color: ui.dim; font.family: ui.monoFamily; font.pixelSize: 10 }
                            }
                            Text { textFormat: Text.PlainText; text: "Le bon contexte fait toute la différence. Donnez à Titan accès à vos fichiers utiles."; Layout.fillWidth: true; wrapMode: Text.WordWrap; lineHeight: 1.5; color: ui.dim; font.family: ui.fontFamily; font.pixelSize: 11 }
                            Rectangle {
                                visible: app.folderGrants.length === 0
                                Layout.fillWidth: true; implicitHeight: 102
                                radius: 11; color: ui.panel; border.color: ui.border
                                Column {
                                    anchors.centerIn: parent; spacing: 10
                                    Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "folder"; width: 25; height: 25; color: ui.dim }
                                    Text { textFormat: Text.PlainText; text: "Aucun dossier partagé"; color: ui.muted; font.family: ui.fontFamily; font.pixelSize: 11 }
                                }
                            }
                            Repeater {
                                model: app.folderGrants
                                delegate: Rectangle {
                                    id: folderCard
                                    required property var modelData
                                    Layout.fillWidth: true
                                    implicitHeight: folderContents.implicitHeight + 20
                                    color: ui.panel; border.color: ui.border; radius: 11
                                    ColumnLayout {
                                        id: folderContents
                                        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 10
                                        spacing: 6
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Icon { name: "folder"; implicitWidth: 16; implicitHeight: 16; color: ui.accent }
                                            Text { textFormat: Text.PlainText; text: folderCard.modelData.path.replace(/\\/g,"/").split("/").filter(Boolean).pop() || folderCard.modelData.path; Layout.fillWidth: true; elide: Text.ElideRight; color: ui.text; font.family: ui.fontFamily; font.pixelSize: 11; font.weight: Font.DemiBold }
                                            IconButton { theme: ui; iconName: "close"; tip: "Retirer l’accès à ce dossier"; implicitWidth: 22; implicitHeight: 22; onClicked: app.removeGrant(folderCard.modelData.path) }
                                        }
                                        Text { textFormat: Text.PlainText; text: folderCard.modelData.path; Layout.fillWidth: true; elide: Text.ElideMiddle; color: ui.dim; font.family: ui.fontFamily; font.pixelSize: 9 }
                                        ToggleRow { theme: ui; Layout.fillWidth: true; title: "Lecture + écriture"; checked: folderCard.modelData.writable; onToggled: value => app.setGrantWritable(folderCard.modelData.path,value) }
                                        Text { textFormat: Text.PlainText; text: folderCard.modelData.writable ? "Écriture autorisée dans ce dossier" : "Lecture et recherche uniquement"; color: folderCard.modelData.writable ? ui.accent : ui.dim; font.family: ui.fontFamily; font.pixelSize: 9 }
                                    }
                                }
                            }
                            AppButton { theme: ui; text: "Ajouter un dossier"; iconName: "plus"; Layout.fillWidth: true; compact: true; onClicked: app.chooseGrantFolder() }
                        }
                        Rectangle { Layout.fillWidth: true; Layout.leftMargin: 18; Layout.rightMargin: 18; implicitHeight: 1; color: ui.border }
                        ColumnLayout {
                            Layout.fillWidth: true; Layout.leftMargin: 18; Layout.rightMargin: 18
                            spacing: 12
                            RowLayout {
                                Layout.fillWidth: true
                                Icon { name: "globe"; implicitWidth: 16; implicitHeight: 16; color: ui.muted }
                                Text { textFormat: Text.PlainText; text: "Navigateur de l’agent"; Layout.fillWidth: true; color: ui.text; font.family: ui.fontFamily; font.pixelSize: 12; font.weight: Font.DemiBold }
                                Rectangle { implicitWidth: 6; implicitHeight: 6; radius: 3; color: app.browserConnected ? ui.accent : ui.dim }
                            }
                            Rectangle {
                                Layout.fillWidth: true; implicitHeight: browserContents.implicitHeight + 26
                                radius: 12; color: ui.panel; border.color: ui.border
                                ColumnLayout {
                                    id: browserContents
                                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 13
                                    spacing: 11
                                    RowLayout {
                                        Rectangle { implicitWidth: 8; implicitHeight: 8; radius: 4; color: app.browserConnected ? ui.accent : ui.warning }
                                        Text { textFormat: Text.PlainText; text: app.browserConnected ? "Connecté et prêt" : "Prêt à explorer"; color: ui.text; font.family: ui.fontFamily; font.pixelSize: 11; font.weight: Font.DemiBold }
                                    }
                                    Text { textFormat: Text.PlainText; text: "Un navigateur dédié pour rechercher, lire et interagir avec les pages, avec votre accord."; Layout.fillWidth: true; wrapMode: Text.WordWrap; lineHeight: 1.5; color: ui.muted; font.family: ui.fontFamily; font.pixelSize: 11 }
                                    AppButton { theme: ui; text: app.browserConnected ? "Afficher le navigateur" : "Ouvrir le navigateur"; iconName: "external"; Layout.fillWidth: true; compact: true; onClicked: app.openBrowser() }
                                    Text { textFormat: Text.PlainText; text: "BROWSER CONTROL SDK"; color: ui.dim; font.family: ui.fontFamily; font.pixelSize: 8; font.letterSpacing: 0.8 }
                                }
                            }
                        }
                        Rectangle { Layout.fillWidth: true; Layout.leftMargin: 18; Layout.rightMargin: 18; implicitHeight: 1; color: ui.border }
                        ColumnLayout {
                            Layout.fillWidth: true; Layout.leftMargin: 18; Layout.rightMargin: 18
                            spacing: 13
                            RowLayout {
                                Layout.fillWidth: true
                                Icon { name: "spark"; implicitWidth: 16; implicitHeight: 16; color: ui.muted }
                                Text { textFormat: Text.PlainText; text: "Activité"; Layout.fillWidth: true; color: ui.text; font.family: ui.fontFamily; font.pixelSize: 12; font.weight: Font.DemiBold }
                                BusyDots { theme: ui; visible: app.busy; running: visible }
                            }
                            Text { textFormat: Text.PlainText; text: window.hasApproval ? "L’agent attend votre accord pour continuer." : app.busy ? (app.status || "Titan prépare votre réponse…") : "Chaque action de l’agent apparaît dans la conversation. Vous gardez le fil, à chaque étape."; Layout.fillWidth: true; wrapMode: Text.WordWrap; lineHeight: 1.55; color: window.hasApproval ? ui.warning : ui.dim; font.family: ui.fontFamily; font.pixelSize: 11 }
                            AppButton { theme: ui; text: window.setting("approvalsEnabled",true) ? "Confirmations activées" : "Confirmations désactivées"; iconName: "shield"; variant: "ghost"; compact: true; Layout.fillWidth: true; onClicked: window.openSettings(3) }
                        }
                        Item { implicitHeight: 10 }
                    }
                }
                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: ui.border }
                RowLayout {
                    Layout.fillWidth: true; Layout.margins: 18
                    Icon { name: "cpu"; implicitWidth: 15; implicitHeight: 15; color: ui.dim }
                    Text { textFormat: Text.PlainText; text: app.runtimeReady ? "Moteur opérationnel" : "Moteur en attente"; Layout.fillWidth: true; color: ui.dim; font.family: ui.fontFamily; font.pixelSize: 10 }
                    IconButton { theme: ui; iconName: "settings"; tip: "Configurer le moteur local"; implicitWidth: 25; implicitHeight: 25; onClicked: window.openSettings(1) }
                }
            }
        }
    }

    SettingsDialog { id: settings; theme: ui; backend: app }
    ApprovalDialog { id: approvalDialog; theme: ui; backend: app }
}
