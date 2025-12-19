; ------------------------------------------------------------------------
; Audio Wave – Windows Installer (NSIS) — polished, English only
; ------------------------------------------------------------------------
; Expects these defines from makensis:
;   /DPRODUCT_NAME
;   /DPRODUCT_VERSION
;   /DPROJECT_ROOT
;   /DCONFIGURATION
;   /DTARGET
;   /DOUTPUT_EXE
;   (optional)
;   /DPLUGIN_ID
;   /DPLUGIN_DLL
;   /DINSTALLER_ICON
;   /DPLUGIN_PUBLISHER
;   /DPLUGIN_URL
; ------------------------------------------------------------------------

Unicode true

!include "MUI2.nsh"
!include "Sections.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"

; ------------------------------------------------------------------------
; Compile-time defines (with sane fallbacks)
; ------------------------------------------------------------------------

!ifndef PRODUCT_NAME
  !define PRODUCT_NAME "Audio Wave Visualizer"
!endif

!ifndef PRODUCT_VERSION
  !define PRODUCT_VERSION "0.0.0"
!endif

!ifndef PROJECT_ROOT
  !define PROJECT_ROOT "."
!endif

!ifndef CONFIGURATION
  !define CONFIGURATION "RelWithDebInfo"
!endif

!ifndef TARGET
  !define TARGET "x64"
!endif

!ifndef OUTPUT_EXE
  !define OUTPUT_EXE "audio-wave-setup.exe"
!endif

; OBS plugin internal ID / folder name
!ifndef PLUGIN_ID
  !define PLUGIN_ID "audio-wave"
!endif

; DLL name built by CMake
!ifndef PLUGIN_DLL
  !define PLUGIN_DLL "audio-wave.dll"
!endif

!ifndef PLUGIN_PUBLISHER
  !define PLUGIN_PUBLISHER "MML Tech"
!endif

!ifndef PLUGIN_URL
  !define PLUGIN_URL "https://mmltools.github.io/audio-wave/"
!endif

; Where CMake installed the plugin:
;   ${PROJECT_ROOT}\release\<CONFIGURATION>\<PLUGIN_ID>\...
!define BUILD_ROOT "${PROJECT_ROOT}\release\${CONFIGURATION}\${PLUGIN_ID}"

; Installer icon
!ifndef INSTALLER_ICON
  !define INSTALLER_ICON "${PROJECT_ROOT}\installer\resources\audio-wave.ico"
!endif

; Optional custom welcome/finish bitmap (typically 164x314)
!define MUI_WELCOMEFINISHPAGE_BITMAP "${PROJECT_ROOT}\installer\resources\audio-wave-welcome.bmp"

; Optional header bitmap (commonly ~150x57)
!ifndef INSTALLER_HEADER_BMP
  !define INSTALLER_HEADER_BMP "${PROJECT_ROOT}\installer\resources\audio-wave-header.bmp"
!endif

; ------------------------------------------------------------------------
; Basic installer metadata
; ------------------------------------------------------------------------

Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "${OUTPUT_EXE}"

; We need admin rights to write into Program Files\obs-studio (typical)
RequestExecutionLevel admin

; Use $INSTDIR as the OBS folder so NSIS behaves predictably
InstallDir "$PROGRAMFILES64\obs-studio"

Var OBSDir
Var PrevUninst

; ------------------------------------------------------------------------
; Add/Remove Programs (ARP) registry constants
; ------------------------------------------------------------------------

!define UNINST_ROOT HKLM
!define UNINST_KEY  "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PLUGIN_ID}"
!define ESTIMATED_SIZE_KB  2500

; ------------------------------------------------------------------------
; Modern UI (MUI2) — visual polish
; ------------------------------------------------------------------------

!define MUI_ABORTWARNING

!define MUI_ICON   "${INSTALLER_ICON}"
!define MUI_UNICON "${INSTALLER_ICON}"

; Header branding (small banner on most pages)
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_RIGHT
!define MUI_HEADERIMAGE_BITMAP "${INSTALLER_HEADER_BMP}"
!define MUI_FINISHPAGE_SHOWREADME
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Open plugin documentation"
!define MUI_FINISHPAGE_SHOWREADME_FUNCTION OpenDocs

; Pages
!insertmacro MUI_PAGE_WELCOME

; OBS folder selection page
PageEx directory
  DirText \
    "Select your OBS Studio installation folder." \
    "Audio Wave Visualizer will be installed into this OBS folder (plugin DLL + locale files)." \
    "Browse..."
  DirVar $OBSDir
  ; validate selection when leaving page
  PageCallbacks "" "" DirPageLeave
PageExEnd

!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

; English only
!insertmacro MUI_LANGUAGE "English"

; ------------------------------------------------------------------------
; Init: auto-detect OBS folder + upgrade handling
; ------------------------------------------------------------------------

Function .onInit
  ; Default
  StrCpy $OBSDir "$PROGRAMFILES64\obs-studio"
  StrCpy $INSTDIR $OBSDir

  ; Try to detect OBS from registry (best-effort; different builds may vary)
  ; Common candidates (we probe multiple keys, ignore failures)
  ClearErrors
  ReadRegStr $0 HKLM "SOFTWARE\OBS Studio" "InstallPath"
  ${IfNot} ${Errors}
    ${If} $0 != ""
      StrCpy $OBSDir $0
      StrCpy $INSTDIR $0
      Goto done_detect
    ${EndIf}
  ${EndIf}

  ClearErrors
  ReadRegStr $0 HKLM "SOFTWARE\WOW6432Node\OBS Studio" "InstallPath"
  ${IfNot} ${Errors}
    ${If} $0 != ""
      StrCpy $OBSDir $0
      StrCpy $INSTDIR $0
      Goto done_detect
    ${EndIf}
  ${EndIf}

  ClearErrors
  ReadRegStr $0 HKCU "SOFTWARE\OBS Studio" "InstallPath"
  ${IfNot} ${Errors}
    ${If} $0 != ""
      StrCpy $OBSDir $0
      StrCpy $INSTDIR $0
      Goto done_detect
    ${EndIf}
  ${EndIf}

done_detect:

  ; If previous version installed (via our ARP key), offer clean upgrade
  ClearErrors
  ReadRegStr $PrevUninst ${UNINST_ROOT} "${UNINST_KEY}" "UninstallString"
  ${IfNot} ${Errors}
    ${If} $PrevUninst != ""
      MessageBox MB_ICONQUESTION|MB_YESNO \
        "A previous version of ${PRODUCT_NAME} appears to be installed.$\r$\n$\r$\nDo you want to update it now? (Recommended)" \
        IDYES do_upgrade IDNO done
do_upgrade:
      ; Best effort: run prior uninstaller silently
      ; If it fails, we continue (installer will overwrite files).
      ExecWait '$PrevUninst /S'
    ${EndIf}
  ${EndIf}

done:
FunctionEnd

; Validate OBS directory selection
Function DirPageLeave
  ; Keep INSTDIR in sync
  StrCpy $INSTDIR $OBSDir

  ; Require obs64.exe to exist (typical OBS layout)
  ${IfNot} ${FileExists} "$OBSDir\bin\64bit\obs64.exe"
    MessageBox MB_ICONEXCLAMATION|MB_OK \
      "This folder does not look like a valid OBS Studio (64-bit) installation.$\r$\n$\r$\nPlease select the folder that contains:\$\r\$\n  bin\64bit\obs64.exe"
    Abort
  ${EndIf}

  ; Optional: warn if plugin target folders missing (we will create them)
  ; No abort here — just proactive reassurance.
FunctionEnd

; ------------------------------------------------------------------------
; Sections
; ------------------------------------------------------------------------

Section "Core OBS Plugin" SEC_CORE
  SectionIn RO

  ; Ensure we target the selected OBS folder
  StrCpy $INSTDIR $OBSDir

  ; Warn if OBS is likely running (lightweight heuristic; no plugins required)
  ; If you want strict detection, use nsProcess plugin — keeping this installer dependency-free.
  MessageBox MB_ICONINFORMATION|MB_OK \
    "If OBS Studio is currently running, please close it before continuing to ensure the plugin can be updated cleanly."

  ; --- Plugin DLL ---
  ; Final path: <OBS>\obs-plugins\64bit\audio-wave.dll
  SetOutPath "$OBSDir\obs-plugins\64bit"
  CreateDirectory "$OBSDir\obs-plugins\64bit"
  File "/oname=${PLUGIN_DLL}" "${BUILD_ROOT}\bin\64bit\${PLUGIN_DLL}"

  ; --- Locale files (if present) ---
  ; Final path: <OBS>\data\obs-plugins\audio-wave\locale\...
  SetOutPath "$OBSDir\data\obs-plugins\${PLUGIN_ID}\locale"
  CreateDirectory "$OBSDir\data\obs-plugins\${PLUGIN_ID}\locale"
  File /nonfatal /r "${BUILD_ROOT}\data\locale\*.*"

  ; --- Write uninstaller inside the plugin data folder ---
  ; Final path: <OBS>\data\obs-plugins\audio-wave\uninstall.exe
  SetOutPath "$OBSDir\data\obs-plugins\${PLUGIN_ID}"
  WriteUninstaller "$OBSDir\data\obs-plugins\${PLUGIN_ID}\uninstall.exe"

  ; --- Register in Apps & Features (ARP) ---
  ; NOTE: We store uninstall string with quotes for safety.
  WriteRegStr   ${UNINST_ROOT} "${UNINST_KEY}" "DisplayName"     "${PRODUCT_NAME}"
  WriteRegStr   ${UNINST_ROOT} "${UNINST_KEY}" "DisplayVersion"  "${PRODUCT_VERSION}"
  WriteRegStr   ${UNINST_ROOT} "${UNINST_KEY}" "Publisher"       "${PLUGIN_PUBLISHER}"
  WriteRegStr   ${UNINST_ROOT} "${UNINST_KEY}" "URLInfoAbout"    "${PLUGIN_URL}"
  WriteRegStr   ${UNINST_ROOT} "${UNINST_KEY}" "InstallLocation" "$OBSDir"
  WriteRegStr   ${UNINST_ROOT} "${UNINST_KEY}" "DisplayIcon"     "$OBSDir\data\obs-plugins\${PLUGIN_ID}\uninstall.exe"
  WriteRegStr   ${UNINST_ROOT} "${UNINST_KEY}" "UninstallString" '"$OBSDir\data\obs-plugins\${PLUGIN_ID}\uninstall.exe"'
  WriteRegDWORD ${UNINST_ROOT} "${UNINST_KEY}" "NoModify"        1
  WriteRegDWORD ${UNINST_ROOT} "${UNINST_KEY}" "NoRepair"        1
  WriteRegDWORD ${UNINST_ROOT} "${UNINST_KEY}" "EstimatedSize"   ${ESTIMATED_SIZE_KB}

SectionEnd

; ------------------------------------------------------------------------
; Uninstaller
; ------------------------------------------------------------------------

Section "Uninstall"
  ; $INSTDIR for uninstall is not guaranteed if launched from ARP,
  ; so infer OBS dir from the uninstaller location.
  ; Uninstaller is stored at: <OBS>\data\obs-plugins\audio-wave\uninstall.exe
  ; So parent-parent-parent gives <OBS>.
  StrCpy $0 "$EXEDIR" ; <OBS>\data\obs-plugins\audio-wave
  ; Go up 3 levels to reach <OBS>
  ${GetParent} $0 $1  ; <OBS>\data\obs-plugins
  ${GetParent} $1 $2  ; <OBS>\data
  ${GetParent} $2 $3  ; <OBS>

  ; Remove installed files
  Delete "$3\obs-plugins\64bit\${PLUGIN_DLL}"

  RMDir /r "$3\data\obs-plugins\${PLUGIN_ID}\locale"
  Delete "$3\data\obs-plugins\${PLUGIN_ID}\uninstall.exe"
  ; Remove the plugin folder if empty (best-effort)
  RMDir "$3\data\obs-plugins\${PLUGIN_ID}"

  ; Remove ARP entry
  DeleteRegKey ${UNINST_ROOT} "${UNINST_KEY}"
SectionEnd

; ------------------------------------------------------------------------
; Finish page actions
; ------------------------------------------------------------------------

Function OpenDocs
  ExecShell "open" "${PLUGIN_URL}"
FunctionEnd

; ------------------------------------------------------------------------
; Version info in EXE properties
; ------------------------------------------------------------------------

; IMPORTANT: VIProductVersion must be numeric a.b.c.d
; Assumes PRODUCT_VERSION is at least a.b.c (e.g. 1.2.3). If not, pass a sanitized value from CI.
VIProductVersion  "${PRODUCT_VERSION}.0"
VIAddVersionKey   "ProductName"     "${PRODUCT_NAME}"
VIAddVersionKey   "FileDescription" "${PRODUCT_NAME} plugin installer"
VIAddVersionKey   "CompanyName"     "${PLUGIN_PUBLISHER}"
VIAddVersionKey   "FileVersion"     "${PRODUCT_VERSION}"
VIAddVersionKey   "ProductVersion"  "${PRODUCT_VERSION}"
VIAddVersionKey   "LegalCopyright"  "Copyright © ${PLUGIN_PUBLISHER}"
