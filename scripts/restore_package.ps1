param([string]$Installer,[string]$Destination)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if(!$Installer){$Installer=Join-Path $root 'dist/DOOM2016VRDLSS-Installer.exe'}
if(!$Destination){$Destination=Join-Path $root '_package/restored'}
$Destination=[IO.Path]::GetFullPath($Destination)
if(Test-Path -LiteralPath $Destination){throw 'Choose a new extraction directory'}
New-Item -ItemType Directory -Path $Destination | Out-Null
$assembly=[Reflection.Assembly]::LoadFile([IO.Path]::GetFullPath($Installer))
foreach($name in @('payload.zip','package.json')){
    $input=$assembly.GetManifestResourceStream($name)
    $output=[IO.File]::Create((Join-Path $Destination $name))
    try{$input.CopyTo($output)}finally{$output.Dispose();$input.Dispose()}
}
$manifest=Get-Content -LiteralPath (Join-Path $Destination 'package.json') -Raw | ConvertFrom-Json
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip=[IO.Compression.ZipFile]::OpenRead((Join-Path $Destination 'payload.zip'))
$runtime=Join-Path $Destination 'runtime'
try {
    foreach($entry in $zip.Entries){
        $name=$entry.FullName
        $expected=$manifest.Files.PSObject.Properties[$name].Value
        $target=[IO.Path]::GetFullPath((Join-Path $runtime $name))
        if(!$expected -or !$target.StartsWith($runtime+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Invalid package entry'}
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$target,$false)
        if((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected){throw 'Package hash mismatch'}
    }
}finally{$zip.Dispose()}
Write-Output ('Restored verified package: '+$Destination)
