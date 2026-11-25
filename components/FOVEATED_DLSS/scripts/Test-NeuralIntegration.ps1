#requires -Version 7.0
param([Parameter(Mandatory)][string]$Library, [Parameter(Mandatory)][string]$RunId)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($RunId -notmatch '^[a-zA-Z0-9_-]+$') { throw 'Invalid RunId' }
$root = Split-Path -Parent $PSScriptRoot
$work = Join-Path $root "out/neural-integration/$RunId"
$evidence = Join-Path $root "evidence/neural-integration/$RunId"
if ((Test-Path -LiteralPath $work) -or (Test-Path -LiteralPath $evidence)) { throw 'Run exists' }
$null = New-Item -ItemType Directory -Path $work, $evidence
# Deliberately non-executable proxy fixture: this test covers packaging identities,
# not vendor loading, host transport or inference.
$proxy = Join-Path $work 'fixture-proxy.dll'
[IO.File]::WriteAllText($proxy, 'non-executable proxy fixture for identity/packaging test')
$hash = (Get-FileHash -LiteralPath $proxy).Hash
$prepare = Join-Path $root 'integration/neural-310.9.16/Prepare-NeuralRuntime.ps1'
$cases = @()
function Reject([string]$Name, [hashtable]$Arguments, [string]$Reason) {
    $caught = $false
    try { & $prepare @Arguments } catch { if ($_.Exception.Message -notlike "*$Reason*") { throw }; $caught = $true }
    if (-not $caught) { throw "Expected rejection: $Name" }
    $script:cases += @{ name=$Name; status='PASS'; expected_rejection=$Reason }
}
$target = Join-Path $work 'prepared'
Reject 'wrong-library' @{Library=$proxy;Proxy=$proxy;ProxySha256=$hash;OutputDirectory=$target} 'Neural library identity mismatch'
Reject 'wrong-proxy' @{Library=$Library;Proxy=$proxy;ProxySha256=('0'*64);OutputDirectory=$target} 'Proxy identity mismatch'
$realHash = (Get-FileHash -LiteralPath $Library).Hash
Reject 'real-as-proxy' @{Library=$Library;Proxy=$Library;ProxySha256=$realHash;OutputDirectory=$target} 'cannot replace the proxy'
& $prepare -Library $Library -Proxy $proxy -ProxySha256 $hash -OutputDirectory $target
$receipt = Get-Content -Raw -LiteralPath (Join-Path $target 'neural-runtime-receipt.json') | ConvertFrom-Json
if ($receipt.status -ne 'PASS' -or $receipt.files.Count -ne 4) { throw 'Incomplete runtime preparation' }
Reject 'existing-output' @{Library=$Library;Proxy=$proxy;ProxySha256=$hash;OutputDirectory=$target} 'Output must be a new directory'
$cases += @{name='two-eye-exact-library';status='PASS'}
@{status='PASS';cases=$cases;library_sha256=$realHash;proxy='non-executable fixture';runtime_executed=$false;
 script_sha256=(Get-FileHash -LiteralPath $prepare).Hash;files=$receipt.files} |
 ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $evidence 'receipt.json') -Encoding utf8NoBOM
Write-Host 'PASS: five Neural packaging cases; exact real library, fixture proxy, no inference'
