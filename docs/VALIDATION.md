# Validation observée

État relevé le **6 novembre 2026**, sur la machine Windows x64 de développement, avec Qt **6.8.3** dynamique et MSVC 2022. Ce relevé décrit des exécutions précises, pas une certification de tous les modèles ou matériels.

| Vérification | Résultat observé | Portée |
| --- | --- | --- |
| Compilation complète Release | Réussie | Application et exécutables de tests sur la machine de développement. |
| CTest | **5 suites réussies** | WorkspaceAccess, AgentProtocol, AgentEngine, Runtime et BrowserTools. Les intégrations facultatives peuvent être ignorées dans le passage standard. |
| WorkspaceAccess | **18 réussites, 0 échec, 1 ignoré** | Accès aux dossiers et écritures protégées ; un scénario nécessitant des privilèges a été ignoré. |
| AgentProtocol | **34 réussites, 0 échec** | Protocole de génération et traitement des réponses du service local. |
| AgentEngine | **17 réussites, 0 échec, 0 ignoré** | Agent avec vrai courtier d’outils et fichiers temporaires ; appels natifs, repli JSON, confirmations, refus et limites. |
| Démarrage graphique réel | Code de sortie **0**, aucun avertissement QML | `--smoke-test --screenshot`, réglages de test isolés. Capture réelle dans `assets/screenshots/overview.png`. |
| Runtime final avec intégrations explicites | **25 réussites, 0 échec, 0 ignoré** | Installation/cache b12167 ; CPU et Vulkan sur RTX 5070 ; cycle de vie, catalogue, services locaux simulés, réflexion Qwen3 et échange d’outil natif. |
| BrowserTools avec intégration réelle | **3 cas QtTest réussis, 0 échec** | Initialisation, un scénario composé sur page locale maîtrisée, puis nettoyage. Ce ne sont pas trois scénarios de navigation indépendants. |
| Distribution Windows | Archive complète créée ; démarrage du binaire distribué : **sortie 0**, aucun avertissement QML | DLL Qt et MSVC, licences et sources Qt vérifiées. Essai avec PATH limité à Windows, sans chemins Qt de développement. Une machine Windows vierge reste à vérifier séparément. |

## Inférence gérée

Modèle utilisé : **SmolLM2-135M-Instruct-Q4_K_M.gguf**, 105 454 144 octets, provenant de `unsloth/SmolLM2-135M-Instruct-GGUF`, révision `9e6855bc4be717fca1ef21360a1db4b29d5c559a`. SHA-256 :

```text
ed5fa30c487b282ec156c29062f1222e5c20875a944ac98289dbd242e947f747
```

Le poids n’est ni versionné ni fourni avec la distribution. Pour les deux configurations, le prompt `Say hello in one short sentence.` a produit **`Hello!`**, avec **deux fragments de texte reçus en streaming**. Une requête sans authentification vers le serveur géré a reçu **HTTP 401**. Les tests finaux vérifient l’arrêt et la récupération des processus possédés lors de la destruction du moteur, ainsi que la reprise après annulation d’une installation, sans avertissement Qt de processus encore actif.

Un second modèle réel, **Qwen3-0.6B-Q4_0.gguf** (428 970 080 octets), provient de `ggml-org/Qwen3-0.6B-GGUF`, révision `13ac06624948e4d8589cb4b6f90100837342fcb8`. SHA-256 :

```text
da2572f16c06133561ce56accaa822216f2391ef4d37fba427801cd6736417d4
```

Les niveaux `auto`, `on` et `off` ont été annoncés par le moteur. Avec la réflexion désactivée, aucun fragment de raisonnement n’a été reçu ; avec la réflexion activée, le raisonnement a été transmis séparément de la réponse. Le modèle a ensuite émis un appel natif à l’outil local de test `validation_echo`, reçu son résultat réel et produit une réponse finale au tour suivant contenant `titan_probe`. Ce scénario vérifie le protocole d’outil sur deux tours, sans prétendre couvrir tous les outils avec ce modèle.

Les preuves locales sont dans `.local/runtime-validation/`, notamment `complete-runtime-tests.txt`, `managed-cpu.json`, `managed-automatic.json`, `model-source.json`, `qwen-model-source.json` et `qwen-thinking-tools.json`. Elles restent ignorées par Git pour éviter de distribuer journaux, chemins locaux ou poids.

Ces exécutions valident des générations courtes avec deux petits modèles. Elles ne mesurent pas la qualité générale des réponses, la consommation maximale, les modèles très volumineux, tous les découpages GGUF, tous les GPU ou un cycle prolongé d’utilisation.

## Agent et fichiers

Les **17 cas AgentEngine** utilisent le vrai courtier d’outils sur des fichiers temporaires, avec un service de génération local maîtrisé. Ils couvrent une lecture puis une modification approuvée avec empreinte, le repli JSON, le refus, l’annulation, l’historique et la limite d’étapes.

Un scénario lit un fichier de **50 Ko** dont le contenu envoyé au modèle est tronqué : les métadonnées structurées et l’empreinte du fichier complet sont conservées. L’agent peut alors effectuer un remplacement approuvé en vérifiant cette empreinte, avec création d’une sauvegarde. Cette vérification distingue le tronquage du contexte de la validité des protections d’écriture.

## Navigateur et interface

Le scénario BrowserTools utilise **BrowserControl v0.2.0**, un profil possédé par le test et une page HTTP servie sur loopback. Il vérifie les onglets, la navigation, l’observation, la saisie, le clic, la capture et les refus de pages locales ou d’actions interdites. Aucun compte ni site tiers réel n’a été utilisé pour ce scénario.

Runtime Windows observé : **Microsoft Edge WebView2 156.0.4314.58**. Le mode Evergreen utilise le runtime installé sur le système ; cette version est une observation du test, pas une version imposée à tous les utilisateurs. Voir la [documentation Microsoft sur sa distribution](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/distribution).

La capture graphique publiée correspond à l’application C++ réelle, sans conversation privée ni exemple de résultat inventé. Les rendus de développement QML ont également couvert le thème clair, les messages Markdown/code, les paramètres, la confirmation d’action et une fenêtre de 960 × 660 pixels.

Le passage final d’intégration navigateur a réussi sans avertissement de nettoyage de profil. Il vérifie aussi le refus d’actions sans garde URL/génération, les champs interdits, la conservation d’un reçu de plan partiellement exécuté et la fermeture de l’onglet actif suivie d’une nouvelle observation.

## Reproduire

Depuis un terminal de validation dédié :

```powershell
.\Build.ps1 -Configuration Release -Test
$env:PATH = "$PWD\.deps\Qt\6.8.3\msvc2022_64\bin;$env:PATH"

# Utiliser un modèle instruct acquis séparément ; aucun poids n’est téléchargé ici.
$env:TITAN_TEST_MODEL_PATH = 'D:\MesModeles\modele-instruct.gguf'
$env:TITAN_TEST_REASONING_MODEL_PATH = 'D:\MesModeles\Qwen3-0.6B-Q4_0.gguf'
$env:TITAN_TEST_INSTALL_RUNTIME = '1'
$env:TITAN_TEST_EVIDENCE_DIRECTORY = "$PWD\.local\runtime-validation"
.\out\build\Release\RuntimeTests.exe -v1

# Ouvre uniquement le navigateur et la page locale possédés par le test.
$env:TITAN_TEST_BROWSER = '1'
.\out\build\Release\BrowserToolsTests.exe
```

Le passage CTest standard ne déclenche pas ces intégrations réseau et matérielles sans leurs variables explicites. Le workflow Windows est configuré pour compiler, tester, empaqueter et lancer le binaire distribué avec des réglages de test isolés ; son existence ne prouve pas qu’il a déjà été exécuté sur GitHub.

Restent à vérifier séparément : le paquet sur une machine sans environnement de développement, de vrais serveurs Ollama et LM Studio installés, les sites externes, les grands modèles et les longues conversations avec outils.
