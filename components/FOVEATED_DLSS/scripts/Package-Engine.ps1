#requires -Version 7.0
[CmdletBinding()]
param(
    [ValidateSet('Core', 'D3D11', 'NGX')][string]$Mode = 'NGX',
    [string]$CMake,
    [string]$NgxSdk,
    [ValidatePattern('^[a-zA-Z0-9][a-zA-Z0-9_-]{0,79}$')][string]$RunId =
        ((Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path -Parent $PSScriptRoot
$provenance = Get-Content -Raw -LiteralPath (Join-Path $root 'provenance.json') | ConvertFrom-Json
if (-not $CMake) {
    $command = Get-Command cmake -ErrorAction SilentlyContinue
    if ($command) { $CMake = $command.Source }
    else {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        if (Test-Path -LiteralPath $vswhere) {
            $found = @(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe')
            if ($found.Count) { $CMake = $found[0] }
        }
    }
}
if (-not $CMake -or -not (Test-Path -LiteralPath $CMake)) { throw 'Supply -CMake or install CMake/Visual Studio C++ tools.' }
$CMake = (Resolve-Path -LiteralPath $CMake).Path
$ctest = Join-Path (Split-Path -Parent $CMake) 'ctest.exe'
if (-not (Test-Path -LiteralPath $ctest)) { throw 'ctest.exe must be beside cmake.exe.' }
if (-not $NgxSdk) { $NgxSdk = Join-Path $root 'dependencies/SDK/NVIDIA-DLSS-310.9.1' }
if ($Mode -eq 'NGX') {
    $NgxSdk = (Resolve-Path -LiteralPath $NgxSdk).Path
    foreach ($item in @('include/nvsdk_ngx.h', 'lib/Windows_x86_64/x64/nvsdk_ngx_d.lib', 'LICENSE.txt')) {
        if (-not (Test-Path -LiteralPath (Join-Path $NgxSdk $item))) { throw "Missing SDK input: $item" }
    }
}
$runName = "$RunId-$($Mode.ToLowerInvariant())"
$work = Join-Path $root "out/packages/$runName"
$evidence = Join-Path $root "evidence/package-validation/$runName"
if ((Test-Path -LiteralPath $work) -or (Test-Path -LiteralPath $evidence)) { throw 'Run already exists; use a new RunId.' }
$null = New-Item -ItemType Directory -Path $work, $evidence
$build = Join-Path $work 'build'
$stage = Join-Path $work 'stage'
$extracted = Join-Path $work 'extracted'
$steps = [System.Collections.Generic.List[object]]::new()
function Invoke-Step([string]$Name, [string]$Tool, [string[]]$Arguments) {
    $log = Join-Path $evidence "$Name.log"
    & $Tool @Arguments *> $log
    $code = $LASTEXITCODE
    $steps.Add([ordered]@{ name = $Name; exit_code = $code; log = "$Name.log"; sha256 = (Get-FileHash -LiteralPath $log).Hash })
    if ($code -ne 0) { throw "$Name failed ($code). See $log" }
    Write-Host "PASS $Name"
}
function File-Inventory([string]$Directory) {
    @(Get-ChildItem -LiteralPath $Directory -File -Recurse | Sort-Object FullName | ForEach-Object {
        [ordered]@{ path = [IO.Path]::GetRelativePath($Directory, $_.FullName).Replace('\', '/');
            bytes = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName).Hash }
    })
}
$receipt = [ordered]@{
    status = 'RUNNING'; recorded_at_utc = [DateTime]::UtcNow.ToString('o'); mode = $Mode;
    engine_version = $provenance.engine_version; source_contract = $provenance.public_contract_version;
    configuration_schema = $provenance.configuration_schema; configuration = 'Release'; architecture = 'x64';
    cmake = $CMake; cmake_version = ((& $CMake --version) -join "`n");
    script_sha256 = (Get-FileHash -LiteralPath $PSCommandPath).Hash;
    game_or_vr_started = $false; real_ngx_inference_run = $false; steps = $steps
}
try {
    # Verify the immutable imported snapshot, then separately inventory the current source.
    # A reviewed API/schema migration may change current files, never the old receipt.
    $origin = Get-Content -Raw -LiteralPath (Join-Path $root 'evidence/origin/portable-module-receipt.json') | ConvertFrom-Json
    $baselineCommit = '7e8327b6dc91b987781e6ed47243a57d1d1327cc'
    $baselinePaths = @($origin.source_sha256.PSObject.Properties | ForEach-Object {
        if ($_.Name -eq 'README.md') { 'docs/origin/engine-0.3.0-README.md' } else { $_.Name }
    })
    $git = (Get-Command git -ErrorAction Stop).Source
    $baselineZip = Join-Path $work 'origin-snapshot.zip'
    Invoke-Step 'baseline-export' $git (@('-C', $root, 'archive', '--format=zip', '--output', $baselineZip, $baselineCommit, '--') + $baselinePaths)
    $baselineDirectory = Join-Path $work 'origin-snapshot'
    [IO.Compression.ZipFile]::ExtractToDirectory($baselineZip, $baselineDirectory)
    foreach ($entry in $origin.source_sha256.PSObject.Properties) {
        $path = if ($entry.Name -eq 'README.md') { 'docs/origin/engine-0.3.0-README.md' } else { $entry.Name }
        if ((Get-FileHash -LiteralPath (Join-Path $baselineDirectory $path)).Hash -ne $entry.Value) {
            throw "Accepted baseline snapshot differs: $path."
        }
    }
    $receipt.original_snapshot_commit = $baselineCommit
    $receipt.current_sources_equal_original = $false
    $receipt.original_entries_verified = @($origin.source_sha256.PSObject.Properties).Count
    $sourcePaths = @('CMakeLists.txt', 'provenance.json', 'core', 'shaders', 'backends', 'include', 'cmake', 'tests', 'examples/consumer', 'integration')
    $sourceFiles = @($sourcePaths | ForEach-Object { Get-ChildItem -LiteralPath (Join-Path $root $_) -File -Recurse } | Sort-Object FullName)
    $receipt.source_inputs = @($sourceFiles | ForEach-Object { [ordered]@{
        path = [IO.Path]::GetRelativePath($root, $_.FullName).Replace('\', '/'); sha256 = (Get-FileHash -LiteralPath $_.FullName).Hash
    } })
    if ($Mode -eq 'NGX') { $receipt.sdk_inputs = File-Inventory $NgxSdk }
    $d3d = if ($Mode -eq 'Core') { 'OFF' } else { 'ON' }
    $sdkArgument = if ($Mode -eq 'NGX') { $NgxSdk } else { '' }
    Invoke-Step 'configure' $CMake @('-S', $root, '-B', $build, '-G', 'Visual Studio 17 2022', '-A', 'x64',
        "-DDLSS_FOVEADO_BUILD_D3D11=$d3d", '-DDLSS_FOVEADO_BUILD_TESTS=ON', "-DDLSS_FOVEADO_NGX_SDK=$sdkArgument")
    Invoke-Step 'build' $CMake @('--build', $build, '--config', 'Release')
    Invoke-Step 'tests' $ctest @('--test-dir', $build, '-C', 'Release', '--output-on-failure', '--no-tests=error')
    Invoke-Step 'install' $CMake @('--install', $build, '--config', 'Release', '--prefix', $stage)
    $null = New-Item -ItemType Directory -Path (Join-Path $stage 'docs')
    foreach ($doc in @('BUILD_AND_PACKAGE.md', 'COMPATIBILITY.md', 'DEPENDENCIES.md', 'CONTROL_CONTRACT_1.0.md', 'CONTROL_CONTRACT_2.0.md', 'EXPERIMENTAL_CONTRACT_1.0.md', 'MIGRATION_1.3.0.md', 'NEURAL_INTEGRATION_310.9.16.md')) {
        Copy-Item -LiteralPath (Join-Path $root "docs/$doc") -Destination (Join-Path $stage 'docs')
    }
    Copy-Item -LiteralPath (Join-Path $root 'integration') -Destination (Join-Path $stage 'integration') -Recurse
    Copy-Item -LiteralPath (Join-Path $root 'provenance.json') -Destination $stage
    $engineReadme = Get-Content -Raw -LiteralPath (Join-Path $root 'README.md')
    $guideStart = $engineReadme.IndexOf('## Boundaries')
    if ($guideStart -lt 0) { throw 'Engine guide section is missing.' }
    ("# Engine $($provenance.engine_version) integration guide`n`n" + $engineReadme.Substring($guideStart)) |
        Set-Content -LiteralPath (Join-Path $stage 'docs/ENGINE_GUIDE.md') -Encoding utf8NoBOM
    @"
# FoveatedDLSS installed development package

The package version, mode, architecture and file hashes are in package-manifest.json.
This contains engine $($provenance.engine_version), source contract $($provenance.public_contract_version), configuration schema $($provenance.configuration_schema) (reads 1/2/3).
Layer control 2 and opt-in experimental extension 1 are documented below.
Four-side geometry requires an explicit binocular projection from the host.

- [Build and consumption](docs/BUILD_AND_PACKAGE.md)
- [Host integration and accepted behavior](docs/ENGINE_GUIDE.md)
- [Compatibility](docs/COMPATIBILITY.md)
- [Dependencies](docs/DEPENDENCIES.md)
- [Layer control contract](docs/CONTROL_CONTRACT_2.0.md)
- [Experimental capabilities](docs/EXPERIMENTAL_CONTRACT_1.0.md)
- [1.3 migration and consumer policy](docs/MIGRATION_1.3.0.md)
- [Neural310.9.16 integration kit](docs/NEURAL_INTEGRATION_310.9.16.md)

The independent CMake consumer is included at examples/consumer. Build against
this extracted prefix as described in the guide. This is a static-library SDK,
not a game plugin or UEVR profile. Runtime NGX and Neural providers are host supplied.
"@ | Set-Content -LiteralPath (Join-Path $stage 'README.md') -Encoding utf8NoBOM
    $null = New-Item -ItemType Directory -Path (Join-Path $stage 'examples')
    Copy-Item -LiteralPath (Join-Path $root 'examples/consumer') -Destination (Join-Path $stage 'examples/consumer') -Recurse
    if ($Mode -eq 'NGX') {
        Copy-Item -LiteralPath (Join-Path $NgxSdk 'LICENSE.txt') -Destination (Join-Path $stage 'NVIDIA-SDK-LICENSE.txt')
    }
    if (@(Get-ChildItem -LiteralPath $stage -File -Recurse -Filter '*.dll').Count) { throw 'Unexpected runtime DLL in portable package.' }
    $rootForms = @($root, $root.Replace('\', '/'))
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $stage 'lib/cmake') -Recurse -File) {
        $content = Get-Content -Raw -LiteralPath $file.FullName
        foreach ($form in $rootForms) { if ($content.Contains($form)) { throw "Export contains source workspace path: $($file.Name)" } }
    }
    $manifest = [ordered]@{ engine_version = $provenance.engine_version; source_contract = $provenance.public_contract_version;
        configuration_schema = $provenance.configuration_schema; control_contract = $provenance.control_contract_version;
        experimental_extension = $provenance.experimental_extension_version; mode = $Mode; configuration = 'Release'; architecture = 'x64';
        runtime_bundled = $false; files = @(File-Inventory $stage) }
    $manifestPath = Join-Path $stage 'package-manifest.json'
    $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding utf8NoBOM
    Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $evidence 'package-manifest.json')
    $zip = Join-Path $work "FoveatedDLSS-$($provenance.engine_version)-$Mode-win-x64-Release.zip"
    [IO.Compression.ZipFile]::CreateFromDirectory($stage, $zip)
    [IO.Compression.ZipFile]::ExtractToDirectory($zip, $extracted)
    $expectedNames = @($manifest.files | ForEach-Object { $_.path }) + @('package-manifest.json')
    $actualNames = @(File-Inventory $extracted | ForEach-Object { $_.path })
    if (Compare-Object $expectedNames $actualNames) { throw 'Archive inventory differs after extraction.' }
    foreach ($entry in $manifest.files) {
        $file = Join-Path $extracted $entry.path
        if ((Get-Item -LiteralPath $file).Length -ne $entry.bytes -or (Get-FileHash -LiteralPath $file).Hash -ne $entry.sha256) {
            throw "Extracted file differs: $($entry.path)"
        }
    }
    if ((Get-FileHash -LiteralPath (Join-Path $extracted 'package-manifest.json')).Hash -ne (Get-FileHash -LiteralPath $manifestPath).Hash) { throw 'Extracted manifest differs.' }
    $consumerSource = Join-Path $work 'consumer-source'
    Copy-Item -LiteralPath (Join-Path $extracted 'examples/consumer') -Destination $consumerSource -Recurse
    $consumerSdk = ''
    if ($Mode -eq 'NGX') {
        $consumerSdk = Join-Path $work 'consumer-sdk'
        $libDir = Join-Path $consumerSdk 'lib/Windows_x86_64/x64'
        $null = New-Item -ItemType Directory -Path $libDir -Force
        Copy-Item -LiteralPath (Join-Path $NgxSdk 'lib/Windows_x86_64/x64/nvsdk_ngx_d.lib') -Destination $libDir
        Copy-Item -LiteralPath (Join-Path $NgxSdk 'LICENSE.txt') -Destination $consumerSdk
    }
    $consumerBuild = Join-Path $work 'consumer-build'
    Invoke-Step 'consumer-configure' $CMake @('-S', $consumerSource, '-B', $consumerBuild, '-G', 'Visual Studio 17 2022', '-A', 'x64',
        "-DCMAKE_PREFIX_PATH=$extracted", "-DDLSS_FOVEADO_NGX_SDK=$consumerSdk")
    Invoke-Step 'consumer-build' $CMake @('--build', $consumerBuild, '--config', 'Release')
    Invoke-Step 'consumer-tests' $ctest @('--test-dir', $consumerBuild, '-C', 'Release', '--output-on-failure', '--no-tests=error')
    foreach ($entry in $receipt.source_inputs) {
        if ((Get-FileHash -LiteralPath (Join-Path $root $entry.path)).Hash -ne $entry.sha256) { throw "Source changed during validation: $($entry.path)" }
    }
    $receipt.artifact = [ordered]@{ path = [IO.Path]::GetRelativePath($root, $zip).Replace('\', '/');
        bytes = (Get-Item -LiteralPath $zip).Length; sha256 = (Get-FileHash -LiteralPath $zip).Hash }
    $receipt.extracted_files_verified = $actualNames.Count
    $testResults = @(Get-Content -LiteralPath (Join-Path $evidence 'tests.log') | Select-String '([0-9]+)% tests passed, ([0-9]+) tests failed out of ([0-9]+)')
    if ($testResults.Count -ne 1 -or $testResults[0].Matches[0].Groups[2].Value -ne '0') { throw 'Missing successful CTest summary.' }
    $receipt.ctest_passed = [int]$testResults[0].Matches[0].Groups[3].Value
    $receipt.installed_external_consumer_passed = 1
    $receipt.status = 'PASS'
    Write-Host "PASS package $zip"
} catch {
    $receipt.status = 'FAIL'
    $receipt.error = $_.Exception.Message
    throw
} finally {
    $receipt | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $evidence 'receipt.json') -Encoding utf8NoBOM
}
