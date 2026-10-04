; ---------------------------------------------------------------------------
;  PlaylistPreview - classic installer (NSIS 3.x, Unicode)
;
;  One script, one architecture per compile: build-installer.ps1 defines ARCH
;  (x64 | x86 | arm64) together with the matching payload directory, so a
;  single build run produces one setup.exe per architecture.
;
;  The product is an Explorer preview handler: an in-process COM DLL that the
;  shell loads into prevhost.exe.  It therefore needs a machine-wide COM
;  registration in the registry view of its own architecture, which is exactly
;  what this installer does - it is not, and cannot be, a per-user MSIX-style
;  registration.
;
;  Payload per architecture (built by build-installer.ps1):
;     PreviewHandlerShell.dll                    COM shell + WinUI island host
;     PlaylistPreviewUI.dll                      WinUI 3 content library
;     Microsoft.WindowsAppRuntime.Bootstrap.dll  App SDK bootstrap (framework
;                                                dependent - the runtime itself
;                                                must already be installed)
;
;  Registration is delegated to the DLL's own DllRegisterServer (regsvr32 /s)
;  so the registry layout has a single source of truth; the uninstaller still
;  removes the keys directly as a safety net.
; ---------------------------------------------------------------------------

Unicode true

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "WinVer.nsh"
!include "FileFunc.nsh"

; --- inputs (overridable with /D on the makensis command line) --------------

!if /FileExists "${__FILEDIR__}\build-defines.nsh"
  !include "${__FILEDIR__}\build-defines.nsh"
!endif

!ifndef APP_VERSION
  !define APP_VERSION "1.0.0"
!endif
!ifndef SOURCE_DIR
  !define SOURCE_DIR "${__FILEDIR__}\..\artifacts\${ARCH}\Release"
!endif
!ifndef LICENSE_FILE
  !define LICENSE_FILE "${__FILEDIR__}\..\LICENSE"
!endif
!ifndef OUT_FILE
  !define OUT_FILE "${__FILEDIR__}\dist\PlaylistPreview-${APP_VERSION}-${ARCH}-setup.exe"
!endif

; Provides ARCH_* (architecture dependent paths, registry view, runtime checks)
; and the $RuntimeFound / $VCRuntimeFound state.  Included after the build
; defines because it defaults ARCH to x64 when nothing set it.
!include "prerequisites.nsh"

; --- product constants ------------------------------------------------------

!define APP_DISPLAY_NAME   "PlaylistPreview"
!define APP_PUBLISHER      "sudo0015"
!define APP_URL            "https://github.com/sudo0015/PlaylistPreview"
!define APP_CLSID          "{8F3A2C64-6D2E-4E7B-9C5A-1B2C3D4E5F60}"
!define PREVIEW_IID        "{8895b1c6-b41f-4c1c-a562-0d564250836f}"
!define HANDLER_DLL        "PreviewHandlerShell.dll"
!define APP_FULL_NAME      "${APP_DISPLAY_NAME} (${ARCH_LABEL})"

; One uninstall entry per architecture - x64 and x86 can coexist, and ARM64
; uses its own directory, so they must not fight over one key.
!define UNINST_KEY         "Software\Microsoft\Windows\CurrentVersion\Uninstall\PlaylistPreview-${ARCH}"

; --- installer identity -----------------------------------------------------

Name "${APP_FULL_NAME}"
; MUI sets a localized caption per page; this one covers the brief moment
; before the first page shows, so it stays language neutral.
Caption "${APP_FULL_NAME} ${APP_VERSION}"
OutFile "${OUT_FILE}"
InstallDir "${ARCH_PROGRAM_FILES}\${ARCH_INSTALL_NAME}"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
SetCompressorDictSize 32
ShowInstDetails show
ShowUninstDetails show

VIProductVersion "${APP_VERSION}.0"
VIAddVersionKey "ProductName"     "${APP_DISPLAY_NAME}"
; Version resource strings stay English: they show up in the file properties
; dialog and in Windows' own UI, and PATH-style tooling reads them too.
VIAddVersionKey "FileDescription" "${APP_FULL_NAME} Setup"
VIAddVersionKey "FileVersion"     "${APP_VERSION}"
VIAddVersionKey "ProductVersion"  "${APP_VERSION}"
VIAddVersionKey "CompanyName"     "${APP_PUBLISHER}"
VIAddVersionKey "LegalCopyright"  "MIT License"

!define MUI_ABORTWARNING

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${LICENSE_FILE}"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; The first language is the fallback; NSIS itself picks the entry matching the
; system language at run time, so no language selection dialog is involved.
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "SimpChinese"

; --- localized strings ------------------------------------------------------
; MUI translates its own pages; these cover everything this script says.
; Replacements are resolved when the instruction runs, so $INSTDIR / $LANGUAGE
; inside the text still expand.

LangString MSG_NEEDS_WIN10 ${LANG_ENGLISH} \
  "${APP_FULL_NAME} requires Windows 10 or later."
LangString MSG_NEEDS_WIN10 ${LANG_SIMPCHINESE} \
  "${APP_FULL_NAME} 需要 Windows 10 或更高版本。"

LangString MSG_NEEDS_X64 ${LANG_ENGLISH} \
  "This is the 64-bit (x64) package. It can only be installed on 64-bit x64 Windows."
LangString MSG_NEEDS_X64 ${LANG_SIMPCHINESE} \
  "这是 64 位 (x64) 安装包，只能在 64 位 x64 Windows 上安装。"

LangString MSG_NEEDS_ARM64 ${LANG_ENGLISH} \
  "This is the ARM64 package. It can only be installed on Windows on ARM."
LangString MSG_NEEDS_ARM64 ${LANG_SIMPCHINESE} \
  "这是 ARM64 安装包，只能在 Windows on ARM 上安装。"

LangString MSG_RUNTIME_MISSING_LOG ${LANG_ENGLISH} \
  "Windows App Runtime: ${REQUIRED_RUNTIME_MAJOR}.${REQUIRED_RUNTIME_MINOR} (${ARCH_LABEL}) or later not found"
LangString MSG_RUNTIME_MISSING_LOG ${LANG_SIMPCHINESE} \
  "Windows App Runtime: 未检测到 ${REQUIRED_RUNTIME_MAJOR}.${REQUIRED_RUNTIME_MINOR} (${ARCH_LABEL}) 或更高版本"

LangString MSG_VCRUNTIME_OK_LOG ${LANG_ENGLISH} \
  "Visual C++ runtime (${ARCH_LABEL}): installed"
LangString MSG_VCRUNTIME_OK_LOG ${LANG_SIMPCHINESE} \
  "Visual C++ 运行库 (${ARCH_LABEL}): 已安装"

LangString MSG_VCRUNTIME_MISSING_LOG ${LANG_ENGLISH} \
  "Visual C++ runtime (${ARCH_LABEL}): not found"
LangString MSG_VCRUNTIME_MISSING_LOG ${LANG_SIMPCHINESE} \
  "Visual C++ 运行库 (${ARCH_LABEL}): 未检测到"

LangString MSG_RUNTIME_PROMPT ${LANG_ENGLISH} \
  "Windows App Runtime ${REQUIRED_RUNTIME_MAJOR}.${REQUIRED_RUNTIME_MINOR} (${ARCH_LABEL}) or later was not found.$\r$\n$\r$\nThe preview handler is an in-process COM DLL loaded by Explorer, so it cannot carry the runtime itself. Without it the preview pane stays empty.$\r$\n$\r$\nOpen the runtime download page now? (You can also install it later - setup will continue.)"
LangString MSG_RUNTIME_PROMPT ${LANG_SIMPCHINESE} \
  "未检测到 Windows App Runtime ${REQUIRED_RUNTIME_MAJOR}.${REQUIRED_RUNTIME_MINOR} (${ARCH_LABEL}，或更高版本)。$\r$\n$\r$\n预览处理器是被资源管理器加载的进程内 COM DLL，无法自带运行时；缺少它时预览窗格会一直空白。$\r$\n$\r$\n是否现在打开运行时下载页面？（可以稍后自行安装，安装程序会继续）"

LangString MSG_VCRUNTIME_PROMPT ${LANG_ENGLISH} \
  "The ${ARCH_LABEL} Microsoft Visual C++ runtime was not found.$\r$\n$\r$\nThe preview handler needs it in order to load. Open the download page now? (setup will continue)"
LangString MSG_VCRUNTIME_PROMPT ${LANG_SIMPCHINESE} \
  "未检测到 ${ARCH_LABEL} 版 Microsoft Visual C++ 运行库。$\r$\n$\r$\n预览处理器需要它才能被加载。是否现在打开下载页面？（安装程序会继续）"

LangString MSG_PREVHOST_BUSY ${LANG_ENGLISH} \
  "prevhost.exe is still holding the previous copy; stopping it so the files can be replaced"
LangString MSG_PREVHOST_BUSY ${LANG_SIMPCHINESE} \
  "prevhost.exe 仍占用旧版本，正在结束它以完成替换"

LangString MSG_FILE_LOCKED ${LANG_ENGLISH} \
  "Cannot replace this file because it is still in use:$\r$\n$INSTDIR\${HANDLER_DLL}$\r$\n$\r$\nClose any Explorer preview pane and try again."
LangString MSG_FILE_LOCKED ${LANG_SIMPCHINESE} \
  "无法替换该文件，它仍被占用：$\r$\n$INSTDIR\${HANDLER_DLL}$\r$\n$\r$\n请关闭资源管理器的预览窗格后重试。"

LangString MSG_COPYING ${LANG_ENGLISH} \
  "Copying the program files to $INSTDIR"
LangString MSG_COPYING ${LANG_SIMPCHINESE} \
  "复制程序文件到 $INSTDIR"

LangString MSG_REGISTERING ${LANG_ENGLISH} \
  "Registering the preview handler (${ARCH_LABEL})"
LangString MSG_REGISTERING ${LANG_SIMPCHINESE} \
  "注册预览处理器 (${ARCH_LABEL})"

LangString MSG_REGISTER_FAILED ${LANG_ENGLISH} \
  "Registering the preview handler failed (regsvr32 exit code $0). Setup aborted."
LangString MSG_REGISTER_FAILED ${LANG_SIMPCHINESE} \
  "注册预览处理器失败（regsvr32 退出码 $0），安装中止。"

LangString MSG_RESTART_PROMPT ${LANG_ENGLISH} \
  "Setup is complete.$\r$\n$\r$\nExplorer has to restart before it picks up the new preview handler (the screen will flicker briefly). Restart it now?"
LangString MSG_RESTART_PROMPT ${LANG_SIMPCHINESE} \
  "安装完成。$\r$\n$\r$\n资源管理器需要重启才能加载新的预览处理器（桌面会短暂闪烁）。现在重启吗？"

LangString MSG_RESTARTING ${LANG_ENGLISH} \
  "Restarting Explorer"
LangString MSG_RESTARTING ${LANG_SIMPCHINESE} \
  "重启资源管理器"

; --- state ------------------------------------------------------------------

Var SkipRuntimeCheck

; ---------------------------------------------------------------------------
;  Checks
; ---------------------------------------------------------------------------

Function .onInit
  ${IfNot} ${AtLeastWin10}
    ${IfNot} ${Silent}
      MessageBox MB_ICONSTOP "$(MSG_NEEDS_WIN10)"
    ${EndIf}
    Abort
  ${EndIf}

  ; The payload can only be loaded by a host process of the same architecture,
  ; so refuse to install on a machine that cannot run it at all.
!ifdef ARCH_X64
  ${IfNot} ${IsNativeAMD64}
    ${IfNot} ${Silent}
      MessageBox MB_ICONSTOP "$(MSG_NEEDS_X64)"
    ${EndIf}
    Abort
  ${EndIf}
!endif
!ifdef ARCH_ARM64
  ${IfNot} ${IsNativeARM64}
    ${IfNot} ${Silent}
      MessageBox MB_ICONSTOP "$(MSG_NEEDS_ARM64)"
    ${EndIf}
    Abort
  ${EndIf}
!endif
  ; The x86 package runs on 32-bit Windows and inside WOW64 on 64-bit Windows,
  ; so it needs no architecture gate beyond Windows 10.

  StrCpy $SkipRuntimeCheck 0
  ${GetParameters} $R0
  ${GetOptions} $R0 "/SKIPRUNTIMECHECK" $R1
  ${IfNot} ${Errors}
    StrCpy $SkipRuntimeCheck 1
  ${EndIf}

  SetRegView ${ARCH_REGVIEW}
  Call CheckPrerequisites

  ${If} $RuntimeFound == 1
    DetailPrint "Windows App Runtime: $RuntimeFoundName"
  ${Else}
    DetailPrint "$(MSG_RUNTIME_MISSING_LOG)"
  ${EndIf}
  ${If} $VCRuntimeFound == 1
    DetailPrint "$(MSG_VCRUNTIME_OK_LOG)"
  ${Else}
    DetailPrint "$(MSG_VCRUNTIME_MISSING_LOG)"
  ${EndIf}

  ${If} $RuntimeFound == 0
  ${AndIf} $SkipRuntimeCheck == 0
    ${IfNot} ${Silent}
      MessageBox MB_ICONEXCLAMATION|MB_YESNO \
        "$(MSG_RUNTIME_PROMPT)" \
        IDNO runtime_continue
      ExecShell "open" "${ARCH_RUNTIME_URL}"
    runtime_continue:
    ${EndIf}
  ${EndIf}

  ${If} $VCRuntimeFound == 0
    ${IfNot} ${Silent}
      MessageBox MB_ICONEXCLAMATION|MB_YESNO \
        "$(MSG_VCRUNTIME_PROMPT)" \
        IDNO vcredist_continue
      ExecShell "open" "${ARCH_VCREDIST_URL}"
    vcredist_continue:
    ${EndIf}
  ${EndIf}
FunctionEnd

; ---------------------------------------------------------------------------
;  Install
; ---------------------------------------------------------------------------

Section "!${APP_DISPLAY_NAME}" SecMain
  SectionIn RO
  SetRegView ${ARCH_REGVIEW}

  ; An existing copy is very likely mapped into prevhost.exe (Explorer keeps
  ; one surrogate alive for the preview pane).  Opening the DLL for append
  ; needs write access, which a mapped image refuses - that is all the
  ; detection we need.
  ${If} ${FileExists} "$INSTDIR\${HANDLER_DLL}"
    ClearErrors
    FileOpen $0 "$INSTDIR\${HANDLER_DLL}" "a"
    ${If} ${Errors}
      DetailPrint "$(MSG_PREVHOST_BUSY)"
      nsExec::Exec 'taskkill /F /IM prevhost.exe'
      Pop $0
      Sleep 700
      ClearErrors
      FileOpen $0 "$INSTDIR\${HANDLER_DLL}" "a"
      ${If} ${Errors}
        ${IfNot} ${Silent}
          MessageBox MB_ICONSTOP|MB_OK "$(MSG_FILE_LOCKED)"
        ${EndIf}
        SetErrors
        Quit
      ${EndIf}
    ${EndIf}
    FileClose $0
  ${EndIf}

  DetailPrint "$(MSG_COPYING)"
  CreateDirectory "$INSTDIR"
  SetOutPath "$INSTDIR"

  File "/oname=${HANDLER_DLL}"                             "${SOURCE_DIR}\${HANDLER_DLL}"
  File "/oname=PlaylistPreviewUI.dll"                      "${SOURCE_DIR}\PlaylistPreviewUI.dll"
  File "/oname=Microsoft.WindowsAppRuntime.Bootstrap.dll"  "${SOURCE_DIR}\Microsoft.WindowsAppRuntime.Bootstrap.dll"

!if /FileExists "${SOURCE_DIR}\PlaylistPreviewUI.pri"
  File "/oname=PlaylistPreviewUI.pri" "${SOURCE_DIR}\PlaylistPreviewUI.pri"
!endif
!if /FileExists "${SOURCE_DIR}\PreviewHandlerShell.pri"
  File "/oname=PreviewHandlerShell.pri" "${SOURCE_DIR}\PreviewHandlerShell.pri"
!endif

  ; Register machine wide, in this architecture's registry view.  The x86
  ; package must use the WOW64 regsvr32; the 64-bit ones reach the native
  ; one by switching file system redirection off first.
  DetailPrint "$(MSG_REGISTERING)"
!ifdef ARCH_NEEDS_REDIRECT_OFF
  ${DisableX64FSRedirection}
!endif
  nsExec::ExecToLog '"${ARCH_REGSVR32}" /s "$INSTDIR\${HANDLER_DLL}"'
  Pop $0
!ifdef ARCH_NEEDS_REDIRECT_OFF
  ${EnableX64FSRedirection}
!endif
  ${If} $0 != "0"
    ${IfNot} ${Silent}
      MessageBox MB_ICONSTOP|MB_OK "$(MSG_REGISTER_FAILED)"
    ${EndIf}
    SetErrors
    Quit
  ${EndIf}

  WriteUninstaller "$INSTDIR\uninstall.exe"

  ; Programs and Features entry, one per architecture.
  WriteRegStr   HKLM "${UNINST_KEY}" "DisplayName"          "${APP_FULL_NAME}"
  WriteRegStr   HKLM "${UNINST_KEY}" "DisplayVersion"       "${APP_VERSION}"
  WriteRegStr   HKLM "${UNINST_KEY}" "Publisher"            "${APP_PUBLISHER}"
  WriteRegStr   HKLM "${UNINST_KEY}" "URLInfoAbout"         "${APP_URL}"
  WriteRegStr   HKLM "${UNINST_KEY}" "InstallLocation"      "$INSTDIR"
  WriteRegStr   HKLM "${UNINST_KEY}" "DisplayIcon"          "$INSTDIR\uninstall.exe"
  WriteRegStr   HKLM "${UNINST_KEY}" "UninstallString"      '"$INSTDIR\uninstall.exe"'
  WriteRegStr   HKLM "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  WriteRegDWORD HKLM "${UNINST_KEY}" "EstimatedSize" $0

  ; Tell the shell that a handler appeared.
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'

  ; Explorer caches its handler list; a restart is the reliable trigger.
  ${IfNot} ${Silent}
    MessageBox MB_ICONQUESTION|MB_YESNO \
      "$(MSG_RESTART_PROMPT)" \
      IDNO no_restart
    DetailPrint "$(MSG_RESTARTING)"
    nsExec::Exec 'taskkill /F /IM explorer.exe'
    Pop $0
    Sleep 900
    ; Windows normally restarts the shell on its own; start it only when it
    ; really is gone, otherwise a second instance would pop a folder window.
    nsExec::ExecToStack 'tasklist /FI "IMAGENAME eq explorer.exe"'
    Pop $0
    Pop $1
    ${StrLoc} $2 $1 "explorer.exe" ">"
    ${If} $2 == ""
      Exec '"$WINDIR\explorer.exe"'
    ${EndIf}
    no_restart:
  ${EndIf}
SectionEnd

; ---------------------------------------------------------------------------
;  Uninstall
; ---------------------------------------------------------------------------

Section "Uninstall"
  SetRegView ${ARCH_REGVIEW}

  nsExec::Exec 'taskkill /F /IM prevhost.exe'
  Pop $0
  Sleep 400

  ; Ask the handler to unregister itself ...
  ${If} ${FileExists} "$INSTDIR\${HANDLER_DLL}"
!ifdef ARCH_NEEDS_REDIRECT_OFF
    ${DisableX64FSRedirection}
!endif
    nsExec::ExecToLog '"${ARCH_REGSVR32}" /s /u "$INSTDIR\${HANDLER_DLL}"'
    Pop $0
!ifdef ARCH_NEEDS_REDIRECT_OFF
    ${EnableX64FSRedirection}
!endif
  ${EndIf}

  ; ... and remove the keys directly as well, so a missing or locked DLL can
  ; never leave a dangling handler registration behind.
  DeleteRegKey HKLM "SOFTWARE\Classes\CLSID\${APP_CLSID}"
  DeleteRegKey HKLM "SOFTWARE\Classes\.cue\shellex\${PREVIEW_IID}"
  DeleteRegKey HKLM "SOFTWARE\Classes\.m3u8\shellex\${PREVIEW_IID}"
  DeleteRegValue HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\PreviewHandlers" "${APP_CLSID}"

  ; A per-user registration is possible when the DLL was registered without
  ; elevation (development); clean that up too, plus the "last view" preference.
  DeleteRegKey HKCU "SOFTWARE\Classes\CLSID\${APP_CLSID}"
  DeleteRegKey HKCU "SOFTWARE\Classes\.cue\shellex\${PREVIEW_IID}"
  DeleteRegKey HKCU "SOFTWARE\Classes\.m3u8\shellex\${PREVIEW_IID}"
  DeleteRegKey HKCU "Software\PlaylistPreview"

  Delete /REBOOTOK "$INSTDIR\uninstall.exe"
  Delete /REBOOTOK "$INSTDIR\${HANDLER_DLL}"
  Delete /REBOOTOK "$INSTDIR\PlaylistPreviewUI.dll"
  Delete /REBOOTOK "$INSTDIR\Microsoft.WindowsAppRuntime.Bootstrap.dll"
  Delete /REBOOTOK "$INSTDIR\PlaylistPreviewUI.pri"
  Delete /REBOOTOK "$INSTDIR\PreviewHandlerShell.pri"
  RMDir /REBOOTOK "$INSTDIR"

  DeleteRegKey HKLM "${UNINST_KEY}"

  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
SectionEnd
