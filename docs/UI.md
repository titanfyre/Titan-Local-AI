# Titan Local AI desktop UI

The flat `qml/` directory belongs in the `TitanLocalAI` QML module. The root component is `Main.qml`, an `ApplicationWindow`. Use `QQuickStyle::setStyle("Basic")` before loading QML. Imports: `QtQuick`, `QtQuick.Controls`, `QtQuick.Layouts`, `QtQuick.Window`. No web engine, shader, third-party font, QtGraphicalEffects or network asset is required.

Set the root context property `app` to the controller described in `IMPLEMENTATION_CONTRACT.md` before loading the module. All components bind only to that contract. Optional model fields `loadable` and `warning` disable incomplete model choices and show their warning. Unknown `systemInfo` fields are intentionally not interpreted.

The sidebar and workspace are resizable and can be hidden. Main shortcuts: Ctrl+N for a new conversation, Ctrl+L to focus the composer, Ctrl+, for settings, Enter to send, Shift+Enter for a line break, Escape to stop a running response. Messages use native `TextEdit` Markdown with text selection and copy. Only explicit HTTP(S) Markdown links can open in the user's default browser. The dedicated agent browser is opened through `app.openBrowser()`.

Folder selectors, model-folder opening, runtime installation and executable selection invoke native backend actions. Permission toggles call `setGrantWritable` explicitly. The approval modal displays `tool`, `summary` and `preview`, and does not dismiss until the controller clears `pendingApproval` after a decision. Existing conversation history is read from `sessions`; the UI creates no example histories, model names or tool outcomes.

Appearance is bound to `settings.theme` and `settings.reducedMotion`. Busy dot animations run only during actual busy/streaming states; no idle animation loops run. Light/dark/system modes and responsive settings dialogs are supported. Thinking choices use the optional `settings.availableThinkingLevels` list supplied by the controller, including on/off-only model support. The UI requires at least 960×660 logical pixels; the default window size is 1500×940. The main font is Segoe UI, with Cascadia Code for tool previews (the OS supplies fallback when unavailable).

`MarkdownBody.qml` keeps native selectable Markdown and renders completed fenced code in dedicated cards. During streaming it updates one native text document, avoiding rebuilding the block view on each token. It strips Markdown image syntax and HTML image tags outside fenced code before rendering, as defense in depth; all labels explicitly use plain text. This complements, and does not replace, backend sanitization.

`assets/app-icon.svg` is a vector source for the application icon; it may be converted into a multi-size Windows ICO during packaging.
