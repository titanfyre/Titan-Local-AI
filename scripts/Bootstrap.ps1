param([string]$QtVersion='6.8.3',[switch]$NoQt)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path $PSScriptRoot -Parent
$dependencyRoot=Join-Path $repoRoot '.deps'
New-Item -ItemType Directory -Path $dependencyRoot -Force | Out-Null
$qtRoot=Join-Path $dependencyRoot "Qt\$QtVersion\msvc2022_64"
if (-not $NoQt -and -not (Test-Path -LiteralPath (Join-Path $qtRoot 'lib\cmake\Qt6\Qt6Config.cmake'))) {
    $pythonCommand=Get-Command python -ErrorAction SilentlyContinue
    if (-not $pythonCommand) { throw 'Python 3.10+ est nécessaire pour préparer Qt. Installez Python puis relancez ce script.' }
    $pythonEnvironment=Join-Path $dependencyRoot 'python'
    if (-not (Test-Path -LiteralPath (Join-Path $pythonEnvironment 'Scripts\python.exe'))) { & $pythonCommand.Source -m venv $pythonEnvironment; if ($LASTEXITCODE) { throw 'Création de l’environnement de préparation impossible.' } }
    $pythonExe=Join-Path $pythonEnvironment 'Scripts\python.exe'
    & $pythonExe -m pip --disable-pip-version-check install 'aqtinstall==3.3.0'
    if ($LASTEXITCODE) { throw 'Installation du préparateur Qt impossible.' }
    & $pythonExe -m aqt install-qt windows desktop $QtVersion win64_msvc2022_64 -O (Join-Path $dependencyRoot 'Qt')
    if ($LASTEXITCODE) { throw 'Téléchargement de Qt impossible.' }
}
if (-not $NoQt) { Write-Output $qtRoot }
