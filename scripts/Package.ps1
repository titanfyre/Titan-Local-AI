[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [string]$QtRoot = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($Configuration -ne 'Release') { throw 'Seule une compilation Release peut etre distribuee.' }
$repoRoot = [IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$qtVersion = '6.8.3'
if (-not $QtRoot) { $QtRoot = Join-Path $repoRoot ".deps\Qt\$qtVersion\msvc2022_64" }
$QtRoot = [IO.Path]::GetFullPath($QtRoot)
$utf8 = New-Object System.Text.UTF8Encoding($false)

function Assert-OwnedPath([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    $prefix = $repoRoot.TrimEnd('\') + '\'
    if (-not $full.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw "Chemin hors du depot refuse : $full" }
    $probe = $full
    while ($probe -and $probe.Length -ge $repoRoot.Length) {
        if (Test-Path -LiteralPath $probe) {
            $item = Get-Item -LiteralPath $probe -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Jonction ou lien refuse pour la sortie : $probe" }
        }
        if ($probe.Equals($repoRoot, [StringComparison]::OrdinalIgnoreCase)) { break }
        $probe = Split-Path $probe -Parent
    }
    return $full
}

function New-OwnedDirectory([string]$Path) {
    $checked = Assert-OwnedPath $Path
    New-Item -ItemType Directory -Path $checked -Force | Out-Null
    return $checked
}

function Write-Utf8([string]$Path, [string]$Text) {
    $checked = Assert-OwnedPath $Path
    [IO.File]::WriteAllText($checked, $Text, $utf8)
}

function Get-VerifiedQtSource([string]$Module, [string]$Sha256) {
    $name = "$Module-everywhere-src-$qtVersion.tar.xz"
    $cached = Assert-OwnedPath (Join-Path $sourceCache $name)
    $url = "https://download.qt.io/archive/qt/6.8/$qtVersion/submodules/$name"
    if (-not (Test-Path -LiteralPath $cached -PathType Leaf)) {
        Write-Host "Sources Qt : $name"
        $partial = Assert-OwnedPath ($cached + '.part-' + [guid]::NewGuid().ToString('N'))
        Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $partial -TimeoutSec 600
        if ((Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash -ne $Sha256) { throw "Empreinte incorrecte : $name. Fichier temporaire conserve : $partial" }
        if (Test-Path -LiteralPath $cached) { throw "Le cache a change pendant le telechargement : $cached" }
        Move-Item -LiteralPath $partial -Destination $cached
    }
    if ((Get-FileHash -LiteralPath $cached -Algorithm SHA256).Hash -ne $Sha256) { throw "Cache Qt invalide : $cached. Aucun fichier existant n'a ete supprime." }
    return [pscustomobject]@{ Path = $cached; Name = $name; Url = $url; Sha256 = $Sha256 }
}

function Find-VisualCppRuntime {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'vswhere.exe introuvable. Installez Visual Studio 2022 avec les outils C++ x64.' }
    $installation = & $vswhere -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
    if (-not $installation) { throw 'Visual Studio 2022 C++ introuvable.' }
    $redistRoot = Join-Path $installation 'VC\Redist\MSVC'
    $versions = @(Get-ChildItem -LiteralPath $redistRoot -Directory | Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } | Sort-Object { [version]$_.Name } -Descending)
    foreach ($version in $versions) {
        $crt = @(Get-ChildItem -LiteralPath (Join-Path $version.FullName 'x64') -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '^Microsoft\.VC\d+\.CRT$' } | Select-Object -First 1)
        if ($crt.Count -gt 0) { return [pscustomobject]@{ Installation = $installation; Redist = $version.FullName; Crt = $crt[0].FullName } }
    }
    throw 'DLL redistribuables MSVC x64 Release introuvables.'
}

$appBinary = Join-Path $repoRoot "out\build\$Configuration\TitanLocalAI.exe"
$deployTool = Join-Path $QtRoot 'bin\windeployqt.exe'
if (-not (Test-Path -LiteralPath $appBinary -PathType Leaf)) { throw "Compilez d'abord : .\Build.ps1 -Configuration Release. Binaire absent : $appBinary" }
if (-not (Test-Path -LiteralPath $deployTool -PathType Leaf)) { throw "windeployqt introuvable : $deployTool" }
$pythonCommand = Get-Command python -ErrorAction SilentlyContinue
if (-not $pythonCommand) { throw 'Python 3.10+ est requis pour extraire les textes de licences Qt.' }
$qconfigPath = Join-Path $QtRoot 'mkspecs\qconfig.pri'
$qtConfig = Get-Content -Raw -LiteralPath $qconfigPath
if ($qtConfig -notmatch 'QT_VERSION\s*=\s*6\.8\.3' -or $qtConfig -notmatch 'QT_CONFIG\s*\+=.*\bshared\b') { throw 'Le paquet doit utiliser Qt 6.8.3 dynamique.' }
$cmakeText = Get-Content -Raw -LiteralPath (Join-Path $repoRoot 'CMakeLists.txt')
$versionMatch = [regex]::Match($cmakeText, 'project\(TitanLocalAI\s+VERSION\s+([0-9.]+)')
if (-not $versionMatch.Success) { throw 'Version Titan introuvable dans CMakeLists.txt.' }
$appVersion = $versionMatch.Groups[1].Value
$outputRoot = New-OwnedDirectory (Join-Path $repoRoot 'out\packages')
$unique = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0,6)
$packageName = "Titan-Local-AI-$appVersion-windows-x64-$unique"
$packageRoot = New-OwnedDirectory (Join-Path $outputRoot $packageName)
$sourceCache = New-OwnedDirectory (Join-Path $repoRoot ".deps\qt-sources\$qtVersion")
$licensesRoot = New-OwnedDirectory (Join-Path $packageRoot 'licenses')
$qtLicenses = New-OwnedDirectory (Join-Path $licensesRoot 'qt')
$qtSources = New-OwnedDirectory (Join-Path $qtLicenses 'sources')
$qtSboms = New-OwnedDirectory (Join-Path $qtLicenses 'sbom')
$runtime = Find-VisualCppRuntime

# The staging directory starts empty. Never copy the build tree, user data or weights.
Copy-Item -LiteralPath $appBinary -Destination (Join-Path $packageRoot 'TitanLocalAI.exe')
$savedPath = $env:PATH
$savedVcInstall = $env:VCINSTALLDIR
$savedVcRedist = $env:VCToolsRedistDir
try {
    $env:PATH = (Join-Path $QtRoot 'bin') + ';' + $env:PATH
    $env:VCINSTALLDIR = (Join-Path $runtime.Installation 'VC') + '\'
    $env:VCToolsRedistDir = $runtime.Redist + '\'
    & $deployTool --release --verbose 0 --compiler-runtime --no-translations --skip-plugin-types qmltooling,generic --qmldir (Join-Path $repoRoot 'qml') --qmlimport (Join-Path $QtRoot 'qml') --dir $packageRoot (Join-Path $packageRoot 'TitanLocalAI.exe')
    if ($LASTEXITCODE -ne 0) { throw "windeployqt a echoue : $LASTEXITCODE" }
} finally {
    $env:PATH = $savedPath
    $env:VCINSTALLDIR = $savedVcInstall
    $env:VCToolsRedistDir = $savedVcRedist
}

# App-local CRT keeps the archive runnable without an administrator installer.
Get-ChildItem -LiteralPath $runtime.Crt -Filter '*.dll' -File | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $packageRoot -Force }
foreach ($required in @('Qt6Core.dll','Qt6Quick.dll','vcruntime140.dll','vcruntime140_1.dll','msvcp140.dll','platforms\qwindows.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $packageRoot $required) -PathType Leaf)) { throw "Dependance portable manquante : $required" }
}
Write-Utf8 (Join-Path $packageRoot 'qt.conf') "[Paths]`nPrefix=.`nPlugins=.`nQmlImports=qml`n"
foreach ($document in @('README.md','THIRD_PARTY_NOTICES.md','LICENSE','Launch.bat')) { Copy-Item -LiteralPath (Join-Path $repoRoot $document) -Destination $packageRoot }
$packageDocs = New-OwnedDirectory (Join-Path $packageRoot 'docs')
Copy-Item -LiteralPath (Join-Path $repoRoot 'docs\VALIDATION.md') -Destination $packageDocs
$packageScreenshots = New-OwnedDirectory (Join-Path $packageRoot 'assets\screenshots')
Copy-Item -LiteralPath (Join-Path $repoRoot 'assets\screenshots\overview.png') -Destination $packageScreenshots
Copy-Item -LiteralPath (Join-Path $repoRoot 'third_party\browser_control\LICENSE') -Destination (Join-Path $licensesRoot 'Browser-Control-SDK-MIT.txt')
Copy-Item -LiteralPath (Join-Path $repoRoot 'third_party\nlohmann\LICENSE') -Destination (Join-Path $licensesRoot 'nlohmann-json-MIT.txt')
Copy-Item -LiteralPath (Join-Path $repoRoot 'src\runtime\runtime-manifest.json') -Destination (Join-Path $licensesRoot 'llama.cpp-runtime-manifest.json')
Copy-Item -LiteralPath $qconfigPath -Destination (Join-Path $qtLicenses 'qconfig.pri')

# Pinned checksums from download.qt.io's matching .sha256 files.
$sources = [ordered]@{
    qtbase = '56001b905601bb9023d399f3ba780d7fa940f3e4861e496a7c490331f49e0b80'
    qtdeclarative = '1f03a2b8f5588b4face7da87926e9b2c1372b3a32157c52df07a75067a9db1af'
    qtsvg = '35eb516460f00f264eb504baa253432384351cf23fb9980a5857190e8deef438'
    qttranslations = 'c3c61d79c3d8fe316a20b3617c64673ce5b5519b2e45535f49bee313152fa531'
}
$sourceManifest = New-Object System.Collections.Generic.List[object]
foreach ($module in $sources.Keys) {
    $source = Get-VerifiedQtSource $module $sources[$module]
    Copy-Item -LiteralPath $source.Path -Destination $qtSources
    $moduleLicenses = New-OwnedDirectory (Join-Path $qtLicenses $module)
    # Use Python's standard xz reader: the tar.exe shipped by older Windows can hang on xz.
    @'
import pathlib, shutil, sys, tarfile
root = pathlib.Path(sys.argv[2]).resolve()
prefix = sys.argv[3].rstrip('/') + '/'
count = 0
with tarfile.open(sys.argv[1], 'r:xz') as archive:
    for member in archive:
        if not member.name.startswith(prefix):
            continue
        if member.isdir():
            continue
        relative = pathlib.PurePosixPath(member.name[len(prefix):])
        target = root.joinpath(*relative.parts).resolve()
        if not member.isfile() or member.size > 1024 * 1024 or not target.is_relative_to(root):
            raise RuntimeError('Unexpected Qt license archive entry: ' + member.name)
        target.parent.mkdir(parents=True, exist_ok=True)
        with archive.extractfile(member) as source, target.open('wb') as output:
            shutil.copyfileobj(source, output)
        count += 1
if count == 0:
    raise RuntimeError('Qt license directory was not found')
print('Qt license texts: ' + str(count))
'@ | & $pythonCommand.Source - $source.Path $moduleLicenses "$module-everywhere-src-$qtVersion/LICENSES"
    if ($LASTEXITCODE -ne 0) { throw "Extraction des licences impossible : $module" }
    $sbom = Join-Path $QtRoot "sbom\$module-$qtVersion.spdx.json"
    if (-not (Test-Path -LiteralPath $sbom -PathType Leaf)) { throw "Inventaire de licences Qt absent : $sbom" }
    Copy-Item -LiteralPath $sbom -Destination $qtSboms
    $sourceManifest.Add([pscustomobject]@{ module = $module; version = $qtVersion; archive = $source.Name; sha256 = $source.Sha256; url = $source.Url })
}
foreach ($licenseName in @('LGPL-3.0-only.txt','GPL-3.0-only.txt')) {
    Copy-Item -LiteralPath (Join-Path $qtLicenses "qtbase\$licenseName") -Destination $qtLicenses
}
Write-Utf8 (Join-Path $qtLicenses 'sources.json') ($sourceManifest | ConvertTo-Json -Depth 4)
Write-Utf8 (Join-Path $qtLicenses 'LIRE-MOI.txt') @'
Titan Local AI utilise les bibliotheques Qt 6.8.3 en liaison dynamique sous LGPLv3.
Les textes LGPLv3 et GPLv3 accompagnent ce fichier. Les sous-dossiers par module
contiennent les autres textes de licences ; sbom/ conserve les attributions Qt.
sources/ contient les archives sources officielles correspondantes, sans modification.
sources.json indique leurs URL d'origine et empreintes. qconfig.pri decrit le paquet Qt.
Vous pouvez modifier Qt, recompiler et remplacer ses DLL/plugins par des versions
x64 compatibles. Titan ne bloque pas ce remplacement. Voir THIRD_PARTY_NOTICES.md.
Conservez ces fichiers et archives sources avec toute redistribution des binaires.
'@

$microsoftRoot = New-OwnedDirectory (Join-Path $licensesRoot 'microsoft')
Write-Utf8 (Join-Path $microsoftRoot 'VC-RUNTIME-NOTICE.txt') @"
Microsoft Visual C++ Runtime x64 Release
Copyright Microsoft Corporation. Tous droits reserves.
DLL redistribuables copiees sans modification depuis Visual Studio 2022 :
$($runtime.Crt)
La redistribution est soumise aux conditions Microsoft applicables au distributeur.
Conditions : https://visualstudio.microsoft.com/license-terms/vs2022-cruntime/
Liste : https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution
Installation et mises a jour : https://aka.ms/vs/17/release/vc_redist.x64.exe
Les DLL fournies sont locales a cette application ; aucun installateur n'est execute.
"@
$redistList = Get-ChildItem -LiteralPath (Join-Path $runtime.Installation 'Licenses') -Filter 'Redist.txt' -File -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
if ($redistList) { Copy-Item -LiteralPath $redistList.FullName -Destination (Join-Path $microsoftRoot 'Visual-Studio-REDIST.txt') }

# Fail closed if an unexpected weight, private data folder or test file appears.
$unexpected = @(Get-ChildItem -LiteralPath $packageRoot -Recurse -Force | Where-Object {
    ($_.PSIsContainer -and $_.Parent.FullName -eq $packageRoot -and $_.Name -in @('models','chats','browser-profile','workspace-backups','test-artifacts')) -or
    (-not $_.PSIsContainer -and ($_.Extension -in @('.gguf','.safetensors','.onnx','.pdb') -or $_.Name -match '^(settings\.json|.*Tests\.exe|titan-smoke\.txt)$'))
})
if ($unexpected.Count -gt 0) { throw "Contenu inattendu dans le paquet : $($unexpected[0].FullName)" }
$files = @(Get-ChildItem -LiteralPath $packageRoot -Recurse -File | ForEach-Object {
    [pscustomobject]@{ path = $_.FullName.Substring($packageRoot.Length + 1).Replace('\','/'); bytes = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
})
Write-Utf8 (Join-Path $packageRoot 'package-manifest.json') (@{ application = 'Titan Local AI'; version = $appVersion; qt = $qtVersion; architecture = 'x64'; configuration = $Configuration; files = $files } | ConvertTo-Json -Depth 5)
$archivePath = Assert-OwnedPath (Join-Path $outputRoot "$packageName.zip")
Compress-Archive -LiteralPath $packageRoot -DestinationPath $archivePath -CompressionLevel Optimal
$archiveHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Utf8 ($archivePath + '.sha256') "$archiveHash  $packageName.zip`n"
Write-Host "Dossier portable : $packageRoot"
Write-Host "Archive : $archivePath"
Write-Output $packageRoot
