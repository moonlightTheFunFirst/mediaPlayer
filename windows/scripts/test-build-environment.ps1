$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'build-environment.ps1')
function Assert-True($Value, $Message) { if (-not $Value) { throw $Message } }
function Assert-Fails($Action, $Pattern) {
    $message = $null
    try { & $Action | Out-Null } catch { $message = $_.Exception.Message }
    Assert-True ($message -and $message -like $Pattern) "Expected failure: $Pattern; got: $message"
}
$environment = Resolve-BuildEnvironment
Assert-True (Test-QtKit $environment.Qt) 'Selected Qt is incompatible'
if ($environment.Vlc) { Assert-True (Test-VlcKit $environment.Vlc) 'Selected VLC is incompatible' } else { Assert-Fails { Resolve-BuildEnvironment -RequireVlc } 'VLC 3.x x64 runtime not found*' }
Assert-True (-not (Test-X64Binary $PSCommandPath)) 'A script was accepted as an x64 executable'
$windowsRoot = Split-Path $PSScriptRoot -Parent
$first = Get-EnvironmentBuildDirectory $windowsRoot 'test' $environment $windowsRoot
$again = Get-EnvironmentBuildDirectory $windowsRoot 'test' $environment $windowsRoot
Assert-True ($first -eq $again) 'Build directory is not stable'
$moved = Get-EnvironmentBuildDirectory $windowsRoot 'test' $environment (Join-Path $windowsRoot 'relocated')
Assert-True ($first -ne $moved) 'Relocation reuses the old cache'
$changed = $environment.PSObject.Copy(); $changed.Qt = Join-Path $windowsRoot 'other-qt'
Assert-True ($first -ne (Get-EnvironmentBuildDirectory $windowsRoot 'test' $changed $windowsRoot)) 'Changed kit reuses the old cache'
Assert-Fails { Resolve-BuildEnvironment -QtRoot $windowsRoot } 'Invalid Qt kit:*'
$savedQtDir = $env:QT_DIR; $savedMingw = $env:MINGW_DIR; $savedQt = $env:QT_MSVC_DIR; $savedCmake = $env:ORANGE_CMAKE; $savedVlc = $env:ORANGE_VLC_DIR
try {
    $env:QT_DIR = $null
    $env:QT_MSVC_DIR = $windowsRoot
    # A command argument takes precedence over an environment override.
    $explicit = Resolve-BuildEnvironment -QtRoot $environment.Qt
    Assert-True ($explicit.Qt -eq $environment.Qt) 'Explicit Qt argument ignored'
    Assert-Fails { Resolve-BuildEnvironment } 'Invalid Qt kit:*'
    $env:QT_MSVC_DIR = $environment.Qt
    $env:ORANGE_CMAKE = Join-Path $windowsRoot 'missing-cmake.exe'
    Assert-Fails { Resolve-BuildEnvironment } 'Compatible CMake/toolchain pair not found*'
    if ($environment.Compiler -eq 'MinGW') {
        Assert-True ($environment.MinGW -and -not $environment.VisualStudio -and -not $environment.Sdk) 'MinGW unexpectedly requires Visual Studio/SDK'
        $args = @(Get-BuildConfigureArguments $environment)
        Assert-True ($args -contains 'MinGW Makefiles' -and $args -notcontains '-A' -and $args -notcontains '-T') 'MinGW received MSVC options'
        $env:ORANGE_CMAKE = $environment.CMake
        $env:MINGW_DIR = $windowsRoot
        Assert-Fails { Resolve-BuildEnvironment } 'Matching MinGW x64 GCC*'
        $env:MINGW_DIR = $savedMingw
    }
    $env:QT_DIR = $windowsRoot
    Assert-Fails { Resolve-BuildEnvironment } 'Invalid Qt kit:*'
    $env:QT_DIR = $null
    $env:ORANGE_CMAKE = $environment.CMake
    $env:ORANGE_VLC_DIR = $windowsRoot
    Assert-Fails { Resolve-BuildEnvironment -RequireVlc } 'Invalid VLC*'
    # PATH-independent discovery uses Qt/VS/registry/standard installation roots.
    $savedPath = $env:PATH
    try {
        $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
        $env:QT_MSVC_DIR = $null; $env:ORANGE_CMAKE = $null; $env:ORANGE_VLC_DIR = $null
        $automatic = Resolve-BuildEnvironment
        Assert-True ($automatic.Qt -and $automatic.CMake) 'Automatic discovery failed'
    } finally { $env:PATH = $savedPath }
} finally {
    $env:QT_DIR = $savedQtDir; $env:MINGW_DIR = $savedMingw; $env:QT_MSVC_DIR = $savedQt; $env:ORANGE_CMAKE = $savedCmake; $env:ORANGE_VLC_DIR = $savedVlc
}
Write-Host 'Build environment discovery tests passed.'
