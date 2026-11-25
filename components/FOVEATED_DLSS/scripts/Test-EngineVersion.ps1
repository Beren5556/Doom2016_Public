#requires -Version 7.0
param([Parameter(Mandatory)][string]$CMake, [Parameter(Mandatory)][string]$RunId)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path -Parent $PSScriptRoot
$project = Get-Content -Raw -LiteralPath (Join-Path $root 'CMakeLists.txt')
if ($project -notmatch 'project\(DlssFoveado VERSION ([0-9]+\.[0-9]+\.[0-9]+)') { throw 'CMake version missing' }
$version = $Matches[1]
if ($RunId -notmatch '^[a-zA-Z0-9_-]+$') { throw 'Invalid RunId' }
$work = Join-Path $root "out/version-check/$RunId"
$evidence = Join-Path $root "evidence/version-check/$RunId"
if ((Test-Path -LiteralPath $work) -or (Test-Path -LiteralPath $evidence)) { throw 'Run already exists' }
$null = New-Item -ItemType Directory -Path $work, $evidence
$header = [IO.File]::ReadAllText((Join-Path $root 'include/dlss_foveado/Foveation.h'))
$provenance = [IO.File]::ReadAllText((Join-Path $root 'provenance.json'))
$cases = @(
    @{ name = 'matching'; header = $header; provenance = $provenance; expected = $version; pass = $true },
    @{ name = 'old-header'; header = $header.Replace("EngineVersion = `"$version`"", 'EngineVersion = "1.1.0"'); provenance = $provenance; expected = $version; pass = $false },
    @{ name = 'old-provenance'; header = $header; provenance = $provenance.Replace("`"engine_version`": `"$version`"", '"engine_version": "1.1.0"'); expected = $version; pass = $false },
    @{ name = 'wrong-cmake'; header = $header; provenance = $provenance; expected = '1.1.0'; pass = $false }
)
$results = @()
foreach ($case in $cases) {
    $fixture = Join-Path $work $case.name
    $null = New-Item -ItemType Directory -Path (Join-Path $fixture 'include/dlss_foveado')
    [IO.File]::WriteAllText((Join-Path $fixture 'include/dlss_foveado/Foveation.h'), $case.header)
    [IO.File]::WriteAllText((Join-Path $fixture 'provenance.json'), $case.provenance)
    $log = Join-Path $evidence "$($case.name).log"
    & $CMake "-DDLSS_FOVEADO_SOURCE_DIR=$fixture" "-DDLSS_FOVEADO_EXPECTED_VERSION=$($case.expected)" -P (Join-Path $root 'cmake/CheckEngineVersion.cmake') *> $log
    $code = $LASTEXITCODE
    if (($code -eq 0) -ne $case.pass) { throw "Unexpected version-check result: $($case.name)" }
    $text = Get-Content -Raw -LiteralPath $log
    if (-not $case.pass -and $text -notmatch 'Engine version mismatch') { throw 'Mismatch rejected for wrong reason' }
    $results += @{ case = $case.name; exit_code = $code; expected_success = $case.pass; status = 'PASS'; log = "$($case.name).log"; sha256 = (Get-FileHash -LiteralPath $log).Hash }
    Write-Host "PASS identity fixture $($case.name)"
}
@{ status = 'PASS'; engine_version = $version; cases = $results } | ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath (Join-Path $evidence 'receipt.json') -Encoding utf8NoBOM
