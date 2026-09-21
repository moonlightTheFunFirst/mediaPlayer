param(
    [string]$QtRoot
)
$ErrorActionPreference = 'Stop'
$windowsRoot = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'build-environment.ps1')
$environment = Resolve-BuildEnvironment -QtRoot $QtRoot
$QtRoot = $environment.Qt
$sourceRoot = Join-Path $windowsRoot 'environment-check'
$buildDir = Get-EnvironmentBuildDirectory $windowsRoot ('environment-check-' + $environment.Compiler.ToLowerInvariant()) $environment $sourceRoot
$previousPath = $env:PATH
$previousPluginPath = $env:QT_PLUGIN_PATH
try {
    $env:PATH = "$(Join-Path $QtRoot 'bin');$previousPath"
    $env:QT_PLUGIN_PATH = Join-Path $QtRoot 'plugins'
    if ($environment.MinGW) { $env:PATH = "$(Join-Path $QtRoot 'bin');$($environment.MinGW)/bin;$previousPath" }
    & $environment.CMake -S $sourceRoot -B $buildDir @(Get-BuildConfigureArguments $environment)
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
    & $environment.CMake --build $buildDir --config Release
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
    $subdir = if ($environment.Compiler -eq 'MSVC') { 'Release' } else { '' }
    $probe = Start-Process -FilePath (Join-Path (Join-Path $buildDir $subdir) 'mediaPlayerEnvironmentCheck.exe') -WindowStyle Hidden -Wait -PassThru
    if ($probe.ExitCode -ne 0) { throw "Qt initialization check failed: $($probe.ExitCode)" }
} finally {
    $env:PATH = $previousPath
    $env:QT_PLUGIN_PATH = $previousPluginPath
}
