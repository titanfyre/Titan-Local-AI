# Composants tiers

La licence MIT de Titan Local AI ne remplace pas les licences des composants suivants.

| Composant | Version / origine | Licence et distribution |
| --- | --- | --- |
| Qt : Core, Gui, Widgets, Network, Concurrent, QML, Quick, Quick Controls et dépendances déployées | 6.8.3, binaires MSVC x64 officiels | Liaison dynamique sous LGPL-3.0, avec exceptions et licences propres aux composants tiers. Aucun correctif de code source Qt n’est appliqué ; `windeployqt` peut ajuster les chemins de déploiement. |
| Browser Control SDK, client C++ | Sources présentes dans `third_party/browser_control/`, TitanCSA | MIT ; texte intégral dans `third_party/browser_control/LICENSE`. |
| nlohmann/json | 3.12.0 | MIT ; texte intégral dans `third_party/nlohmann/LICENSE`. |
| llama.cpp | b12167, ggml-org | MIT ; moteur téléchargé séparément par l’application, absent du paquet de base. Les archives téléchargées peuvent contenir d’autres composants avec leurs propres notices. |
| BrowserControl | v0.2.0, TitanCSA | Composant téléchargé séparément à l’ouverture du navigateur ; projet sous MIT. Les composants Microsoft du navigateur restent soumis à leurs propres conditions. |
| Microsoft Visual C++ Runtime | DLL x64 Release du Visual Studio utilisé pour empaqueter | Conditions Microsoft ; DLL copiées sans modification depuis le dossier redistribuable de Visual Studio. |

## Qt et LGPL

La distribution créée par `scripts/Package.ps1` comprend :

- les textes de licences des modules Qt déployables, notamment `LGPL-3.0-only.txt` et `GPL-3.0-only.txt` ;
- les inventaires SPDX fournis par Qt, avec attributions et informations des composants tiers ;
- les archives sources Qt **6.8.3** correspondantes (`qtbase`, `qtdeclarative`, `qtsvg`, `qttranslations`), vérifiées par empreinte SHA-256, et la configuration du paquet Qt utilisé ;
- les licences MIT des composants incorporés et les informations du runtime MSVC.

Conservez le dossier `licenses/` avec les binaires lors d’une redistribution. Le script n’inclut ni les outils de développement Qt ni des bibliothèques Qt choisies exclusivement sous GPL.

Vous pouvez remplacer les DLL Qt et leurs plugins par des versions compatibles que vous avez recompilées. Fermez l’application, sauvegardez le dossier distribué, puis remplacez les bibliothèques concernées et leurs plugins en conservant architecture x64, ABI, noms et arborescence compatibles. Aucune vérification de signature de Titan n’empêche ce remplacement. Le code de Titan et ses fichiers CMake sont fournis sous MIT pour permettre sa recompilation. Les droits de modification et d’analyse nécessaires au débogage de modifications des bibliothèques LGPL sont préservés.

Les sources officielles et les conditions détaillées sont disponibles dans les [archives Qt 6.8.3](https://download.qt.io/archive/qt/6.8/6.8.3/submodules/), les [informations LGPL de Qt](https://www.qt.io/development/open-source-lgpl-obligations) et les [licences tierces de Qt](https://doc.qt.io/qt-6.8/licenses-used-in-qt.html). Pour une version modifiée de Qt, fournissez aussi ses modifications et les instructions de reconstruction correspondantes.

## Autres sources et conditions

- [Browser Control SDK](https://github.com/TitanCSA/Browser-Control-SDK), avec les textes MIT conservés dans le paquet.
- [nlohmann/json 3.12.0](https://github.com/nlohmann/json/tree/v3.12.0).
- [llama.cpp b12167](https://github.com/ggml-org/llama.cpp/tree/b12167) et sa [licence MIT](https://github.com/ggml-org/llama.cpp/blob/b12167/LICENSE). Les URL et empreintes de ses archives Windows figurent dans `src/runtime/runtime-manifest.json` (copié dans `licenses/llama.cpp-runtime-manifest.json` dans le paquet).
- [Conditions de redistribution Visual C++](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files?view=msvc-170), [liste redistribuable Visual Studio 2022](https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution) et [licence du runtime](https://visualstudio.microsoft.com/license-terms/vs2022-cruntime/).

Les poids de modèles sont acquis séparément et restent soumis à leurs licences propres. Aucune licence de modèle n’est accordée par Titan.
