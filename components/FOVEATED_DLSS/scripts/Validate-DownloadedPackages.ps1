#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Index,
    [Parameter(Mandatory)][string]$DownloadDirectory,
    [Parameter(Mandatory)][string]$EvidenceDirectory,
    [Parameter(Mandatory)][string]$CMake,
    [Parameter(Mandatory)][string]$NgxSdk
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path -Parent $PSScriptRoot
$indexData = Get-Content -Raw -LiteralPath $Index | ConvertFrom-Json
$download = (Resolve-Path -LiteralPath $DownloadDirectory).Path
$null = New-Item -ItemType Directory -Path $EvidenceDirectory -Force
$evidence = (Resolve-Path -LiteralPath $EvidenceDirectory).Path
$ctest = Join-Path (Split-Path -Parent $CMake) 'ctest.exe'
$results = [System.Collections.Generic.List[object]]::new()
foreach ($package in $indexData.packages) {
    $artifact = $package.artifact
    $name = [IO.Path]::GetFileName($artifact.path)
    $zipPath = Join-Path $download $name
    if ((Get-Item -LiteralPath $zipPath).Length -ne $artifact.bytes -or
        (Get-FileHash -LiteralPath $zipPath).Hash -ne $artifact.sha256) { throw "Downloaded bytes differ: $name" }
    $stem = [IO.Path]::GetFileNameWithoutExtension($name)
    $extract = Join-Path $download $stem
    if (Test-Path -LiteralPath $extract) { throw "Extraction already exists: $extract" }
    $prefix = [IO.Path]::GetFullPath($extract) + [IO.Path]::DirectorySeparatorChar
    $archive = [IO.Compression.ZipFile]::OpenRead($zipPath)
    try {
        $seen = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach ($entry in $archive.Entries) {
            $target = [IO.Path]::GetFullPath((Join-Path $extract $entry.FullName))
            if ([IO.Path]::IsPathRooted($entry.FullName) -or $entry.FullName.Contains(':') -or
                -not $target.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) -or
                -not $seen.Add($target)) { throw "Unsafe/duplicate archive entry: $($entry.FullName)" }
        }
    } finally { $archive.Dispose() }
    [IO.Compression.ZipFile]::ExtractToDirectory($zipPath, $extract)
    $manifest = Get-Content -Raw -LiteralPath (Join-Path $extract 'package-manifest.json') | ConvertFrom-Json
    if ($manifest.engine_version -ne $indexData.engine_version -or $manifest.mode -ne $package.mode -or
        $manifest.source_contract -ne $indexData.source_contract -or
        $manifest.configuration_schema -ne $indexData.configuration_schema -or
        $manifest.control_contract -ne $indexData.control_contract -or
        $manifest.experimental_extension -ne $indexData.experimental_extension -or $manifest.runtime_bundled) {
        throw "Downloaded manifest contract differs: $name"
    }
    $files = @(Get-ChildItem -LiteralPath $extract -Recurse -File)
    $expected = @($manifest.files | ForEach-Object { $_.path }) + @('package-manifest.json')
    $actual = @($files | ForEach-Object { [IO.Path]::GetRelativePath($extract, $_.FullName).Replace('\', '/') })
    if (Compare-Object $expected $actual) { throw "Downloaded inventory differs: $name" }
    foreach ($file in $manifest.files) {
        $path = Join-Path $extract $file.path
        if ((Get-Item -LiteralPath $path).Length -ne $file.bytes -or
            (Get-FileHash -LiteralPath $path).Hash -ne $file.sha256) { throw "Manifest file differs: $($file.path)" }
    }
    if (@($files | Where-Object Extension -eq '.dll').Count) { throw 'Unexpected runtime DLL in engine SDK' }
    $consumer = Join-Path $download "$stem-consumer-source"
    Copy-Item -LiteralPath (Join-Path $extract 'examples/consumer') -Destination $consumer -Recurse
    $sdk = ''
    if ($package.mode -eq 'NGX') {
        $sdk = Join-Path $download "$stem-consumer-sdk"
        $lib = Join-Path $sdk 'lib/Windows_x86_64/x64'
        $null = New-Item -ItemType Directory -Path $lib -Force
        Copy-Item -LiteralPath (Join-Path $NgxSdk 'lib/Windows_x86_64/x64/nvsdk_ngx_d.lib') -Destination $lib
        Copy-Item -LiteralPath (Join-Path $NgxSdk 'LICENSE.txt') -Destination $sdk
    }
    $build = Join-Path $download "$stem-consumer-build"
    $commands = @(
        @{ name = 'configure'; tool = $CMake; arguments = @('-S', $consumer, '-B', $build, '-G', 'Visual Studio 17 2022', '-A', 'x64', "-DCMAKE_PREFIX_PATH=$extract", "-DDLSS_FOVEADO_NGX_SDK=$sdk") },
        @{ name = 'build'; tool = $CMake; arguments = @('--build', $build, '--config', 'Release') },
        @{ name = 'tests'; tool = $ctest; arguments = @('--test-dir', $build, '-C', 'Release', '--output-on-failure', '--no-tests=error') }
    )
    $logs = @()
    foreach ($step in $commands) {
        $log = Join-Path $evidence "$stem-$($step.name).log"
        & $step.tool @($step.arguments) *> $log
        if ($LASTEXITCODE -ne 0) { throw "Downloaded consumer failed: $log" }
        $logs += @{ path = [IO.Path]::GetRelativePath($root, $log).Replace('\', '/'); sha256 = (Get-FileHash -LiteralPath $log).Hash }
    }
    $results.Add([ordered]@{ name = $name; mode = $package.mode; bytes = $artifact.bytes;
        sha256 = $artifact.sha256; download_verified = $true; extracted_files_verified = $files.Count;
        independent_consumer = 'PASS 1/1'; logs = $logs })
    Write-Host "PASS downloaded $($package.mode): bytes, manifest and independent consumer"
}
$sumsPath = Join-Path $download 'SHA256SUMS.txt'
$sums = Get-Content -LiteralPath $sumsPath
if ($sums.Count -ne $results.Count) { throw 'Checksum list length differs' }
foreach ($item in $results) {
    if ($sums -notcontains "$($item.sha256.ToLowerInvariant())  $($item.name)") { throw "Checksum list missing $($item.name)" }
}
Copy-Item -LiteralPath $sumsPath -Destination $evidence
[ordered]@{ status = 'PASS'; recorded_at_utc = [DateTime]::UtcNow.ToString('o');
    engine_version = $indexData.engine_version; game_or_vr_started = $false;
    real_ngx_inference = $false; assets = $results;
    checksum_file_sha256 = (Get-FileHash -LiteralPath $sumsPath).Hash } |
    ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $evidence 'download-verification.json') -Encoding utf8NoBOM
