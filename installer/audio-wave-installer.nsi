; ------------------------------------------------------------------------
; Audio Wave – Windows Installer (NSIS)
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
; ------------------------------------------------------------------------

Unicode true

!include "MUI2.nsh"
!include "Sections.nsh"

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

; Where CMake installed the plugin:
;   ${PROJECT_ROOT}\release\<CONFIGURATION>\<PLUGIN_ID>\...
!define BUILD_ROOT "${PROJECT_ROOT}\release\${CONFIGURATION}\${PLUGIN_ID}"

; Installer icon
!ifndef INSTALLER_ICON
  !define INSTALLER_ICON "${PROJECT_ROOT}\installer\resources\audio-wave.ico"
!endif

; Optional custom welcome/finish bitmap (164x314 or similar)
!define MUI_WELCOMEFINISHPAGE_BITMAP "${PROJECT_ROOT}\installer\resources\audio-wave-welcome.bmp"

; ------------------------------------------------------------------------
; Basic installer metadata
; ------------------------------------------------------------------------

Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "${OUTPUT_EXE}"

; We need admin rights to write into Program Files\obs-studio
RequestExecutionLevel admin

Var OBSDir

; ------------------------------------------------------------------------
; Sections
; ------------------------------------------------------------------------

Section "Core OBS Plugin" SEC_CORE
  SectionIn RO

  ; --- Plugin DLL ---
  ; Final path: <OBS>\obs-plugins\64bit\audio-wave.dll
  SetOutPath "$OBSDir\obs-plugins\64bit"
  File "/oname=${PLUGIN_DLL}" "${BUILD_ROOT}\bin\64bit\${PLUGIN_DLL}"

  ; --- Locale files (if present) ---
  ; Final path: <OBS>\data\obs-plugins\audio-wave\locale\...
  SetOutPath "$OBSDir\data\obs-plugins\${PLUGIN_ID}\locale"
  File /nonfatal /r "${BUILD_ROOT}\data\locale\*.*"

SectionEnd

; ------------------------------------------------------------------------
; MUI pages
; ------------------------------------------------------------------------

!define MUI_ABORTWARNING

; Custom icon for installer + uninstaller
!define MUI_ICON   "${INSTALLER_ICON}"
!define MUI_UNICON "${INSTALLER_ICON}"

!insertmacro MUI_PAGE_WELCOME

; OBS folder selection page
PageEx directory
  DirText \
    "Select the folder where OBS Studio is installed." \
    "The ${PRODUCT_NAME} plugin (Audio Wave (Simple) source, shapes and wave styles) will be installed into this OBS Studio folder." \
    "Browse..."
  DirVar $OBSDir
PageExEnd

!insertmacro MUI_PAGE_INSTFILES

; Languages
!insertmacro MUI_LANGUAGE "English"

; ------------------------------------------------------------------------
; Init: default OBS folder
; ------------------------------------------------------------------------

Function .onInit
  ; Default to standard 64-bit OBS install path; user can change it
  StrCpy $OBSDir "$PROGRAMFILES64\obs-studio"
FunctionEnd

; ------------------------------------------------------------------------
; Version info in EXE properties
; ------------------------------------------------------------------------

VIProductVersion  "${PRODUCT_VERSION}.0"
VIAddVersionKey   "ProductName"     "${PRODUCT_NAME}"
VIAddVersionKey   "FileDescription" "Audio Wave Visualizer plugin installer"
VIAddVersionKey   "CompanyName"     "MML Tech"
VIAddVersionKey   "FileVersion"     "${PRODUCT_VERSION}"
VIAddVersionKey   "LegalCopyright"  "Copyright © MML Tech"
