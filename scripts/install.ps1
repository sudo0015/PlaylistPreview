<#
.SYNOPSIS
    Installs PlaylistPreview as a Windows Explorer preview handler.

.DESCRIPTION
    Copies the handler to a stable location and registers it machine-wide.

    Deployment is framework-dependent on purpose: the handler is an in-process
    COM DLL loaded into prevhost.exe, and the Windows App SDK explicitly refuses
    self-contained deployment for class libraries
    ("WindowsAppSDKSelfContained should not be applied to a class library").
    This script therefore verifies that a suitable Windows App Runtime is
    installed and tells you where to get one if it is missing.

    Explorer hands the handler to prevhost.exe through the COM surrogate, and
    that launch goes through the COM SCM, which runs as SYSTEM and only reads the
    machine-wide registry.  A per-user registration would silently do nothing,
    so this script always requires elevation.

.EXAMPLE
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\install.ps1

.EXAMPLE
    # See what would happen without touching anything
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\install.ps1 -DryRun
#>
[CmdletBinding()]
param(
    [ValidateSet('x64', 'x86', 'arm64')][string] $Platform = 'x64',
    [ValidateSet('Debug', 'Release')][string] $Configuration = 'Debug',
    [string] $InstallDir = (Join-Path $env:ProgramFiles 'PlaylistPreview'),
    [string] $SourceDir = '',
    [switch] $Uninstall,
    [switch] $DryRun,
    [switch] $SkipRuntimeCheck,
    [switch] $RestartExplorer,
    # Development/testing only: install under %LOCALAPPDATA% without elevation.
    # Registry writes then fall back to HKCU, which Explorer's surrogate launch
    # cannot see -- the preview pane will stay empty even though everything
    # "worked".  Use it to exercise this script, not to deploy.
    [switch] $PerUser
)

$ErrorActionPreference = 'Stop'

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $SourceDir) {
    $SourceDir = Join-Path $repoRoot "artifacts\$Platform\$Configuration"
}

# Must match the Windows App SDK the projects build against
# (Microsoft.WindowsAppSDK 2.5.1 -> runtime 2.5.1.0).  Bump together.
$requiredRuntimeVersion = [version] '2.5.1.0'
$runtimePackageName = 'Microsoft.WindowsAppRuntime.2'
$runtimeDownloadUrl = 'https://aka.ms/windowsappsdk/2.5/latest/windowsappruntimeinstall-x64.exe'

$architectureName = switch ($Platform) {
    'x64' { 'X64' }
    'x86' { 'X86' }
    'arm64' { 'ARM64' }
}

if ($PerUser) {
    $InstallDir = Join-Path $env:LOCALAPPDATA 'PlaylistPreview'
}

# Files that must sit next to PreviewHandlerShell.dll in the install directory.
$payload = @(
    'PreviewHandlerShell.dll',
    'PlaylistPreviewUI.dll',
    'Microsoft.WindowsAppRuntime.Bootstrap.dll'
)

function Test-Elevated {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-InstalledRuntime {
    Get-AppxPackage -Name $runtimePackageName -ErrorAction SilentlyContinue |
        Where-Object {
            $_.Architecture -eq $architectureName -and
            [version] $_.Version -ge $requiredRuntimeVersion
        } |
        Sort-Object { [version] $_.Version } -Descending |
        Select-Object -First 1
}

function Get-RegisteredHandlerPath {
    $key = 'HKLM:\SOFTWARE\Classes\CLSID\{8F3A2C64-6D2E-4E7B-9C5A-1B2C3D4E5F60}\InprocServer32'
    if (Test-Path $key) {
        return (Get-Item $key).GetValue('')
    }
    return $null
}

function Invoke-RegSvr32 {
    param([string] $Dll, [switch] $Unregister)

    $arguments = @('/s')
    if ($Unregister) { $arguments += '/u' }
    $arguments += "`"$Dll`""

    # Start-Process -PassThru is used instead of `& regsvr32.exe` because
    # $LASTEXITCODE is not reliably populated in every PowerShell host.
    $process = Start-Process -FilePath 'regsvr32.exe' -ArgumentList $arguments -Wait -PassThru
    return $process.ExitCode
}

Write-Host ''
Write-Host '  PlaylistPreview preview handler'
Write-Host "  platform     : $Platform"
Write-Host "  configuration: $Configuration"
Write-Host "  source       : $SourceDir"
Write-Host "  install dir  : $InstallDir"
Write-Host ''

if ($Uninstall) {
    if (-not $DryRun -and -not $PerUser -and -not (Test-Elevated)) {
        Write-Host 'Requesting administrator rights...'
        $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"",
            '-Uninstall', '-Platform', $Platform, '-Configuration', $Configuration, '-InstallDir', "`"$InstallDir`"")
        Start-Process -FilePath 'powershell.exe' -ArgumentList $arguments -Verb RunAs
        exit 0
    }

    $dll = Join-Path $InstallDir 'PreviewHandlerShell.dll'
    if (Test-Path -LiteralPath $dll) {
        Write-Host "Unregistering $dll"
        if (-not $DryRun) {
            [void] (Invoke-RegSvr32 -Dll $dll -Unregister)
        }
    }
    else {
        Write-Host "Nothing to unregister at $dll"
        $registered = Get-RegisteredHandlerPath
        if ($registered -and (Test-Path -LiteralPath $registered)) {
            Write-Host "Falling back to the registered copy: $registered"
            if (-not $DryRun) {
                [void] (Invoke-RegSvr32 -Dll $registered -Unregister)
            }
        }
    }

    if ((Test-Path -LiteralPath $InstallDir) -and -not $DryRun) {
        Write-Host "Removing $InstallDir"
        try {
            Remove-Item -LiteralPath $InstallDir -Recurse -Force -ErrorAction Stop
        }
        catch {
            # Almost always a running host (prevhost.exe / a dev harness) still
            # having one of the DLLs mapped.
            Write-Host "  Could not remove every file: $($_.Exception.Message)" -ForegroundColor Yellow
            Write-Host '  Close any window previewing a playlist, then delete the folder manually.' -ForegroundColor Yellow
        }
    }

    # Per-user preference written by the handler itself (last used view mode).
    $preferenceKey = 'HKCU:\Software\PlaylistPreview'
    if ((Test-Path -LiteralPath $preferenceKey) -and -not $DryRun) {
        Write-Host "Removing $preferenceKey"
        Remove-Item -LiteralPath $preferenceKey -Recurse -Force
    }

    Write-Host ''
    Write-Host '  Uninstalled.'
    Write-Host ''
    exit 0
}

# --- install -----------------------------------------------------------------

$missing = @()
foreach ($file in $payload) {
    if (-not (Test-Path -LiteralPath (Join-Path $SourceDir $file))) {
        $missing += $file
    }
}

if ($missing.Count -gt 0) {
    Write-Host '  Missing build output:' -ForegroundColor Red
    $missing | ForEach-Object { Write-Host "    $_" }
    Write-Host ''
    Write-Host '  Build first, for example:'
    Write-Host "    msbuild `"$repoRoot\PlaylistPreview.slnx`" /p:Configuration=$Configuration /p:Platform=$Platform"
    Write-Host ''
    exit 1
}

if (-not $SkipRuntimeCheck) {
    $runtime = Get-InstalledRuntime
    if ($runtime) {
        Write-Host "  Windows App Runtime: $($runtime.PackageFullName)"
    }
    else {
        Write-Host '  No suitable Windows App Runtime found.' -ForegroundColor Yellow
        Write-Host "  Required: $runtimePackageName >= $requiredRuntimeVersion ($architectureName)" -ForegroundColor Yellow
        Write-Host ''
        Write-Host '  The handler is an in-process COM DLL and cannot ship the runtime'
        Write-Host '  itself, so it will not render until the runtime is installed:'
        Write-Host "    $runtimeDownloadUrl"
        Write-Host ''
        Write-Host '  Re-run with -SkipRuntimeCheck to install anyway.'
        Write-Host ''
        exit 2
    }
}

if (-not $DryRun) {
    if (-not $PerUser -and -not (Test-Elevated)) {
        Write-Host 'Requesting administrator rights...'
        $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"",
            '-Platform', $Platform, '-Configuration', $Configuration, '-InstallDir', "`"$InstallDir`"")
        if ($SkipRuntimeCheck) { $arguments += '-SkipRuntimeCheck' }
        if ($RestartExplorer) { $arguments += '-RestartExplorer' }
        Start-Process -FilePath 'powershell.exe' -ArgumentList $arguments -Verb RunAs
        exit 0
    }

    # Clean up a previous registration that points somewhere else (typically the
    # build output directory), otherwise the old path stays in the registry.
    $registered = Get-RegisteredHandlerPath
    $installedDll = Join-Path $InstallDir 'PreviewHandlerShell.dll'
    if ($registered -and ($registered -ne $installedDll) -and (Test-Path -LiteralPath $registered)) {
        Write-Host "Unregistering previous copy: $registered"
        [void] (Invoke-RegSvr32 -Dll $registered -Unregister)
    }

    New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
    foreach ($file in $payload) {
        Copy-Item -LiteralPath (Join-Path $SourceDir $file) -Destination $InstallDir -Force
        Write-Host "  copied $file"
    }
}
else {
    Write-Host '  [dry run] would copy:'
    $payload | ForEach-Object { Write-Host "    $_" }
}

if ($DryRun) {
    Write-Host ''
    $planned = Join-Path $InstallDir 'PreviewHandlerShell.dll'
    Write-Host "  [dry run] would run: regsvr32 /s `"$planned`""
    Write-Host '  Dry run finished; nothing was changed.'
    Write-Host ''
    exit 0
}

$installedDll = Join-Path $InstallDir 'PreviewHandlerShell.dll'
Write-Host "Registering $installedDll"
$regsvrExitCode = Invoke-RegSvr32 -Dll $installedDll
if ($regsvrExitCode -ne 0) {
    Write-Host "  regsvr32 failed with exit code $regsvrExitCode" -ForegroundColor Red
    exit 1
}

$registeredNow = Get-RegisteredHandlerPath
Write-Host ''
Write-Host "  InprocServer32 : $registeredNow"
Write-Host "  Registered for : .cue, .m3u8 (machine wide)"
Write-Host '  Low IL opt-out : DisableLowILProcessIsolation = 1'
Write-Host ''
Write-Host '  Select a .cue or .m3u8 file in Explorer and press Alt+P.'
Write-Host "  Undo with: powershell -File `"$PSScriptRoot\install.ps1`" -Uninstall"
Write-Host ''

if ($RestartExplorer) {
    Write-Host '  Restarting Explorer so the new handler is picked up...'
    Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2
}
else {
    Write-Host '  Tip: close and reopen the Explorer window (or restart Explorer) so the'
    Write-Host '  preview pane picks up the new registration.  -RestartExplorer does it for you.'
    Write-Host ''
}

if ($PerUser) {
    Write-Host ''
    Write-Host '  NOTE: this was a per-user test install (%LOCALAPPDATA%).' -ForegroundColor Yellow
    Write-Host '  Explorer launches the handler through the COM surrogate, which only' -ForegroundColor Yellow
    Write-Host '  sees machine-wide registration, so the preview pane will not use it.' -ForegroundColor Yellow
    Write-Host '  Run without -PerUser (elevated) for a real install.' -ForegroundColor Yellow
    Write-Host ''
}
