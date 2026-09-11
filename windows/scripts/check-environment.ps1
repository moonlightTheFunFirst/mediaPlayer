param(
    [string]$QtRoot = 'F:\Qt\6.8.3\msvc2022_64'
)
$ErrorActionPreference = 'Stop'
$windowsRoot = Split-Path $PSScriptRoot -Parent
$buildDir = Join-Path $windowsRoot 'build/environment-check-msvc'
if (-not (Test-Path (Join-Path $QtRoot 'lib/cmake/Qt6/Qt6Config.cmake'))) {
    throw "Qt MSVC kit not found: $QtRoot"
}
& cmake -S (Join-Path $windowsRoot 'environment-check') -B $buildDir `
    -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$QtRoot"
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
& cmake --build $buildDir --config Debug
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
$previousPath = $env:PATH
$previousPluginPath = $env:QT_PLUGIN_PATH
try {
    $env:PATH = "$(Join-Path $QtRoot 'bin');$previousPath"
    $env:QT_PLUGIN_PATH = Join-Path $QtRoot 'plugins'
    & (Join-Path $buildDir 'Debug/mediaPlayerEnvironmentCheck.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Qt initialization check failed.' }
} finally {
    $env:PATH = $previousPath
    $env:QT_PLUGIN_PATH = $previousPluginPath
}
