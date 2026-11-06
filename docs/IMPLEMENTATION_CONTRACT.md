# Titan Local AI — implementation contract

Windows x64 desktop application. C++20 backend and Qt 6.8 Quick/Controls UI. No web UI wrapper. Build with CMake and MSVC 2022. All repository changes belong in this new project; do not modify sibling projects or perform Reddit work.

## UI/backend boundary

Root context property `app` is an `AppController` QObject.

Properties (with change notifications): `messages` (QAbstractListModel), `models` (QVariantList), `folderGrants` (QVariantList), `sessions` (QVariantList), `settings` (QVariantMap), `systemInfo` (QVariantMap), `selectedModelId` (QString), `busy` (bool), `status` (QString), `errorMessage` (QString), `runtimeStatus` (QString), `runtimeReady` (bool), `downloadProgress` (double 0..1 or -1), `browserConnected` (bool), `modelsPath` (QString), `thinkingSupported` (bool), `pendingApproval` (QVariantMap; empty or tool/summary/preview).

Message model roles: `messageId`, `messageRole` (user/assistant/tool), `messageText`, `reasoningText`, `messageState` (streaming/done/error/running), `toolLabel`.

Models are maps: id, name, path, sizeLabel, quantization, architecture, supportsThinking. Folder grants: path, writable. Sessions: id, title, updated.

Invokables: `sendPrompt(QString)`, `stop()`, `newChat()`, `openSession(QString)`, `deleteSession(QString)`, `chooseModelsFolder()`, `openModelsFolder()`, `refreshModels()`, `chooseGrantFolder()`, `removeGrant(QString)`, `setGrantWritable(QString,bool)`, `selectModel(QString)`, `updateSetting(QString,QVariant)`, `installRuntime()`, `connectProvider()`, `openBrowser()`, `clearError()`, `answerApproval(bool)`, `chooseRuntimeExecutable()`.

Settings keys: provider (managed/ollama/lmstudio/custom), endpoint, externalModel, temperature, topP, contextSize, maxTokens, threads, gpuLayers (-1 automatic,0 CPU), thinkingLevel (auto/off/low/medium/high), approvalsEnabled, toolMode (auto/native/json/off), maxSteps, systemPrompt, theme (dark/light/system), reducedMotion. Never invent backend properties in QML without updating this contract and coordinating.

## Runtime interfaces

`ModelCatalog` QObject owns asynchronous model folder scanning/watching and GGUF metadata. Methods setDirectory(QString), directory(), refresh(), models(); signals modelsChanged(), directoryChanged().

`RuntimeManager` QObject: configure(QVariantMap), loadModel(QVariantMap), ensureInstalled(), setExternalEndpoint(QString,QString), stop(), setExecutable(QString). Getters ready(), status(), progress(), baseUrl(), logTail(), supportsThinking(), modelId(). Signals stateChanged(), modelReady(QString), failed(QString), capabilitiesChanged(). Owns only processes it launches. HTTP bound to loopback; managed inference never contacts a cloud model API. Download approved official release artifacts, verify supplied/pinned SHA-256, atomic install, no visible PowerShell helper windows. No models downloaded automatically.

## Filesystem interface

`FolderGrant { QString path; bool writable; }` and plain C++ `WorkspaceAccess` in namespace `titan` (runtime classes also namespace titan). Constructor accepts QStringList protectedRoots. Methods setGrants(QList<FolderGrant>), grants(), invoke(QString,QJsonObject), static toolDefinitions(). Tools: files_list, files_read, files_search, files_write, files_replace. Results are JSON success/error objects, not exceptions across the UI boundary.

Enforce canonical root boundaries, Windows path case and separators, junction/symlink escapes, read-only grants, and protected application data. No shell, executable tool, delete, cookie/storage/vault or arbitrary browser evaluation tool. Existing-file writes require expected_sha256 from a prior read; new-file writes must not overwrite an existing file. Bounded text I/O/search, atomic saves, backups outside model grants. Tests use temporary owned directories only.

## Browser and agent

Use Browser-Control-SDK C++ client and dedicated owned browser instance/profile. Browser capabilities expose an allowlist: tabs, navigate HTTP(S), observe, finite guarded act, screenshot. No generic RPC pass-through, cookie/vault APIs, eval, file:// or unrestricted CDP. Tool results/page text are untrusted data, cannot grant permissions. File writes and browser acts support one approval card per bounded operation; the user can explicitly turn confirmations off. Folder permissions can only be changed through UI.

Agent supports streaming OpenAI-compatible chat completions from local inference, native tool calls and explicit JSON tool fallback for local models without native function support, cancellation and a finite step limit. Never fabricate a tool outcome. Save histories locally, and keep application data, model files, downloads, endpoint credentials and generated binaries out of git.
