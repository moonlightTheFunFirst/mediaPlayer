param(
    [string]$QtRoot
)
$ErrorActionPreference = 'Stop'
$windowsRoot = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'build-environment.ps1')
$environment = Resolve-BuildEnvironment -QtRoot $QtRoot
$QtRoot = $environment.Qt
$sourceRoot = Join-Path $windowsRoot 'environment-check'
$buildDir = Get-EnvironmentBuildDirectory $windowsRoot 'environment-check-msvc' $environment $sourceRoot
& $environment.CMake -S $sourceRoot -B $buildDir -G $environment.Generator -A x64 -T v143 `
    "-DCMAKE_GENERATOR_INSTANCE=$($environment.VisualStudio)" "-DCMAKE_SYSTEM_VERSION=$($environment.Sdk)" "-DCMAKE_PREFIX_PATH=$QtRoot"
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
& $environment.CMake --build $buildDir --config Debug
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
