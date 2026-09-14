param([ValidateSet('Run', 'Deploy')][string]$Mode = 'Run', [string]$QtRoot, [switch]$RequireVlc)
$ErrorActionPreference = 'Stop'
$previousPath = $env:PATH
$previousPluginPath = $env:QT_PLUGIN_PATH
$previousVcInstallDir = $env:VCINSTALLDIR
try {
    $windowsRoot = [IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
    $projectRoot = Split-Path $windowsRoot -Parent
    . (Join-Path $PSScriptRoot 'build-environment.ps1')
    $build = Get-OrangeBuildEnvironment -QtRoot $QtRoot
    $buildDir = Join-Path $windowsRoot ('build/' + $build.Name)
    $qtRoot = $build.QtRoot
    $deployTool = Join-Path $qtRoot 'bin/windeployqt.exe'
    $cmake = $build.CMake
    $vlcRoot = if ($env:ORANGE_VLC_DIR) { $env:ORANGE_VLC_DIR } else { Join-Path $env:ProgramFiles 'VideoLAN/VLC' }
    $hasVlc = Test-Path -LiteralPath (Join-Path $vlcRoot 'libvlc.dll')
    if ($hasVlc -or $RequireVlc -or $env:ORANGE_VLC_DIR) {
        foreach ($relative in @('libvlc.dll', 'libvlccore.dll', 'plugins/access/libdvdnav_plugin.dll', 'COPYING.txt')) {
            if (-not (Test-Path -LiteralPath (Join-Path $vlcRoot $relative))) { throw "DVD runtime missing: $vlcRoot/$relative. Set ORANGE_VLC_DIR to VLC 3.x x64." }
        }
        if ((Get-Item -LiteralPath (Join-Path $vlcRoot 'libvlc.dll')).VersionInfo.FileMajorPart -ne 3) { throw 'DVD playback requires VLC 3.x x64.' }
    } else { Write-Warning 'VLC is not installed: building with normal media playback only. DVD ISO requires VLC 3.x x64 (ORANGE_VLC_DIR).' }
    $appExe = Join-Path (Join-Path $buildDir $build.ExeSubdir) 'Orange.exe'
    $outputDir = Join-Path $windowsRoot 'output'
    foreach ($process in @(Get-Process Orange,mediaPlayer -ErrorAction SilentlyContinue)) {
        if ($process.Path -eq (Join-Path $buildDir 'Release/mediaPlayer.exe') -or ($Mode -eq 'Deploy' -and $process.Path -eq (Join-Path $outputDir 'mediaPlayer.exe')) -or $process.Path -eq $appExe -or ($Mode -eq 'Deploy' -and $process.Path -eq (Join-Path $outputDir 'Orange.exe'))) {
            throw 'This build/output is running. Close Media Player and retry.'
        }
    }
    Write-Host "Qt: $qtRoot"
    Write-Host "Build: $buildDir"
    & $cmake -S $projectRoot -B $buildDir @($build.Configure)
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
    & $cmake --build $buildDir --config Release --clean-first --parallel
    if ($LASTEXITCODE -ne 0) { throw 'Clean rebuild failed.' }
    $deployDir = Split-Path $appExe -Parent
    if ($Mode -eq 'Deploy') {
        $deployDir = Join-Path $windowsRoot ('build/deploy-' + [Guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $deployDir | Out-Null
        Copy-Item -LiteralPath $appExe -Destination $deployDir
    }
    # Run reuses the build folder. Remove only generated runtime paths managed here,
    # so files from an older, broader deployment cannot survive the new selection.
    if ($Mode -eq 'Run') {
        foreach ($relative in @('vlc', 'imageformats', 'iconengines', 'networkinformation', 'tls', 'Qt6Svg.dll', 'README.md', 'WINDOWS.md')) {
            $generatedPath = [IO.Path]::GetFullPath((Join-Path $deployDir $relative))
            $generatedRoot = [IO.Path]::GetFullPath($buildDir) + [IO.Path]::DirectorySeparatorChar
            if (-not $generatedPath.StartsWith($generatedRoot, [StringComparison]::OrdinalIgnoreCase)) { throw "Unsafe generated path: $generatedPath" }
            if (Test-Path -LiteralPath $generatedPath) {
                if ((Get-Item -LiteralPath $generatedPath).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Generated path must not be a link: $generatedPath" }
                Remove-Item -LiteralPath $generatedPath -Recurse -Force
            }
        }
    }
    # ICO is used for the app icon; PNG support is built into QtGui. Video preview
    # frames are decoded by Multimedia, not by the still-image format plugins.
    & $deployTool --release --force --compiler-runtime --translations ja --include-plugins ffmpegmediaplugin --skip-plugin-types iconengines,networkinformation,tls --exclude-plugins qgif,qicns,qjpeg,qsvg,qtga,qtiff,qwbmp,qwebp --dir $deployDir (Join-Path $deployDir 'Orange.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Qt runtime deployment failed.' }
    if ($hasVlc) {
        $vlcOutput = Join-Path $deployDir 'vlc'
        New-Item -ItemType Directory -Force -Path $vlcOutput | Out-Null
        foreach ($name in @('libvlc.dll', 'libvlccore.dll', 'COPYING.txt')) {
            Copy-Item -LiteralPath (Join-Path $vlcRoot $name) -Destination $vlcOutput -Force
        }
        # Keep playback/decoding and rendering fallbacks. Orange does not use VLC's
        # own GUI, streaming/encoding outputs, discovery, visualizers or remote control.
        $vlcPluginTypes = @('access', 'audio_filter', 'audio_mixer', 'audio_output',
            'codec', 'd3d11', 'd3d9', 'demux', 'logger', 'misc', 'packetizer', 'spu',
            'stream_filter', 'text_renderer', 'video_chroma', 'video_filter', 'video_output')
        $vlcPluginsOutput = Join-Path $vlcOutput 'plugins'
        New-Item -ItemType Directory -Force -Path $vlcPluginsOutput | Out-Null
        foreach ($type in $vlcPluginTypes) {
            Copy-Item -LiteralPath (Join-Path $vlcRoot "plugins/$type") -Destination $vlcPluginsOutput -Recurse -Force
        }
    }
    foreach ($relative in (@('Qt6Core.dll', 'Qt6Widgets.dll', 'Qt6Multimedia.dll', 'Qt6MultimediaWidgets.dll', 'platforms/qwindows.dll', 'multimedia/ffmpegmediaplugin.dll', 'avcodec-61.dll', 'avformat-61.dll', 'avutil-59.dll', 'swresample-5.dll', 'swscale-8.dll') + $build.Runtime)) {
        if (-not (Test-Path -LiteralPath (Join-Path $deployDir $relative))) { throw "Missing runtime: $relative" }
    }
    Set-Content -LiteralPath (Join-Path $deployDir 'qt.conf') -Value "[Paths]`nPrefix=.`nPlugins=." -Encoding ASCII
    Copy-Item -LiteralPath (Join-Path $windowsRoot 'THIRD_PARTY.md') -Destination $deployDir
    $licenseOutput = Join-Path $deployDir 'licenses'
    New-Item -ItemType Directory -Path $licenseOutput -Force | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $windowsRoot 'licenses') | Copy-Item -Destination $licenseOutput -Recurse -Force
    if ($build.MinGWRoot) {
        $compilerLicenses = Join-Path $licenseOutput 'MinGW'
        New-Item -ItemType Directory -Path $compilerLicenses -Force | Out-Null
        foreach ($component in @('gcc', 'mingw-w64', 'winpthreads')) {
            Copy-Item -LiteralPath (Join-Path $build.MinGWRoot "licenses/$component") -Destination $compilerLicenses -Recurse -Force
        }
    }
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
} finally {
    $env:PATH = $previousPath
    $env:QT_PLUGIN_PATH = $previousPluginPath
    $env:VCINSTALLDIR = $previousVcInstallDir
}
