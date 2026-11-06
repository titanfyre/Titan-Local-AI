param([ValidateSet('Debug','Release')][string]$Configuration='Release',[switch]$Test,[switch]$Package)
$ErrorActionPreference='Stop'
$repoRoot=$PSScriptRoot
$qtRoot=Join-Path $repoRoot '.deps\Qt\6.8.3\msvc2022_64'
if (-not (Test-Path -LiteralPath (Join-Path $qtRoot 'lib\cmake\Qt6\Qt6Config.cmake'))) { & (Join-Path $repoRoot 'scripts\Bootstrap.ps1') }
$env:PATH=(Join-Path $qtRoot 'bin')+';'+$env:PATH
$buildRoot=Join-Path $repoRoot 'out\build'
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Installez Visual Studio 2022 Build Tools avec le composant C++ Desktop.' }
$vsRoot= & $vswhere -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw 'Compilateur MSVC C++ introuvable.' }
$toolset=Get-ChildItem -LiteralPath (Join-Path $vsRoot 'VC\Tools\MSVC') -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1 -ExpandProperty Name
& cmake -S $repoRoot -B $buildRoot -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$qtRoot" "-DCMAKE_GENERATOR_INSTANCE=$vsRoot" '-DBUILD_TESTING=ON' "-T v143,version=$toolset"
if ($LASTEXITCODE) { throw 'Configuration CMake impossible.' }
& cmake --build $buildRoot --config $Configuration --parallel -- "/p:VCToolsVersion=$toolset"
if ($LASTEXITCODE) { throw 'Compilation impossible.' }
if ($Test) { & ctest --test-dir $buildRoot -C $Configuration --output-on-failure; if ($LASTEXITCODE) { throw 'Échec des tests.' } }
if ($Package) { & (Join-Path $repoRoot 'scripts\Package.ps1') -Configuration $Configuration }
Write-Host "Application : $buildRoot\$Configuration\TitanLocalAI.exe"
