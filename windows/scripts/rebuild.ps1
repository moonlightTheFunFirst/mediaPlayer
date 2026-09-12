param([ValidateSet('Run', 'Deploy')][string]$Mode = 'Run')
$ErrorActionPreference = 'Stop'
try {
    $windowsRoot = [IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
    $projectRoot = Split-Path $windowsRoot -Parent
    $buildDir = Join-Path $windowsRoot 'build/msvc-release'
    $qtRoot = if ($env:QT_MSVC_DIR) { $env:QT_MSVC_DIR } else { 'F:\Qt\6.8.3\msvc2022_64' }
    $qtBin = Join-Path $qtRoot 'bin'
    $deployTool = Join-Path $qtBin 'windeployqt.exe'
    $vlcRoot = if ($env:ORANGE_VLC_DIR) { $env:ORANGE_VLC_DIR } else { Join-Path $env:ProgramFiles 'VideoLAN/VLC' }
    foreach ($relative in @('libvlc.dll', 'libvlccore.dll', 'plugins/access/libdvdnav_plugin.dll', 'COPYING.txt')) {
        if (-not (Test-Path -LiteralPath (Join-Path $vlcRoot $relative))) { throw "DVD runtime missing: $vlcRoot/$relative. Set ORANGE_VLC_DIR to VLC 3.x x64." }
    }
    if ((Get-Item -LiteralPath (Join-Path $vlcRoot 'libvlc.dll')).VersionInfo.FileMajorPart -ne 3) { throw 'DVD playback requires VLC 3.x x64.' }
    if (-not (Test-Path -LiteralPath $deployTool)) { throw "Qt MSVC kit not found: $qtRoot. Set QT_MSVC_DIR." }
    $cmake = (Get-Command cmake.exe -ErrorAction SilentlyContinue).Source
    if (-not $cmake) {
        $cmake = 'F:\Qt\Tools\CMake_64\bin\cmake.exe'
        if (-not (Test-Path -LiteralPath $cmake)) { throw 'CMake not found. Add CMake to PATH.' }
    }
    # Keep the compiler and Qt runtime from the same MSVC x64 kit.
    $env:PATH = "$qtBin;$env:PATH"
    $env:QT_PLUGIN_PATH = Join-Path $qtRoot 'plugins'
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($vsRoot) { $env:VCINSTALLDIR = (Join-Path $vsRoot 'VC') + '\' }
    }
    $appExe = Join-Path $buildDir 'Release/Orange.exe'
    $outputDir = Join-Path $windowsRoot 'output'
    foreach ($process in @(Get-Process Orange,mediaPlayer -ErrorAction SilentlyContinue)) {
        if ($process.Path -eq (Join-Path $buildDir 'Release/mediaPlayer.exe') -or ($Mode -eq 'Deploy' -and $process.Path -eq (Join-Path $outputDir 'mediaPlayer.exe')) -or $process.Path -eq $appExe -or ($Mode -eq 'Deploy' -and $process.Path -eq (Join-Path $outputDir 'Orange.exe'))) {
            throw 'This build/output is running. Close Media Player and retry.'
        }
    }
    Write-Host "Qt: $qtRoot"
    Write-Host "Build: $buildDir"
    & $cmake -S $projectRoot -B $buildDir -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$qtRoot"
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
    & $cmake --build $buildDir --config Release --clean-first --parallel
    if ($LASTEXITCODE -ne 0) { throw 'Clean rebuild failed.' }
    $deployDir = Split-Path $appExe -Parent
    if ($Mode -eq 'Deploy') {
        $deployDir = Join-Path $windowsRoot ('build/deploy-' + [Guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $deployDir | Out-Null
        Copy-Item -LiteralPath $appExe -Destination $deployDir
    }
    & $deployTool --release --force --compiler-runtime --translations ja --include-plugins ffmpegmediaplugin --dir $deployDir (Join-Path $deployDir 'Orange.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Qt runtime deployment failed.' }
    $vlcOutput = Join-Path $deployDir 'vlc'
    New-Item -ItemType Directory -Force -Path $vlcOutput | Out-Null
    foreach ($name in @('libvlc.dll', 'libvlccore.dll', 'COPYING.txt')) {
        Copy-Item -LiteralPath (Join-Path $vlcRoot $name) -Destination $vlcOutput -Force
    }
    Copy-Item -LiteralPath (Join-Path $vlcRoot 'plugins') -Destination $vlcOutput -Recurse -Force
    foreach ($relative in @('Qt6Core.dll', 'Qt6Widgets.dll', 'Qt6Multimedia.dll', 'Qt6MultimediaWidgets.dll', 'platforms/qwindows.dll', 'multimedia/ffmpegmediaplugin.dll', 'avcodec-61.dll', 'avformat-61.dll', 'avutil-59.dll', 'swresample-5.dll', 'swscale-8.dll', 'vc_redist.x64.exe')) {
        if (-not (Test-Path -LiteralPath (Join-Path $deployDir $relative))) { throw "Missing runtime: $relative" }
    }
    Set-Content -LiteralPath (Join-Path $deployDir 'qt.conf') -Value "[Paths]`nPrefix=.`nPlugins=." -Encoding ASCII
    Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md') -Destination $deployDir
    Copy-Item -LiteralPath (Join-Path $windowsRoot 'THIRD_PARTY.md') -Destination $deployDir
    Copy-Item -LiteralPath (Join-Path $windowsRoot 'README.md') -Destination (Join-Path $deployDir 'WINDOWS.md')
    $licenseOutput = Join-Path $deployDir 'licenses'
    New-Item -ItemType Directory -Path $licenseOutput -Force | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $windowsRoot 'licenses') | Copy-Item -Destination $licenseOutput -Recurse -Force
    if ($Mode -eq 'Deploy') {
        $sha = [Security.Cryptography.SHA256]::Create()
        try {
            $originalHash = [Convert]::ToBase64String($sha.ComputeHash([IO.File]::ReadAllBytes($appExe)))
            $copiedHash = [Convert]::ToBase64String($sha.ComputeHash([IO.File]::ReadAllBytes((Join-Path $deployDir 'Orange.exe'))))
        } finally { $sha.Dispose() }
        if ($originalHash -ne $copiedHash) {
            throw 'Executable copy verification failed.'
        }
        # Only move known generated directories under this project's windows folder.
        foreach ($target in @($deployDir, $outputDir)) {
            $full = [IO.Path]::GetFullPath($target)
            if (-not $full.StartsWith($windowsRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw "Unsafe output path: $full" }
            if ((Test-Path -LiteralPath $full) -and ((Get-Item -LiteralPath $full).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Output must not be a directory link: $full" }
        }
        $backupDir = $null
        if (Test-Path -LiteralPath $outputDir) {
            $backupDir = Join-Path $windowsRoot ('build/output-backup-' + [Guid]::NewGuid().ToString('N'))
            Move-Item -LiteralPath $outputDir -Destination $backupDir
        }
        try { Move-Item -LiteralPath $deployDir -Destination $outputDir }
        catch {
            if ($backupDir -and -not (Test-Path -LiteralPath $outputDir)) { Move-Item -LiteralPath $backupDir -Destination $outputDir }
            throw
        }
        Write-Host "Deploy complete: $outputDir\Orange.exe"
        if ($backupDir) { Write-Host "Previous output retained: $backupDir" }
    } else {
        # Use the deployed plugins, matching direct execution of the resulting exe.
        $env:QT_PLUGIN_PATH = $null
        Start-Process -FilePath $appExe -WorkingDirectory $deployDir -WindowStyle Normal
        Write-Host "Started: $appExe"
    }
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
