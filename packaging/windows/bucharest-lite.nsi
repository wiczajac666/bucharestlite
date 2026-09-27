!define APP_NAME "Bucharest Lite"
!define APP_NAME_SHORT "BucharestLite"
!define APP_VERSION "1.1.1"
!define APP_PUBLISHER "BucharestLite contributors"
!define APP_EXE "bucharest-lite.exe"
!define APP_ICON "bucharest-lite.ico"
!define APP_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME_SHORT}"
!define APP_REG_KEY "Software\BucharestLite"
; APP_STAGE_DIR is set on the makensis command line by build_installer.ps1.
!ifndef APP_STAGE_DIR
    !define APP_STAGE_DIR "staging"
!endif
!define APP_STAGE_EXE "${APP_STAGE_DIR}\${APP_EXE}"
!define APP_STAGE_LICENSE "${APP_STAGE_DIR}\LICENSE"
!define APP_STAGE_NOTICES "${APP_STAGE_DIR}\THIRD_PARTY_NOTICES.md"

Unicode true
ManifestDPIAware true
SetCompressor /SOLID lzma

Name "${APP_NAME}"
Caption "${APP_NAME} ${APP_VERSION}"
Icon "${APP_ICON}"
OutFile "bucharest-lite-${APP_VERSION}-windows-x64-setup.exe"

InstallDir "$PROGRAMFILES64\${APP_NAME_SHORT}"
InstallDirRegKey HKLM "${APP_REG_KEY}" "InstallDir"

RequestExecutionLevel admin

!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "x64.nsh"

!define MUI_ABORTWARNING
!define MUI_ICON "${APP_ICON}"
!define MUI_UNICON "${APP_ICON}"
Var StartMenuFolder
!define MUI_STARTMENUPAGE_DEFAULTFOLDER "${APP_NAME}"
!define MUI_STARTMENUPAGE_REGISTRY_ROOT "HKLM"
!define MUI_STARTMENUPAGE_REGISTRY_KEY "${APP_REG_KEY}"
!define MUI_STARTMENUPAGE_REGISTRY_VALUENAME "StartMenuFolder"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${APP_STAGE_LICENSE}"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_STARTMENU Application $StartMenuFolder
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Section "Application" SecApp
    SectionIn RO
    SetShellVarContext all

    ; App binaries + Qt/FFmpeg runtime, all dropped next to the exe.
    SetOutPath "$INSTDIR"
    File "${APP_STAGE_EXE}"
    File /r "${APP_STAGE_DIR}\*.dll"
    !include "${APP_STAGE_DIR}\deploy.nsh"
    File "${APP_STAGE_LICENSE}"
    File "${APP_STAGE_NOTICES}"

    ; Codec plugins ship in the install dir; the app scans
    ; <exe root>\plugins\{video,audio} as its system root (paths.cpp) and
    ; %APPDATA%\BucharestLite\plugins as the per-user override root.
    SetOutPath "$INSTDIR\plugins\video"
    File /r "${APP_STAGE_DIR}\plugins\video\*.dll"
    SetOutPath "$INSTDIR\plugins\audio"
    File /r "${APP_STAGE_DIR}\plugins\audio\*.dll"

    ; Registry / uninstall bookkeeping.
    WriteRegStr HKLM "${APP_REG_KEY}" "InstallDir" "$INSTDIR"
    WriteRegStr HKLM "${APP_UNINST_KEY}" "DisplayName" "${APP_NAME}"
    WriteRegStr HKLM "${APP_UNINST_KEY}" "DisplayVersion" "${APP_VERSION}"
    WriteRegStr HKLM "${APP_UNINST_KEY}" "Publisher" "${APP_PUBLISHER}"
    WriteRegStr HKLM "${APP_UNINST_KEY}" "DisplayIcon" "$INSTDIR\${APP_EXE}"
    WriteRegStr HKLM "${APP_UNINST_KEY}" "UninstallString" "$\"$INSTDIR\uninstall.exe$\""
    WriteRegStr HKLM "${APP_UNINST_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "${APP_UNINST_KEY}" "NoModify" "1"
    WriteRegStr HKLM "${APP_UNINST_KEY}" "NoRepair" "1"
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    IntFmt $0 "0x%08X" $0
    WriteRegDWORD HKLM "${APP_UNINST_KEY}" "EstimatedSize" "$0"

    WriteUninstaller "$INSTDIR\uninstall.exe"

    ; Start Menu folder + desktop shortcut; the folder is chosen on the
    ; MUI start menu page and remembered in HKLM\Software\BucharestLite.
    !insertmacro MUI_STARTMENU_WRITE_BEGIN Application
        CreateDirectory "$SMPROGRAMS\$StartMenuFolder"
        CreateShortCut "$SMPROGRAMS\$StartMenuFolder\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
        CreateShortCut "$SMPROGRAMS\$StartMenuFolder\Uninstall ${APP_NAME}.lnk" "$INSTDIR\uninstall.exe"
        CreateShortCut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
        ; Persist the folder even on silent installs, so the uninstaller can
        ; locate the shortcuts (the MUI page only writes this when shown).
        WriteRegStr HKLM "${APP_REG_KEY}" "StartMenuFolder" "$StartMenuFolder"
    !insertmacro MUI_STARTMENU_WRITE_END
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecApp} "Bucharest Lite application, codec plugins and Qt/FFmpeg runtime."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
    SetShellVarContext all
    !insertmacro MUI_STARTMENU_GETFOLDER Application $StartMenuFolder

    DeleteRegKey HKLM "${APP_UNINST_KEY}"
    DeleteRegKey HKLM "${APP_REG_KEY}"

    Delete "$SMPROGRAMS\$StartMenuFolder\${APP_NAME}.lnk"
    Delete "$SMPROGRAMS\$StartMenuFolder\Uninstall ${APP_NAME}.lnk"
    RMDir "$SMPROGRAMS\$StartMenuFolder"
    Delete "$DESKTOP\${APP_NAME}.lnk"

    ; User data under %APPDATA% (projects, autosaves) is intentionally
    ; preserved on uninstall.
    RMDir /r "$INSTDIR"
SectionEnd

Function .onInit
    ${IfNot} ${RunningX64}
        MessageBox MB_OK "${APP_NAME} requires a 64-bit Windows installation."
        Abort
    ${EndIf}

    ; Pre-select the last-chosen folder (upgrade) or the default; this also
    ; supplies a valid value for silent installs, which skip the page.
    ReadRegStr $StartMenuFolder HKLM "${APP_REG_KEY}" "StartMenuFolder"
    ${If} $StartMenuFolder == ""
        StrCpy $StartMenuFolder "${APP_NAME}"
    ${EndIf}
FunctionEnd