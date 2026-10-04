; ---------------------------------------------------------------------------
;  Architecture selection and prerequisite detection.
;
;  ARCH is defined per package by build-installer.ps1 (x64 | x86 | arm64) and
;  drives every architecture dependent detail: the runtime package suffix, the
;  registry view, the VC++ redistributable key and the download link.
;
;  Sets:
;    $RuntimeFound      1 when a matching Windows App Runtime is installed
;    $RuntimeFoundName  the package full name that matched
;    $VCRuntimeFound    1 when the matching VC++ 2015-2022 runtime is installed
; ---------------------------------------------------------------------------

!include "LogicLib.nsh"
!include "StrFunc.nsh"

${Using:StrFunc} StrLoc

!ifndef ARCH
  !define ARCH "x64"
!endif

!if "${ARCH}" == "x64"
  !define ARCH_LABEL      "x64"
  !define ARCH_X64
!else
  !if "${ARCH}" == "x86"
    !define ARCH_LABEL    "x86"
    !define ARCH_X86
  !else
    !if "${ARCH}" == "arm64"
      !define ARCH_LABEL  "ARM64"
      !define ARCH_ARM64
    !else
      !error "ARCH must be x64, x86 or arm64 (got '${ARCH}')"
    !endif
  !endif
!endif

; Minimum Windows App Runtime the handler was built against.  Bump together
; with Microsoft.WindowsAppSDK in the vcxproj files.
!define REQUIRED_RUNTIME_MAJOR 2
!define REQUIRED_RUNTIME_MINOR 5

!ifdef ARCH_X64
  !define ARCH_PKG_SUFFIX  "_x64__8wekyb3d8bbwe"
  !define ARCH_SUFFIX_LEN  19
  !define ARCH_VC_KEY      "x64"
  !define ARCH_REGVIEW     64
  !define ARCH_PROGRAM_FILES "$PROGRAMFILES64"
  !define ARCH_INSTALL_NAME  "PlaylistPreview"
  !define ARCH_RUNTIME_URL "https://aka.ms/windowsappsdk/2.5/latest/windowsappruntimeinstall-x64.exe"
  !define ARCH_VCREDIST_URL "https://aka.ms/vs/17/release/vc_redist.x64.exe"
  !define ARCH_REGSVR32    "$WINDIR\System32\regsvr32.exe"
  !define ARCH_NEEDS_REDIRECT_OFF
!endif

!ifdef ARCH_X86
  !define ARCH_PKG_SUFFIX  "_x86__8wekyb3d8bbwe"
  !define ARCH_SUFFIX_LEN  19
  !define ARCH_VC_KEY      "x86"
  !define ARCH_REGVIEW     32
  !define ARCH_PROGRAM_FILES "$PROGRAMFILES32"
  !define ARCH_INSTALL_NAME  "PlaylistPreview"
  !define ARCH_RUNTIME_URL "https://aka.ms/windowsappsdk/2.5/latest/windowsappruntimeinstall-x86.exe"
  !define ARCH_VCREDIST_URL "https://aka.ms/vs/17/release/vc_redist.x86.exe"
  ; The 32-bit regsvr32: $SYSDIR is already the WOW64 directory on x64 Windows.
  !define ARCH_REGSVR32    "$SYSDIR\regsvr32.exe"
!endif

!ifdef ARCH_ARM64
  !define ARCH_PKG_SUFFIX  "_arm64__8wekyb3d8bbwe"
  !define ARCH_SUFFIX_LEN  21
  !define ARCH_VC_KEY      "arm64"
  !define ARCH_REGVIEW     64
  ; Program Files is shared with the x64 package, so ARM64 gets its own folder.
  !define ARCH_PROGRAM_FILES "$PROGRAMFILES64"
  !define ARCH_INSTALL_NAME  "PlaylistPreview (ARM64)"
  !define ARCH_RUNTIME_URL "https://aka.ms/windowsappsdk/2.5/latest/windowsappruntimeinstall-arm64.exe"
  !define ARCH_VCREDIST_URL "https://aka.ms/vs/17/release/vc_redist.arm64.exe"
  !define ARCH_REGSVR32    "$WINDIR\System32\regsvr32.exe"
  !define ARCH_NEEDS_REDIRECT_OFF
!endif

Var RuntimeFound
Var RuntimeFoundName
Var VCRuntimeFound

Function CheckPrerequisites
  StrCpy $RuntimeFound 0
  StrCpy $RuntimeFoundName ""
  StrCpy $VCRuntimeFound 0

  ; The Windows App Runtime ships as a framework package, and the machine wide
  ; package repository is the only place that lists every installed version.
  ; Look for Microsoft.WindowsAppRuntime.2_<version>${ARCH_PKG_SUFFIX} and
  ; accept anything at or above the required version.
  StrCpy $R1 0
  runtime_loop:
    EnumRegKey $R2 HKLM "SOFTWARE\Classes\Local Settings\Software\Microsoft\Windows\CurrentVersion\AppModel\PackageRepository\Packages" $R1
    StrCmp $R2 "" runtime_done
    IntOp $R1 $R1 + 1

    ; "Microsoft.WindowsAppRuntime.2_" is exactly 30 characters.
    StrCpy $R3 $R2 30
    StrCmp $R3 "Microsoft.WindowsAppRuntime.2_" 0 runtime_loop
    ; The architecture suffix is 19 characters for x64/x86, 21 for arm64.
    StrCpy $R4 $R2 "" -${ARCH_SUFFIX_LEN}
    StrCmp $R4 "${ARCH_PKG_SUFFIX}" 0 runtime_loop

    StrCpy $R5 $R2 24 30
    ${StrLoc} $R6 $R5 "_" ">"
    StrCmp $R6 "" runtime_loop
    StrCpy $R7 $R5 $R6            ; e.g. "2.5.1.0"

    ${StrLoc} $R8 $R7 "." ">"
    StrCmp $R8 "" runtime_loop
    StrCpy $R9 $R7 $R8            ; major
    IntOp $R0 $R8 + 1
    StrCpy $R0 $R7 8 $R0          ; "5.1.0"
    ${StrLoc} $R3 $R0 "." ">"
    StrCpy $R0 $R0 $R3            ; minor

    IntCmp $R9 ${REQUIRED_RUNTIME_MAJOR} runtime_major_eq runtime_loop runtime_ok

  runtime_major_eq:
    IntCmp $R0 ${REQUIRED_RUNTIME_MINOR} runtime_ok runtime_loop runtime_ok

  runtime_ok:
    StrCpy $RuntimeFound 1
    StrCpy $RuntimeFoundName $R2

  runtime_done:

  ; The handler DLLs import the VC++ 2015-2022 runtime dynamically and are
  ; loaded by prevhost.exe, so it has to be present for this architecture.
  ; The redistributable registers itself in the view of the architecture it
  ; was built for (x86 lands in the Wow6432Node view).
  ReadRegDWORD $R0 HKLM "SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\${ARCH_VC_KEY}" "Installed"
  StrCmp $R0 1 0 vcredist_done
    StrCpy $VCRuntimeFound 1
  vcredist_done:
FunctionEnd
