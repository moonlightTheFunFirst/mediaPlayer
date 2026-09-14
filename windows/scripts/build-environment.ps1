# Shared discovery for the build and the Qt initialization check.
function Get-OrangeBuildEnvironment {
    param([string]$QtRoot)
    if (-not $QtRoot) { $QtRoot = $env:QT_DIR }
    if (-not $QtRoot) { $QtRoot = $env:QT_MSVC_DIR }
    if (-not $QtRoot) {
        $candidates = @(foreach ($drive in Get-PSDrive -PSProvider FileSystem) {
            Get-ChildItem -Path (Join-Path $drive.Root 'Qt/6.*/msvc*_64'), (Join-Path $drive.Root 'Qt/6.*/mingw_64') -Directory -ErrorAction SilentlyContinue
        })
        $QtRoot = $candidates | Where-Object { [version]$_.Parent.Name -ge [version]'6.8' } | Sort-Object { [version]$_.Parent.Name } -Descending | Select-Object -First 1 -ExpandProperty FullName
    }
    if (-not $QtRoot -or -not (Test-Path -LiteralPath "$QtRoot/lib/cmake/Qt6/Qt6Config.cmake")) {
        throw 'Qt 6.8+ kit not found. Set QT_DIR to a Qt MSVC x64 or MinGW x64 kit.'
    }
    $QtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
    $qtInstall = Split-Path (Split-Path $QtRoot -Parent) -Parent
    # Prefer Qt's native Windows CMake over unrelated MSYS tools on PATH.
    $cmake = Join-Path $qtInstall 'Tools/CMake_64/bin/cmake.exe'
    if (-not (Test-Path -LiteralPath $cmake)) { $cmake = (Get-Command cmake.exe -ErrorAction Stop).Source }
    $isMinGW = Test-Path -LiteralPath "$QtRoot/mkspecs/win32-g++"
    $isMinGW = $isMinGW -and (Test-Path -LiteralPath "$QtRoot/bin/libstdc++-6.dll")
    $configure = @("-DCMAKE_PREFIX_PATH=$QtRoot", "-DQt6_DIR=$QtRoot/lib/cmake/Qt6")
    $toolBin = Join-Path $QtRoot 'bin'
    if ($isMinGW) {
        # Official Qt 6.8 Windows kits use MinGW 13.1. Other toolchains require an explicit override.
        $mingwRoot = if ($env:MINGW_DIR) { $env:MINGW_DIR } else { Join-Path $qtInstall 'Tools/mingw1310_64' }
        foreach ($tool in @('g++.exe', 'mingw32-make.exe')) {
            if (-not (Test-Path -LiteralPath "$mingwRoot/bin/$tool")) { throw "Missing $tool. Set MINGW_DIR to the compiler supplied with this Qt kit." }
        }
        $toolBin += ";$mingwRoot/bin"
        $configure += @('-G', 'MinGW Makefiles', '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_CXX_COMPILER=$mingwRoot/bin/g++.exe", "-DCMAKE_MAKE_PROGRAM=$mingwRoot/bin/mingw32-make.exe")
        $name = 'mingw-release'
        $exeSubdir = ''
        $runtime = @('libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll')
    } else {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio 2022 C++ x64 tools are required for this Qt kit.' }
        $vsRoot = & $vswhere -latest -version '[17,18)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if (-not $vsRoot) { throw 'Visual Studio 2022 C++ x64 tools are required for this Qt kit.' }
        $env:VCINSTALLDIR = (Join-Path $vsRoot 'VC') + '\'
        $configure += @('-G', 'Visual Studio 17 2022', '-A', 'x64', "-DCMAKE_GENERATOR_INSTANCE=$vsRoot")
        $name = 'msvc-release'
        $exeSubdir = 'Release'
        $runtime = @('vc_redist.x64.exe')
    }
    $env:PATH = "$toolBin;$env:PATH"
    $env:QT_PLUGIN_PATH = Join-Path $QtRoot 'plugins'
    [pscustomobject]@{ QtRoot = $QtRoot; CMake = $cmake; Configure = $configure; Name = $name; ExeSubdir = $exeSubdir; Runtime = $runtime; MinGWRoot = $mingwRoot }
}
