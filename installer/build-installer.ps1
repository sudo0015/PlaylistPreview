<#
.SYNOPSIS
    Builds PlaylistPreview and packs one NSIS installer per architecture.

.DESCRIPTION
    Produces installer\dist\PlaylistPreview-<version>-<arch>-setup.exe for every
    supported architecture (x64, x86, ARM64) in a single run.  Use -Arch to
    restrict the run to a subset.

    Each package carries that architecture's payload, registers the handler in
    that architecture's registry view and checks the matching Windows App
    Runtime / VC++ runtime:

        x64    -> artifacts\x64\Release     + 64-bit registry view
        x86    -> artifacts\Win32\Release   + 32-bit (WOW64) view
        ARM64  -> artifacts\ARM64\Release   + 64-bit registry view

    x64 and x86 can be installed side by side (different registry views and
    different Program Files roots); ARM64 gets its own directory because it
    shares Program Files with x64.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File installer\build-installer.ps1 -Version 1.2.0

.EXAMPLE
    # Only x64, packaging an existing build
    powershell -ExecutionPolicy Bypass -File installer\build-installer.ps1 -Arch x64 -SkipBuild
#>
[CmdletBinding()]
param(
    [ValidateSet('x64', 'x86', 'arm64')]
    [string[]] $Arch = @('x64', 'x86', 'arm64'),
    [string] $Version,
    [ValidateSet('Debug', 'Release')] [string] $Configuration = 'Release',
    [string] $NsisDir,
    [switch] $SkipBuild
)

$ErrorActionPreference = 'Stop'

$repoRoot    = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$scriptPath  = Join-Path $PSScriptRoot 'PlaylistPreview.nsi'
$definesPath = Join-Path $PSScriptRoot 'build-defines.nsh'
$licensePath = Join-Path $repoRoot 'LICENSE'
$distDir     = Join-Path $PSScriptRoot 'dist'
$solution    = Join-Path $repoRoot 'PlaylistPreview.slnx'

# Name -> the MSBuild solution platform and the output directory it maps to.
# "x86" is the solution platform; the projects call it Win32, which is also the
# artifacts directory name.
$archTable = [ordered]@{
    'x64'   = @{ SolutionPlatform = 'x64';   PayloadDir = 'x64' }
    'x86'   = @{ SolutionPlatform = 'x86';   PayloadDir = 'Win32' }
    'arm64' = @{ SolutionPlatform = 'ARM64'; PayloadDir = 'ARM64' }
}

$payloadFiles = @(
    'PreviewHandlerShell.dll',
    'PlaylistPreviewUI.dll',
    'Microsoft.WindowsAppRuntime.Bootstrap.dll'
)

function Write-Step([string] $Text) {
    Write-Host ''
    Write-Host "== $Text"
}

function Get-GitLine {
    param([string[]] $Arguments)

    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $lines = & git -C $repoRoot @Arguments 2>$null
        if ($LASTEXITCODE -ne 0) { return $null }
        return ($lines | Select-Object -First 1)
    }
    catch {
        return $null
    }
    finally {
        $ErrorActionPreference = $previous
    }
}

function Get-ProductVersion {
    # The repository records the release in the commit subject ("v1.1.1 ..."),
    # so try a real tag first and fall back to the message of the current commit.
    foreach ($raw in @((Get-GitLine @('describe', '--tags', '--abbrev=0')),
                       (Get-GitLine @('log', '-1', '--pretty=%s')))) {
        if (-not $raw) { continue }
        $match = [regex]::Match($raw.Trim(), 'v?(\d+(?:\.\d+){1,3})')
        if ($match.Success -and $match.Index -le 1) { return $match.Groups[1].Value }
    }
    return '1.0.0'
}

function Resolve-MsBuild {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $found = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild `
                    -find 'MSBuild\**\Bin\amd64\MSBuild.exe' 2>$null | Select-Object -First 1
        if ($found -and (Test-Path -LiteralPath $found)) { return $found }
    }

    $command = Get-Command msbuild.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }

    throw 'MSBuild not found. Install the "Desktop development with C++" workload or pass -SkipBuild to package an existing build.'
}

function Resolve-Makensis {
    param([string] $Hint)

    $candidates = New-Object System.Collections.Generic.List[string]

    if ($Hint) {
        $candidates.Add($(if (Test-Path -LiteralPath $Hint -PathType Container) { Join-Path $Hint 'makensis.exe' } else { $Hint }))
    }
    if ($env:NSIS_DIR) {
        $candidates.Add((Join-Path $env:NSIS_DIR 'makensis.exe'))
    }

    $command = Get-Command makensis.exe -ErrorAction SilentlyContinue
    if ($command) { $candidates.Add($command.Source) }

    foreach ($key in 'HKLM:\SOFTWARE\NSIS', 'HKLM:\SOFTWARE\WOW6432Node\NSIS') {
        $install = Get-ItemProperty -Path $key -ErrorAction SilentlyContinue
        if ($install -and $install.'(default)') {
            $candidates.Add((Join-Path $install.'(default)' 'makensis.exe'))
        }
    }

    foreach ($guess in 'C:\Program Files (x86)\NSIS\makensis.exe',
                       'C:\Program Files\NSIS\makensis.exe',
                       'D:\NSIS\makensis.exe',
                       'E:\NSIS\makensis.exe') {
        $candidates.Add($guess)
    }

    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) { return $candidate }
    }

    throw 'makensis.exe not found. Install NSIS or pass -NsisDir <path> (or set $env:NSIS_DIR).'
}

# --- version ----------------------------------------------------------------

if (-not $Version) { $Version = Get-ProductVersion }
if ($Version -notmatch '^\d+(\.\d+){1,3}$') {
    throw "Version '$Version' must look like 1.2.3 (VIProductVersion needs four numbers; the build appends .0)."
}

$requested = @($Arch | ForEach-Object { $_.ToLowerInvariant() } | Select-Object -Unique)

Write-Host ''
Write-Host '  PlaylistPreview installer build'
Write-Host "  version      : $Version"
Write-Host "  configuration: $Configuration"
Write-Host "  architectures: $($requested -join ', ')"

if (-not (Test-Path -LiteralPath $distDir)) {
    New-Item -ItemType Directory -Path $distDir -Force | Out-Null
}

# --- compile -----------------------------------------------------------------

if (-not $SkipBuild) {
    $msbuild = Resolve-MsBuild
    Write-Host "  msbuild      : $msbuild"

    foreach ($name in $requested) {
        $platform = $archTable[$name].SolutionPlatform
        Write-Step "Building $name (solution platform $platform)"

        & $msbuild $solution "/p:Configuration=$Configuration" "/p:Platform=$platform" `
            /m /nologo /v:m /clp:Summary

        if ($LASTEXITCODE -ne 0) { throw "MSBuild failed for $name with exit code $LASTEXITCODE." }
    }
}
else {
    Write-Step 'Skipping the build (-SkipBuild)'
}

# --- package each architecture ----------------------------------------------

$makensis = Resolve-Makensis -Hint $NsisDir
Write-Host ''
Write-Host "  makensis     : $makensis"

& (Join-Path $PSScriptRoot 'ensure-utf8bom.ps1') -Path `
    $scriptPath, (Join-Path $PSScriptRoot 'prerequisites.nsh')

$results = New-Object System.Collections.Generic.List[object]

foreach ($name in $requested) {
    $payloadDir = Join-Path $repoRoot "artifacts\$($archTable[$name].PayloadDir)\$Configuration"
    Write-Step "Packaging $name"

    $missing = @($payloadFiles | Where-Object { -not (Test-Path -LiteralPath (Join-Path $payloadDir $_)) })
    if ($missing.Count -gt 0) {
        Write-Host "  Missing $Configuration|$name output in $payloadDir :" -ForegroundColor Red
        $missing | ForEach-Object { Write-Host "    $_" }
        throw "Build $name first (or drop -SkipBuild)."
    }

    foreach ($file in $payloadFiles) {
        $info = Get-Item -LiteralPath (Join-Path $payloadDir $file)
        Write-Host ("  {0,-46} {1,10:N0} bytes" -f $file, $info.Length)
    }

    $outFile = Join-Path $distDir "PlaylistPreview-$Version-$name-setup.exe"
    $defines = @(
        '; Generated by build-installer.ps1 - do not edit, do not commit.'
        "!define ARCH `"$name`""
        "!define APP_VERSION `"$Version`""
        "!define SOURCE_DIR `"$payloadDir`""
        "!define LICENSE_FILE `"$licensePath`""
        "!define OUT_FILE `"$outFile`""
    )
    [System.IO.File]::WriteAllLines($definesPath, $defines, [System.Text.UTF8Encoding]::new($false))
    Write-Host '  defines:'
    $defines | Select-Object -Skip 1 | ForEach-Object { Write-Host "    $_" }

    & $makensis /V2 $scriptPath
    if ($LASTEXITCODE -ne 0) { throw "makensis failed for $name with exit code $LASTEXITCODE." }
    if (-not (Test-Path -LiteralPath $outFile)) { throw "Expected output $outFile was not produced." }

    $results.Add([pscustomobject]@{
        Arch   = $name
        File   = $outFile
        Size   = (Get-Item -LiteralPath $outFile).Length
        Sha256 = (Get-FileHash -LiteralPath $outFile -Algorithm SHA256).Hash
    })
}

# --- summary ----------------------------------------------------------------

Write-Host ''
Write-Host '  Installers ready' -ForegroundColor Green
foreach ($result in $results) {
    Write-Host ''
    Write-Host ("    {0,-6} {1}" -f $result.Arch, $result.File)
    Write-Host ("           {0:N0} bytes" -f $result.Size)
    Write-Host ("           sha256 {0}" -f $result.Sha256)
}
Write-Host ''
