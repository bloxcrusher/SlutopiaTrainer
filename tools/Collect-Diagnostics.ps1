[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$GameDirectory,
    [string]$SaveDirectory,
    [string]$OutputPath = (Join-Path $env:TEMP 'slutopia-diagnostics.json')
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $GameDirectory).Path.TrimEnd('\')
if (-not (Test-Path -LiteralPath $root -PathType Container)) { throw 'GameDirectory must be a directory' }
function Get-PeMachine([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $reader = [IO.BinaryReader]::new($stream)
    try {
        if ($stream.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5A4D) { return 'Not PE' }
        $stream.Position = 0x3c
        $offset = $reader.ReadInt32()
        if ($offset -lt 0 -or $offset -gt $stream.Length - 6) { return 'Invalid PE offset' }
        $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550) { return 'Invalid PE signature' }
        switch ($reader.ReadUInt16()) {
            0x8664 { return 'x64' }
            0x014c { return 'x86' }
            0xAA64 { return 'ARM64' }
            default { return 'Unknown machine' }
        }
    } finally { $reader.Dispose() }
}
$files = @(Get-ChildItem -LiteralPath $root -File -Recurse | ForEach-Object {
    [ordered]@{ RelativePath = $_.FullName.Substring($root.Length + 1); Bytes = $_.Length; ModifiedUtc = $_.LastWriteTimeUtc.ToString('o') }
})
$binaries = @(Get-ChildItem -LiteralPath $root -File -Recurse | Where-Object {
    $_.Extension -eq '.exe' -or $_.Name -in @('UnityPlayer.dll', 'GameAssembly.dll', 'mono-2.0-bdwgc.dll', 'globalgamemanagers', 'boot.config', 'global-metadata.dat')
} | ForEach-Object {
    $machine = if ($_.Extension -in @('.exe', '.dll')) { Get-PeMachine $_.FullName } else { $null }
    [ordered]@{ RelativePath = $_.FullName.Substring($root.Length + 1); SHA256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
        Machine = $machine; FileVersion = $_.VersionInfo.FileVersion; ProductVersion = $_.VersionInfo.ProductVersion }
})
$saves = @()
if ($SaveDirectory) {
    $saveRoot = (Resolve-Path -LiteralPath $SaveDirectory).Path.TrimEnd('\')
    $saves = @(Get-ChildItem -LiteralPath $saveRoot -File -Recurse | ForEach-Object {
        [ordered]@{ RelativePath = $_.FullName.Substring($saveRoot.Length + 1); Bytes = $_.Length
            ModifiedUtc = $_.LastWriteTimeUtc.ToString('o'); SHA256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
    })
}
[ordered]@{ CapturedUtc = [DateTime]::UtcNow.ToString('o'); GameDirectory = $root; Files = $files
    BinaryEvidence = $binaries; SaveMetadata = $saves } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
Write-Output "Metadata saved to $OutputPath. Review local paths before sharing. No file contents were copied."
