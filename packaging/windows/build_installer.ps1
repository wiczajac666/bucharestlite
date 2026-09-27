# Builds the Bucharest Light Windows release (with tests) and produces the
# NSIS installer + portable zip.
#
# Requirements (run on a Windows x64 machine):
#   - Visual Studio 2022 Build Tools (VCTools workload) already installed, or
#     pass -VcVarsBat pointing at vcvars64.bat.
#   - Ninja, CMake, git on PATH (installed via Chocolatey in the CI VM).
#   - Qt 6 for MSVC at $env:QT_ROOT  (e.g. C:\Qt\6.8.3\msvc2022_64)
#   - FFmpeg shared build (Gyan full_build-shared) at $env:FFMPEG_ROOT
#   - NSIS on PATH (makensis.exe)
#
# Example:
#   $env:QT_ROOT   = 'C:\Qt\6.8.3\msvc2022_64'
#   $env:FFMPEG_ROOT = 'C:\dev\ffmpeg-gyan\ffmpeg-9.0.2-full_build-shared'
#   .\build_installer.ps1
#
# Produces, in packaging\windows\:
#   bucharest-lite-1.1.1-windows-x64-setup.exe
#   bucharest-lite-1.1.1-windows-x64-portable.zip

param(
    [string]$Preset = 'windows-release',
    [string]$QtRoot = $env:QT_ROOT,
    [string]$FfmpegRoot = $env:FFMPEG_ROOT,
    [string]$VcVarsBat = ''
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$Ps1Dir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent (Split-Path -Parent $Ps1Dir)
$BuildDir = Join-Path $RepoRoot "build\windows\$Preset"
$Version = '1.1.1'

if (-not $QtRoot)     { throw 'QT_ROOT not set.' }
if (-not $FfmpegRoot) { throw 'FFMPEG_ROOT not set.' }

$QtBin          = Join-Path $QtRoot 'bin'
$Windeployqt    = Join-Path $QtBin 'windeployqt.exe'
$FfmpegBin      = Join-Path $FfmpegRoot 'bin'
$StageDir       = Join-Path $Ps1Dir 'staging'
$ExeFile        = Join-Path $BuildDir "src\ui\bucharest-lite.exe"

if (-not $VcVarsBat) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $vsInstall = & $vswhere -latest -products '*' `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath
        if (-not $vsInstall) {
            throw 'Visual Studio Build Tools (VCTools workload) not found.'
        }
        $VcVarsBat = Join-Path $vsInstall 'VC\Auxiliary\Build\vcvars64.bat'
    }
}
if (-not (Test-Path $VcVarsBat)) { throw "vcvars64.bat not found at $VcVarsBat" }

$Makensis = (Get-Command 'makensis.exe' -ErrorAction SilentlyContinue).Source
if (-not $Makensis) {
    foreach ($cand in @('C:\Program Files (x86)\NSIS\makensis.exe',
                        'C:\Program Files\NSIS\makensis.exe')) {
        if (Test-Path $cand) { $Makensis = $cand; break }
    }
}
if (-not $Makensis) { throw 'makensis.exe not found (install NSIS).' }

function Invoke-VcEnv([string]$CommandLine) {
    $bat = Join-Path $env:TEMP ("bl_build_" + $PID + ".cmd")
    $content = "@call `"$VcVarsBat`"`r`n" +
               "@cd /d `"$RepoRoot`"`r`n" +
               "@$CommandLine`r`n" +
               "@exit /b %errorlevel%"
    Set-Content -Path $bat -Value $content -Encoding Ascii
    try {
        & $env:ComSpec /d /s /c "`"$bat`""
    } finally {
        Remove-Item $bat -Force -ErrorAction SilentlyContinue
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed (exit $LASTEXITCODE): $CommandLine"
    }
}

Write-Host "==> Configuring & building (preset $Preset)"
$env:QT_ROOT = $QtRoot
$env:FFMPEG_ROOT = $FfmpegRoot
$env:PATH = "$FfmpegBin;$QtBin;$env:PATH"
Invoke-VcEnv "cd /d `"$RepoRoot`" && cmake --preset $Preset"
Invoke-VcEnv "cd /d `"$RepoRoot`" && cmake --build --preset $Preset"

Write-Host "==> Running tests"
Invoke-VcEnv "cd /d `"$RepoRoot`" && ctest --preset $Preset"

if (-not (Test-Path $ExeFile)) { throw "Built exe not found at $ExeFile" }

Write-Host "==> Staging deployable tree at $StageDir"
Remove-Item -Recurse -Force $StageDir -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $StageDir | Out-Null

Copy-Item $ExeFile $StageDir

Write-Host "==> Staging license files"
Copy-Item (Join-Path $RepoRoot 'LICENSE') $StageDir
Copy-Item (Join-Path $RepoRoot 'THIRD_PARTY_NOTICES.md') $StageDir

Write-Host "==> windeployqt (Qt runtime + platform plugins)"
& $Windeployqt --release --no-translations "$StageDir\bucharest-lite.exe"
if ($LASTEXITCODE -ne 0) { throw 'windeployqt failed.' }

Write-Host "==> Copying FFmpeg runtime DLLs (incl. MinGW-w64 dependencies)"
$ffmpegDlls = @('avcodec-*.dll','avformat-*.dll','avutil-*.dll',
                'swscale-*.dll','swresample-*.dll',
                'libgcc_s_seh-1.dll','libstdc++-6.dll','libwinpthread-1.dll')
foreach ($pat in $ffmpegDlls) {
    Get-ChildItem -Path $FfmpegBin -Filter $pat -ErrorAction SilentlyContinue |
        Copy-Item -Destination $StageDir
}

Write-Host "==> Copying codec plugins"
New-Item -ItemType Directory -Force -Path "$StageDir\plugins\video" | Out-Null
New-Item -ItemType Directory -Force -Path "$StageDir\plugins\audio" | Out-Null
Copy-Item "$BuildDir\plugins\video\*.dll" "$StageDir\plugins\video\"
Copy-Item "$BuildDir\plugins\audio\*.dll" "$StageDir\plugins\audio\"

Write-Host "==> Generating deploy.nsh (Qt plugin subdirectories)"
$deployNsh = @()
Get-ChildItem -Path $StageDir -Directory | ForEach-Object {
    if ($_.Name -in @('platforms','styles','imageformats','iconengines','generic',
                      'tls','multimedia','qml','networkinformation')) {
        $deployNsh += "    File /r `"`${APP_STAGE_DIR}\$($_.Name)\*.dll`""
    }
}
Set-Content -Path "$StageDir\deploy.nsh" -Value $deployNsh -Encoding Ascii

Write-Host "==> Building NSIS installer"
Push-Location $Ps1Dir
try {
    & $Makensis "-DAPP_STAGE_DIR=$StageDir" bucharest-lite.nsi
    if ($LASTEXITCODE -ne 0) { throw 'makensis failed.' }
} finally {
    Pop-Location
}
$setupExe = "bucharest-lite-$Version-windows-x64-setup.exe"
if (-not (Test-Path (Join-Path $Ps1Dir $setupExe))) {
    throw "Installer not produced: $setupExe"
}

Write-Host "==> Building portable zip"
Remove-Item "$StageDir\deploy.nsh" -ErrorAction SilentlyContinue
$portable = Join-Path $Ps1Dir "bucharest-lite-$Version-windows-x64-portable.zip"
Remove-Item $portable -ErrorAction SilentlyContinue
& 'C:\Program Files\7-Zip\7z.exe' a -tzip -mx=9 $portable (Join-Path $StageDir '*')
if ($LASTEXITCODE -ne 0) { throw '7z failed.' }

Remove-Item -Recurse -Force $StageDir

Write-Host ''
Write-Host "OK. Built:"
Write-Host "  - $(Join-Path $Ps1Dir $setupExe)"
Write-Host "  - $portable"