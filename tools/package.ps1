<#
.SYNOPSIS
    Builds a Release Beat Maker for Windows x64, runs the tests and packages it as an installer.

.DESCRIPTION
    The Windows counterpart of tools/package.sh. Produces, in dist\:
      Beat Maker-<version>-windows-x64\                 the staged app (what the installer installs)
        Beat Maker.exe, Beat Maker.pdb, assets\, docs\ (when built), LICENSE, README.md, CHANGELOG.md
      Beat Maker-<version>-windows-x64-setup.exe         the Inno Setup installer
      Beat Maker-<version>-windows-x64-setup.exe.sha256

    Needs Visual Studio 2022 or later with "Desktop development with C++" (it provides the compiler,
    CMake and Ninja; the script enters the VS developer environment by itself), and Inno Setup 6.3+
    for the installer step (winget install JRSoftware.InnoSetup).

    The build directory defaults to %LOCALAPPDATA%\BeatMaker\build-release, outside the source tree:
    a OneDrive-synced folder would upload gigabytes of objects and can lock files mid-build.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\package.ps1
.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\package.ps1 -SkipTests -BuildDir C:\bm\build
#>
[CmdletBinding()]
param(
    [switch] $SkipTests,
    [switch] $NoInstaller,
    [string] $BuildDir = (Join-Path $env:LOCALAPPDATA 'BeatMaker\build-release'),
    [string] $MkDocs = ''
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

function Invoke-Checked([string] $exe, [string[]] $arguments) {
    & $exe @arguments
    if ($LASTEXITCODE -ne 0) { throw "$exe failed with exit code $LASTEXITCODE" }
}

# --- Visual Studio developer environment (cl, link, cmake, ninja) -----------------------------
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw 'Visual Studio not found. Install Visual Studio 2022+ with "Desktop development with C++".' }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'Visual Studio has no C++ tools. Add the "Desktop development with C++" workload in the Visual Studio Installer.' }
    Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
}
foreach ($tool in 'cmake', 'ninja') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "$tool not found: add 'C++ CMake tools for Windows' in the Visual Studio Installer." }
}

# --- Version --------------------------------------------------------------------------------------
$cmakeLists = Get-Content (Join-Path $repo 'CMakeLists.txt') -Raw
$version = [regex]::Match($cmakeLists, 'set\(BEATMAKER_VERSION_STRING "([^"]+)"\)').Groups[1].Value
$numeric = [regex]::Match($cmakeLists, 'project\(BeatMaker VERSION ([0-9.]+)').Groups[1].Value
if (-not $version) { throw 'BEATMAKER_VERSION_STRING not found in CMakeLists.txt' }
$name = "Beat Maker-$version-windows-x64"
Write-Host "Packaging $name (build in $BuildDir)"

# --- Build and test -------------------------------------------------------------------------------
Invoke-Checked cmake @('-S', $repo, '-B', $BuildDir, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release')
Invoke-Checked cmake @('--build', $BuildDir)
if (-not $SkipTests) {
    Invoke-Checked ctest @('--test-dir', $BuildDir, '--output-on-failure', '-j', "$env:NUMBER_OF_PROCESSORS")
}
$artefacts = Join-Path $BuildDir 'ui\BeatMaker_artefacts\Release'
$exe = Join-Path $artefacts 'Beat Maker.exe'
if (-not (Test-Path $exe)) { throw "Build output missing: $exe" }

# --- Docs (optional: needs mkdocs) ----------------------------------------------------------------
if (-not $MkDocs) { $found = Get-Command mkdocs -ErrorAction SilentlyContinue; if ($found) { $MkDocs = $found.Source } }
if ($MkDocs) {
    $python = (Get-Command py, python -ErrorAction SilentlyContinue | Select-Object -First 1).Source
    Invoke-Checked $python @((Join-Path $repo 'tools\build_docs.py'), '--app', $exe, '--mkdocs', $MkDocs)
}

# --- Stage ----------------------------------------------------------------------------------------
$dist = Join-Path $repo 'dist'
$stage = Join-Path $dist $name
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item $exe $stage
$pdb = Join-Path $artefacts 'Beat Maker.pdb'
if (Test-Path $pdb) { Copy-Item $pdb $stage }   # function names in crash backtraces
Copy-Item (Join-Path $repo 'assets') (Join-Path $stage 'assets') -Recurse
$site = Join-Path $repo 'site'
if (Test-Path (Join-Path $site 'index.html')) { Copy-Item $site (Join-Path $stage 'docs') -Recurse }
foreach ($f in 'LICENSE', 'README.md', 'CHANGELOG.md') { Copy-Item (Join-Path $repo $f) $stage }
Write-Host "Staged $stage"

# --- Installer ------------------------------------------------------------------------------------
if ($NoInstaller) { return }
$iscc = @(
    (Get-Command iscc.exe -ErrorAction SilentlyContinue | ForEach-Object Source),
    (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
    (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe'),
    (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe')
) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $iscc) { throw 'Inno Setup not found. Install it (winget install JRSoftware.InnoSetup) or pass -NoInstaller.' }

$setupName = "$name-setup"
$isccArgs = @("/DAppVersion=$version", "/DNumericVersion=$numeric", "/DSourceDir=$stage",
              "/DOutputDir=$dist", "/DOutputBaseName=$setupName", '/Q')
$icon = Join-Path $BuildDir 'ui\BeatMaker_artefacts\JuceLibraryCode\icon.ico'
if (Test-Path $icon) { $isccArgs += "/DIconFile=$icon" }
Invoke-Checked $iscc ($isccArgs + (Join-Path $repo 'packaging\windows\beat-maker.iss'))

$setup = Join-Path $dist "$setupName.exe"
$hash = (Get-FileHash $setup -Algorithm SHA256).Hash.ToLower()
Set-Content -Path "$setup.sha256" -Value "$hash  $setupName.exe" -Encoding ascii -NoNewline
Write-Host "Packaged $setup"
Write-Host "$hash  $setupName.exe"
