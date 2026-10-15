# Building the Windows installer

`build_installer.ps1` is the single entry point for producing both distro
artifacts for Windows:

- `bucharest-lite-<version>-windows-x64-setup.exe` — NSIS per-machine installer (Admin)
- `bucharest-lite-<version>-windows-x64-portable.zip` — run from anywhere, no install

## Version numbering

`project(BucharestLite VERSION ...)` in the top-level `CMakeLists.txt` is the
**single source of truth**. Nothing in this directory hardcodes a version:

| Where | How it gets the version |
|---|---|
| `bucharest-lite.exe` metadata | `version.rc.in` is `configure_file`'d from `PROJECT_VERSION_{MAJOR,MINOR,PATCH}` + `BL_VERSION_BUILD` |
| Installer filename / uninstaller `DisplayVersion` | `build_installer.ps1` reads `windows-version.txt` (written by CMake at configure time) and passes `-DAPP_VERSION=` to `makensis` |
| Portable zip filename | same `$Version` variable |

To cut a release, edit **only** the `VERSION` in `CMakeLists.txt` (and add the
matching `CHANGELOG.md` heading, which is kept separate on purpose).

These used to be four independent hardcoded copies. They drifted: the 1.7.38
release shipped an exe whose `VS_FIXEDFILEINFO` still read `1.0.1.0` while
only the string fields said `1.7.38`, so Explorer's *Details* tab, the
Programs-and-Features entry and the NSIS `DisplayVersion` all disagreed. If you
ever need to reintroduce a hardcoded version, don't — add a `configure_file` or
a stamp file instead.

## Host prerequisites

Run on a Windows x64 build host (the CI VM is Windows 10 Pro 22H2), or just push
a `v*` tag / dispatch the `windows-package` job in `.github/workflows/ci.yml`,
which runs this script on `windows-latest` and uploads both artifacts. That job
also asserts the built exe's `FILEVERSION` matches `project(... VERSION ...)`
before it will publish anything.

| Tool | Version used | Notes |
|---|---|---|
| Visual Studio 2022 Build Tools | 14.44+ | VCTools workload + Windows 10/11 SDKs; auto-located via vswhere |
| CMake | 4.4 | on PATH |
| Ninja | any recent | on PATH |
| Qt 6 | 6.8.3 msvc2022_64 | `$env:QT_ROOT` (aqtinstall build incl. qtsvg, qtmultimedia, qttools) |
| FFmpeg shared build | 9.0.2 GPL (Gyan full_build-shared) | `$env:FFMPEG_ROOT`; must be the GPL build for x264/x265/SVT-AV1 |
| NSIS | 3.12 | on PATH (`makensis.exe`) |
| 7-Zip | 26 | default install path used for the portable zip |

## Build & package

```powershell
$env:QT_ROOT   = 'C:\Qt\6.8.3\msvc2022_64'
$env:FFMPEG_ROOT = 'C:\dev\ffmpeg-gyan\ffmpeg-9.0.2-full_build-shared'
.\build_installer.ps1
```

The script:

1. Configures with the `windows-release` CMake preset (Ninja + MSVC),
2. Builds `bucharest-lite.exe` + the 9 codec plugins and runs the full
   `ctest` suite (`QT_QPA_PLATFORM=offscreen`),
3. Runs `windeployqt` to gather the Qt runtime,
4. Copies the FFmpeg runtime DLLs (incl. MinGW-w64 deps libgcc_s_seh-1.dll,
   libstdc++-6.dll, libwinpthread-1.dll) next to the exe,
5. Washes everything into `staging/`, generates `deploy.nsh`, and calls
   `makensis`, then zips the same tree into the portable archive.

## What goes where at runtime

The app never looks into the registry for plugin paths. Discovered scan roots
(`src/core/platform/paths.cpp`):

| Root | Origin | Purpose |
|---|---|---|
| `<exe dir>\plugins\{video,audio}` | System | codec plugins shipped by installer / portable zip |
| `%APPDATA%\BucharestLite\plugins\{video,audio}` | User | user-installed plugin overrides |

User projects/autosaves live only under `%APPDATA%\BucharestLite` and are
preserved on uninstall.

## Headless notes (this VM)

- All steps run over SSH as non-interactive admin: run `build_installer.ps1`
  inside a `cmd /c` invoked via `schtasks`-free foreground SSH (session is
  already High-IL admin).
- `Write-Progress`/colored output fails on non-tty SSH; the script forces
  `$ProgressPreference='SilentlyContinue'` and avoids tty-only calls.