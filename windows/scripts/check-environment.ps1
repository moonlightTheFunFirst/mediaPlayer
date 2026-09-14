param([string]$QtRoot)
$ErrorActionPreference = 'Stop'
$previousPath = $env:PATH
$previousPluginPath = $env:QT_PLUGIN_PATH
$previousVcInstallDir = $env:VCINSTALLDIR
try {
    . (Join-Path $PSScriptRoot 'build-environment.ps1')
    $build = Get-OrangeBuildEnvironment -QtRoot $QtRoot
    $windowsRoot = Split-Path $PSScriptRoot -Parent
    $buildDir = Join-Path $windowsRoot ('build/environment-check-' + $build.Name)
    & $build.CMake -S (Join-Path $windowsRoot 'environment-check') -B $buildDir @($build.Configure)
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
    & $build.CMake --build $buildDir --config Release --parallel
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
    $env:QT_PLUGIN_PATH = Join-Path $build.QtRoot 'plugins'
    & (Join-Path (Join-Path $buildDir $build.ExeSubdir) 'mediaPlayerEnvironmentCheck.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Qt initialization check failed.' }
} finally {
    $env:PATH = $previousPath
    $env:QT_PLUGIN_PATH = $previousPluginPath
    $env:VCINSTALLDIR = $previousVcInstallDir
}
