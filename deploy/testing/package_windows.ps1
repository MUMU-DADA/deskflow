# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT
param(
    [string]$BuildDirectory = 'build',
    [string]$OutputDirectory = 'dist-test',
    [string]$Configuration = 'Release',
    [string]$OpenSslBin = '',
    [string]$RedistDirectory = $env:VCToolsRedistDir
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$out = (Resolve-Path -LiteralPath $OutputDirectory).Path
$stage = Join-Path $out 'deskflow-game-guard-windows-x64'
if (Test-Path -LiteralPath $stage) { throw "Refusing to overwrite existing package stage: $stage" }
New-Item -ItemType Directory -Path $stage | Out-Null
$bin = Join-Path $build 'bin'
if (!(Test-Path -LiteralPath (Join-Path $bin 'deskflow.exe'))) {
    $bin = Join-Path $bin $Configuration
}
foreach ($name in @('deskflow.exe', 'deskflow-core.exe', 'deskflow-game-probe.exe')) {
    Copy-Item -LiteralPath (Join-Path $bin $name) -Destination $stage
}
# Start with build's third-party runtime DLLs; windeployqt handles Qt/plugins.
Get-ChildItem -LiteralPath $bin -Filter '*.dll' | Copy-Item -Destination $stage
if ($OpenSslBin) {
    Get-ChildItem -LiteralPath $OpenSslBin -Filter '*.dll' | Where-Object { $_.Name -match '^lib(ssl|crypto)' } |
        Copy-Item -Destination $stage -Force
}
foreach ($name in @('deskflow.exe', 'deskflow-core.exe', 'deskflow-game-probe.exe')) {
    & windeployqt --release --no-compiler-runtime --no-system-d3d-compiler --no-opengl-sw --no-quick-import `
        --dir $stage --plugindir (Join-Path $stage 'plugins') (Join-Path $stage $name)
    if ($LASTEXITCODE -ne 0) { throw "windeployqt failed for $name" }
}
$translations = Join-Path $stage 'translations'
New-Item -ItemType Directory -Path $translations -Force | Out-Null
$applicationTranslations = @(Get-ChildItem -LiteralPath (Join-Path $build 'translations') -Filter 'deskflow_*.qm')
if (!$applicationTranslations.Count) { throw 'Compiled Deskflow translations are missing' }
$applicationTranslations | Copy-Item -Destination $translations -Force
# Only app-local redistributable DLLs from the official VS Redist folder;
# never copy compiler-bin or system32 DLLs. Windows 10 supplies the UCRT.
if (!$RedistDirectory -or !(Test-Path -LiteralPath $RedistDirectory)) {
    throw 'VCToolsRedistDir (official MSVC redistributable directory) is required'
}
$crt = Get-ChildItem -LiteralPath (Join-Path $RedistDirectory 'x64') -Directory |
    Where-Object { $_.Name -match '^Microsoft\.VC\d+\.CRT$' } | Select-Object -First 1
if (!$crt) { throw 'Unable to locate x64 Microsoft.VC*.CRT redistributable folder' }
Get-ChildItem -LiteralPath $crt.FullName -Filter '*.dll' | Copy-Item -Destination $stage -Force
foreach ($name in @('Qt6Core.dll', 'Qt6Widgets.dll', 'Qt6Network.dll', 'vcruntime140.dll', 'msvcp140.dll')) {
    if (!(Test-Path -LiteralPath (Join-Path $stage $name))) { throw "Missing runtime dependency: $name" }
}
foreach ($pattern in @('libssl*.dll', 'libcrypto*.dll')) {
    if (!(Get-ChildItem -LiteralPath $stage -Filter $pattern)) { throw "Missing OpenSSL runtime: $pattern" }
}
@'
[Paths]
Prefix=.
Plugins=plugins
Translations=translations
'@ | Set-Content -LiteralPath (Join-Path $stage 'qt.conf') -Encoding utf8

# A separate portable config prevents the full test app from modifying stable settings.
$settings = Join-Path $stage 'settings'
New-Item -ItemType Directory -Path $settings | Out-Null
@'
[core]
processMode=1
enableFullscreenGameGuard=true
[gui]
startCoreWithGui=false
'@ | Set-Content -LiteralPath (Join-Path $settings 'Deskflow.conf') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $repo 'deploy/testing/README.txt') -Destination $stage
Copy-Item -LiteralPath (Join-Path $repo 'deploy/testing/Run Game Probe.cmd') -Destination $stage
Copy-Item -LiteralPath (Join-Path $repo 'deploy/testing/Start Deskflow Test.cmd') -Destination $stage
Copy-Item -LiteralPath (Join-Path $repo 'LICENSE') -Destination $stage
Copy-Item -LiteralPath (Join-Path $repo 'LICENSES') -Destination $stage -Recurse
& git -C $repo rev-parse HEAD | Set-Content -LiteralPath (Join-Path $stage 'SOURCE-COMMIT.txt')

# Verify PE dependency closure against package files + OS libraries using dumpbin.
$systemRoot = Join-Path $env:WINDIR 'System32'
Get-ChildItem -LiteralPath $stage -Recurse -File | Where-Object { $_.Extension -in '.dll', '.exe' } | ForEach-Object {
    $binary = $_
    $listing = & dumpbin /DEPENDENTS $binary.FullName
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect $($binary.Name)" }
    foreach ($line in $listing) {
        if ($line -match '^\s+([^\s]+\.dll)\s*$') {
            $dependency = $Matches[1]
            if ($dependency -match '^(api-ms-|ext-ms-)') { continue }
            $local = Join-Path $stage $dependency
            $system = Join-Path $systemRoot $dependency
            if (!(Test-Path -LiteralPath $local) -and !(Test-Path -LiteralPath $system)) {
                throw "Unresolved dependency $dependency in $($binary.Name)"
            }
            # Qt/OpenSSL/MSVC must be app-local even if the CI machine has them installed.
            if ($dependency -match '^(Qt6|libssl|libcrypto|msvcp\d|vcruntime\d|concrt\d)' -and
                !(Test-Path -LiteralPath $local)) { throw "Runtime is not bundled: $dependency" }
        }
    }
}

$originalPath = $env:PATH
$originalStateHome = $env:XDG_STATE_HOME
$originalPlatform = $env:QT_QPA_PLATFORM
$isolatedQtKeys = @('QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QML2_IMPORT_PATH', 'QML_IMPORT_PATH')
$originalQtValues = @{}
foreach ($key in $isolatedQtKeys) { $originalQtValues[$key] = [Environment]::GetEnvironmentVariable($key, 'Process') }
try {
    foreach ($key in $isolatedQtKeys) { [Environment]::SetEnvironmentVariable($key, $null, 'Process') }
    $env:PATH = "$stage;$systemRoot;$env:WINDIR"
    $env:XDG_STATE_HOME = Join-Path $out 'smoke-state'
    # windeployqt ships qwindows, not the SDK's offscreen test plugin.
    # --version exits before core startup and creates no application window.
    $env:QT_QPA_PLATFORM = 'windows'
    & (Join-Path $stage 'deskflow-core.exe') --version
    if ($LASTEXITCODE -ne 0) { throw 'Packaged core --version failed' }
    $process = Start-Process -FilePath (Join-Path $stage 'deskflow-game-probe.exe') -ArgumentList '--help' `
        -Wait -PassThru -WindowStyle Hidden
    if ($process.ExitCode -ne 0) { throw 'Packaged probe --help failed' }
    $probeOutput = Join-Path $out 'probe-smoke.ndjson'
    $process = Start-Process -FilePath (Join-Path $stage 'deskflow-game-probe.exe') `
        -ArgumentList @('--samples', '1', '--output', "`"$probeOutput`"") -Wait -PassThru -WindowStyle Hidden
    if ($process.ExitCode -ne 0 -or !(Test-Path -LiteralPath $probeOutput)) { throw 'Packaged probe smoke failed' }
} finally {
    $env:PATH = $originalPath
    $env:XDG_STATE_HOME = $originalStateHome
    $env:QT_QPA_PLATFORM = $originalPlatform
    foreach ($key in $isolatedQtKeys) { [Environment]::SetEnvironmentVariable($key, $originalQtValues[$key], 'Process') }
}
$archive = Join-Path $out 'deskflow-game-guard-windows-x64.zip'
Compress-Archive -LiteralPath $stage -DestinationPath $archive
$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $([IO.Path]::GetFileName($archive))" | Set-Content -LiteralPath "$archive.sha256" -Encoding ascii
Write-Output "Created $archive"
