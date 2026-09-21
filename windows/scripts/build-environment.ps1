# Shared discovery for Windows PowerShell 5.1. No installation or global PATH changes.
function Get-ExistingPaths($Paths) {
    @($Paths | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | ForEach-Object { [IO.Path]::GetFullPath($_) } | Select-Object -Unique)
}
function Get-ApplicationPath([string]$Name) {
    $command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) { $command.Source }
}
function Test-X64Binary([string]$Path) {
    try {
        $stream = [IO.File]::OpenRead($Path)
        $reader = New-Object IO.BinaryReader($stream)
        if ($reader.ReadUInt16() -ne 0x5a4d) { return $false }
        $stream.Position = 0x3c; $offset = $reader.ReadInt32()
        $stream.Position = $offset
        return $reader.ReadUInt32() -eq 0x4550 -and $reader.ReadUInt16() -eq 0x8664
    } catch { return $false } finally { if ($reader) { $reader.Dispose() } elseif ($stream) { $stream.Dispose() } }
}
function Get-QtCompiler([string]$Path) {
    $config = Join-Path $Path 'mkspecs/qconfig.pri'
    if (-not (Test-Path -LiteralPath $config)) { return }
    $content = Get-Content -LiteralPath $config -Raw
    if ($content -match '(?m)^QT_GCC_MAJOR_VERSION\s*=') { return 'MinGW' }
    if (Test-Path -LiteralPath (Join-Path $Path 'lib/Qt6Core.lib')) { return 'MSVC' }
}
function Test-QtKit([string]$Path) {
    if (-not (Get-QtCompiler $Path)) { return $false }
    foreach ($file in @('bin/Qt6Core.dll', 'bin/windeployqt.exe', 'bin/avcodec-61.dll', 'bin/avformat-61.dll',
        'bin/avutil-59.dll', 'bin/swscale-8.dll', 'bin/swresample-5.dll',
        'lib/cmake/Qt6/Qt6Config.cmake', 'lib/cmake/Qt6Widgets/Qt6WidgetsConfig.cmake',
        'lib/cmake/Qt6Multimedia/Qt6MultimediaConfig.cmake', 'lib/cmake/Qt6MultimediaWidgets/Qt6MultimediaWidgetsConfig.cmake',
        'plugins/platforms/qwindows.dll', 'plugins/multimedia/ffmpegmediaplugin.dll')) {
        if (-not (Test-Path -LiteralPath (Join-Path $Path $file))) { return $false }
    }
    foreach ($dll in @('Qt6Core.dll', 'avcodec-61.dll', 'avformat-61.dll', 'avutil-59.dll', 'swscale-8.dll', 'swresample-5.dll')) {
        if (-not (Test-X64Binary (Join-Path $Path "bin/$dll"))) { return $false }
    }
    $version = (Get-Item -LiteralPath (Join-Path $Path 'bin/Qt6Core.dll')).VersionInfo
    return $version.FileMajorPart -eq 6 -and $version.FileMinorPart -ge 8 -and (Test-X64Binary (Join-Path $Path 'bin/Qt6Core.dll'))
}
function Test-VlcKit([string]$Path) {
    foreach ($file in @('libvlc.dll', 'libvlccore.dll', 'plugins/access/libdvdnav_plugin.dll', 'COPYING.txt')) {
        if (-not (Test-Path -LiteralPath (Join-Path $Path $file))) { return $false }
    }
    return (Get-Item -LiteralPath (Join-Path $Path 'libvlc.dll')).VersionInfo.FileMajorPart -eq 3 -and (Test-X64Binary (Join-Path $Path 'libvlc.dll'))
}
function Resolve-BuildEnvironment([string]$QtRoot, [switch]$RequireVlc) {
    $roots = @($env:QTDIR, $env:QT_ROOT)
    foreach ($drive in Get-PSDrive -PSProvider FileSystem) { $roots += Join-Path $drive.Root 'Qt' }
    foreach ($base in @($env:ProgramFiles, ${env:ProgramFiles(x86)}, $env:LOCALAPPDATA, $env:USERPROFILE)) {
        if ($base) { $roots += Join-Path $base 'Qt' }
    }
    # Qt installer registration can point at a nonstandard installation root.
    foreach ($key in @('HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*',
        'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*',
        'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*')) {
        $roots += @(Get-ItemProperty $key -ErrorAction SilentlyContinue | Where-Object { $_.DisplayName -like 'Qt*' } | ForEach-Object { $_.InstallLocation })
    }
    $roots = Get-ExistingPaths $roots
    $qtCandidates = @()
    foreach ($name in @('qmake.exe', 'qtpaths.exe', 'windeployqt.exe')) {
        $exe = Get-ApplicationPath $name
        if ($exe) { $qtCandidates += Split-Path (Split-Path $exe -Parent) -Parent }
    }
    foreach ($prefix in @($env:CMAKE_PREFIX_PATH -split ';')) { if ($prefix) { $qtCandidates += $prefix } }
    foreach ($root in $roots) {
        $qtCandidates += $root
        $qtCandidates += @(Get-ChildItem -Path (Join-Path $root '6.*\msvc*'), (Join-Path $root '6.*\mingw*') -Directory -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
    }
    $explicitQt = if ($QtRoot) { $QtRoot } elseif ($env:QT_DIR) { $env:QT_DIR } else { $env:QT_MSVC_DIR }
    if ($explicitQt) {
        if (-not (Test-QtKit $explicitQt)) { throw "Invalid Qt kit: $explicitQt. Require Qt 6.8+ MSVC/MinGW x64, Multimedia and FFmpeg 7 ABI (61/61/59/8). Set QT_DIR." }
        $qt = [IO.Path]::GetFullPath($explicitQt)
    } else {
        $qt = Get-ExistingPaths $qtCandidates | Where-Object { Test-QtKit $_ } | Sort-Object -Property @{Expression={ [version](Get-Item -LiteralPath (Join-Path $_ 'bin/Qt6Core.dll')).VersionInfo.FileVersion }; Descending=$true}, @{Expression={$_}} | Select-Object -First 1
        if (-not $qt) { throw "Compatible Qt not found. Searched PATH, CMAKE_PREFIX_PATH and: $($roots -join ', '). Set QT_DIR to a Qt 6.8+ MSVC/MinGW x64 kit with Multimedia and FFmpeg 7 DLLs." }
    }
    $compiler = Get-QtCompiler $qt
    $mingw = $null
    $instances = @()
    if ($compiler -eq 'MSVC') {
        $vswhere = Get-ApplicationPath 'vswhere.exe'
        if (-not $vswhere) { $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe' }
        if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer/vswhere not found. Install Visual Studio or Build Tools with Desktop development with C++.' }
        $previousEncoding = [Console]::OutputEncoding
        try {
            [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
            $instances = @((& $vswhere -all -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -utf8 -format json | ConvertFrom-Json))
        } finally { [Console]::OutputEncoding = $previousEncoding }
    } else {
        $config = Get-Content -LiteralPath (Join-Path $qt 'mkspecs/qconfig.pri') -Raw
        $versionParts = foreach ($part in @('MAJOR', 'MINOR', 'PATCH')) {
            if ($config -notmatch "(?m)^QT_GCC_${part}_VERSION\s*=\s*(\d+)") { throw 'Qt compiler version metadata missing.' }
            $Matches[1]
        }
        $gccVersion = $versionParts -join '.'
        $toolsRoot = Join-Path (Split-Path (Split-Path $qt -Parent) -Parent) 'Tools'
        $mingwCandidates = @(Get-ChildItem -Path (Join-Path $toolsRoot 'mingw*') -Directory -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
        if ($env:MINGW_DIR) { $mingwCandidates = @($env:MINGW_DIR) }
        foreach ($candidate in $mingwCandidates) {
            $gxx = Join-Path $candidate 'bin/g++.exe'
            if (-not (Test-Path -LiteralPath $gxx) -or -not (Test-Path -LiteralPath (Join-Path $candidate 'bin/mingw32-make.exe'))) { continue }
            $actualVersion = & $gxx -dumpfullversion
            $target = & $gxx -dumpmachine
            if ($LASTEXITCODE -eq 0 -and $actualVersion -eq $gccVersion -and $target -eq 'x86_64-w64-mingw32') { $mingw = [IO.Path]::GetFullPath($candidate); break }
        }
        if (-not $mingw) { throw "Matching MinGW x64 GCC $gccVersion not found. Set MINGW_DIR to the compiler supplied with this Qt kit." }
    }
    $cmakeCandidates = @($env:ORANGE_CMAKE, (Get-ApplicationPath 'cmake.exe'))
    foreach ($root in $roots + @(Split-Path (Split-Path $qt -Parent) -Parent)) { $cmakeCandidates += Join-Path $root 'Tools/CMake_64/bin/cmake.exe' }
    $cmakeCandidates += Join-Path $env:ProgramFiles 'CMake/bin/cmake.exe'
    foreach ($instance in $instances) { $cmakeCandidates += Join-Path $instance.installationPath 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe' }
    if ($compiler -eq 'MinGW') { $cmakeCandidates = @((Join-Path $toolsRoot 'CMake_64/bin/cmake.exe')) + $cmakeCandidates }
    if ($env:ORANGE_CMAKE) { $cmakeCandidates = @($env:ORANGE_CMAKE) }
    $cmake = $null; $vs = $null; $generator = $null
    foreach ($candidate in (Get-ExistingPaths $cmakeCandidates)) {
        try { $capabilities = (& $candidate -E capabilities 2>$null | ConvertFrom-Json) } catch { continue }
        if ($LASTEXITCODE -ne 0 -or [version]$capabilities.version.string -lt [version]'3.21') { continue }
        if ($compiler -eq 'MinGW') {
            if ($capabilities.generators.name -contains 'MinGW Makefiles') { $cmake = $candidate; $generator = 'MinGW Makefiles'; break }
            continue
        }
        foreach ($instance in ($instances | Sort-Object { [version]$_.installationVersion } -Descending)) {
            $major = ([version]$instance.installationVersion).Major
            if ($major -lt 17) { continue }
            # The supported Qt MSVC 2022 kits need the v143 toolset even with a newer IDE.
            $toolsets = @(Get-ChildItem -Path (Join-Path $instance.installationPath 'VC/Tools/MSVC/14.*') -Directory -ErrorAction SilentlyContinue | Where-Object { ([version]$_.Name).Minor -ge 30 -and ([version]$_.Name).Minor -lt 50 })
            if (-not $toolsets.Count) { continue }
            $match = $capabilities.generators | Where-Object { $_.name -like "Visual Studio $major *" } | Select-Object -First 1
            if ($match) { $cmake = $candidate; $vs = $instance.installationPath; $generator = $match.name; break }
        }
        if ($cmake) { break }
    }
    if (-not $cmake) { throw 'Compatible CMake/toolchain pair not found. Require CMake 3.21+ with the selected generator. MSVC needs VS 2022+ with v143 x64 tools and Windows SDK. Set ORANGE_CMAKE for a custom cmake.exe.' }
    $sdk = $null
    if ($compiler -eq 'MSVC') {
        $sdkRoots = @()
        foreach ($key in @('HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots', 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows Kits\Installed Roots')) {
            $sdkRoots += (Get-ItemProperty $key -ErrorAction SilentlyContinue).KitsRoot10
        }
        $sdk = $null
        foreach ($root in (Get-ExistingPaths $sdkRoots)) {
            $sdk = Get-ChildItem -LiteralPath (Join-Path $root 'Include') -Directory -ErrorAction SilentlyContinue | Where-Object {
                (Test-Path -LiteralPath (Join-Path $_.FullName 'um/Windows.h')) -and
                (Test-Path -LiteralPath (Join-Path $root "Lib/$($_.Name)/um/x64/kernel32.lib")) -and
                (Test-Path -LiteralPath (Join-Path $root "Lib/$($_.Name)/ucrt/x64/ucrt.lib"))
            } | Sort-Object Name -Descending | Select-Object -First 1
            if ($sdk) { break }
        }
        if (-not $sdk) { throw 'Windows SDK x64 headers/libraries not found. Add a Windows 10/11 SDK through Visual Studio Installer.' }
    }
    $vlc = $null
    # Optional for normal media; an explicit override remains strict.
    $vlcCandidates = @($env:ORANGE_VLC_DIR)
    $vlcExe = Get-ApplicationPath 'vlc.exe'; if ($vlcExe) { $vlcCandidates += Split-Path $vlcExe -Parent }
    foreach ($key in @('HKLM:\SOFTWARE\VideoLAN\VLC', 'HKCU:\SOFTWARE\VideoLAN\VLC', 'HKLM:\SOFTWARE\WOW6432Node\VideoLAN\VLC')) {
        $vlcCandidates += (Get-ItemProperty $key -ErrorAction SilentlyContinue).InstallDir
    }
    foreach ($base in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) { if ($base) { $vlcCandidates += Join-Path $base 'VideoLAN/VLC' } }
    if ($env:ORANGE_VLC_DIR) {
        if (-not (Test-VlcKit $env:ORANGE_VLC_DIR)) { throw "Invalid VLC 3.x x64 directory: $env:ORANGE_VLC_DIR" }
        $vlc = [IO.Path]::GetFullPath($env:ORANGE_VLC_DIR)
    } else { $vlc = Get-ExistingPaths $vlcCandidates | Where-Object { Test-VlcKit $_ } | Select-Object -First 1 }
    if (-not $vlc -and $RequireVlc) { throw 'VLC 3.x x64 runtime not found in PATH, registry or Program Files. Set ORANGE_VLC_DIR.' }
    $qtVersion = (Get-Item -LiteralPath (Join-Path $qt 'bin/Qt6Core.dll')).VersionInfo.FileVersion
    $result = [pscustomobject]@{ Qt=$qt; QtVersion=$qtVersion; Compiler=$compiler; MinGW=$mingw; CMake=$cmake; CMakeVersion=$capabilities.version.string; VisualStudio=$vs; Generator=$generator; Toolset=$(if ($compiler -eq 'MSVC') { 'v143' } else { $gccVersion }); Sdk=$sdk.Name; Vlc=$vlc }
    $result | Format-List | Out-Host
    return $result
}
function Get-BuildConfigureArguments($Environment, [string]$Configuration = 'Release') {
    @('-G', $Environment.Generator, "-DCMAKE_PREFIX_PATH=$($Environment.Qt)")
    if ($Environment.Compiler -eq 'MinGW') {
        @("-DCMAKE_BUILD_TYPE=$Configuration", "-DCMAKE_CXX_COMPILER=$($Environment.MinGW)/bin/g++.exe", "-DCMAKE_MAKE_PROGRAM=$($Environment.MinGW)/bin/mingw32-make.exe")
    } else {
        @('-A', 'x64', '-T', $Environment.Toolset, "-DCMAKE_GENERATOR_INSTANCE=$($Environment.VisualStudio)", "-DCMAKE_SYSTEM_VERSION=$($Environment.Sdk)")
    }
}
function Get-EnvironmentBuildDirectory([string]$WindowsRoot, [string]$Purpose, $Environment, [string]$Source) {
    # A relocated checkout or changed toolchain gets an independent CMake cache.
    $identity = [IO.Path]::GetFullPath($Source) + '|' + ($Environment | ConvertTo-Json -Compress)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $key = ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($identity)))).Replace('-', '').Substring(0, 16).ToLowerInvariant() }
    finally { $sha.Dispose() }
    return Join-Path $WindowsRoot "build/$Purpose-$key"
}
