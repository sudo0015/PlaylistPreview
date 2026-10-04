<#
.SYNOPSIS
    Adds a UTF-8 BOM to NSIS sources that need one.

.DESCRIPTION
    makensis reads a script as ANSI unless the file starts with a BOM, which
    turns the Chinese UI strings into either mojibake or a hard
    "Bad text encoding" error.  Editors and patch tools drop the BOM on every
    save, so the build normalises the sources before compiling instead of
    relying on it being there.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, ValueFromPipeline = $true)]
    [string[]] $Path
)

process {
    foreach ($item in $Path) {
        if (-not (Test-Path -LiteralPath $item)) { continue }

        $bytes = [System.IO.File]::ReadAllBytes($item)
        if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
            continue
        }

        $text = [System.Text.UTF8Encoding]::new($false).GetString($bytes)
        [System.IO.File]::WriteAllText($item, $text, [System.Text.UTF8Encoding]::new($true))
        Write-Host "  added the UTF-8 BOM: $item"
    }
}
