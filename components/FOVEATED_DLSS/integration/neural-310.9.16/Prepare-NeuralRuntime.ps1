#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Library,
    [Parameter(Mandatory)][string]$Proxy,
    [Parameter(Mandatory)][ValidatePattern('^[A-Fa-f0-9]{64}$')][string]$ProxySha256,
    [Parameter(Mandatory)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
# Packaging only: never load DLLs, start a helper, or update an installed profile.
$contract = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'contract.json') | ConvertFrom-Json
if ($contract.contract_version -ne 1) { throw 'Unsupported Neural integration contract' }
$libraryPath = (Resolve-Path -LiteralPath $Library).Path
$proxyPath = (Resolve-Path -LiteralPath $Proxy).Path
$libraryHash = (Get-FileHash -LiteralPath $libraryPath).Hash
$proxyHash = (Get-FileHash -LiteralPath $proxyPath).Hash
if ($libraryHash -ne $contract.library_sha256) { throw 'Neural library identity mismatch' }
if ($proxyHash -ne $ProxySha256) { throw 'Proxy identity mismatch' }
if ($proxyHash -eq $libraryHash) { throw 'The real library cannot replace the proxy' }
$destination = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $destination) { throw 'Output must be a new directory; existing installations are never overwritten' }
$null = New-Item -ItemType Directory -Path $destination
$files = @()
foreach ($eye in $contract.eyes) {
    $folder = Join-Path $destination $eye
    $null = New-Item -ItemType Directory -Path $folder
    foreach ($inputFile in @(
        @{ source = $libraryPath; name = $contract.real_filename; sha = $libraryHash },
        @{ source = $proxyPath; name = $contract.proxy_filename; sha = $proxyHash }
    )) {
        $target = Join-Path $folder $inputFile.name
        Copy-Item -LiteralPath $inputFile.source -Destination $target
        if ((Get-FileHash -LiteralPath $target).Hash -ne $inputFile.sha) { throw 'Prepared runtime bytes differ' }
        $files += @{ path = "$eye/$($inputFile.name)"; bytes = (Get-Item -LiteralPath $target).Length; sha256 = $inputFile.sha }
    }
}
[ordered]@{
    status = 'PASS'; contract_version = 1; files = $files; runtime_executed = $false;
    scope = 'Verified two-eye library/proxy input only; host must supply helper, model, dependencies, environment and complete installer'
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $destination 'neural-runtime-receipt.json') -Encoding utf8NoBOM
Write-Host 'PASS: exact patched library behind separately pinned proxy for both eyes; no runtime executed'
