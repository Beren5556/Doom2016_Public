param([switch]$CheckOnly)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$utf8=New-Object Text.UTF8Encoding($false)
function HashFile([string]$path){$sha=[Security.Cryptography.SHA256]::Create();try{return ([BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes($path)))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}}
$own=Join-Path $root 'DoomLayer'
$python=Join-Path $root 'runtimes/python/python.exe'
$node=Join-Path $root 'runtimes/node/node.exe'
$modules=Join-Path $root 'runtimes/node/node_modules'
foreach($file in @($python,$node,(Join-Path $root 'KharvoxLauncher.exe'),(Join-Path $root 'KharvoxLayer.dll'))){if(!(Test-Path -LiteralPath $file)){throw ('Missing portable file: '+$file)}}
[IO.File]::WriteAllText((Join-Path $own 'python.cfg'),$python,$utf8)
[IO.File]::WriteAllText((Join-Path $own 'runtime.json'),(@{node=$node;modules=$modules}|ConvertTo-Json),$utf8)
$layerPath=Join-Path $root 'KharvoxLayer.json'
$layer=Get-Content -LiteralPath $layerPath -Raw | ConvertFrom-Json
$layer.layer.library_path=Join-Path $root 'KharvoxLayer.dll'
[IO.File]::WriteAllText($layerPath,($layer|ConvertTo-Json -Depth 8),$utf8)
$inventoryPath=Join-Path $own 'inventory.json'
$inventory=Get-Content -LiteralPath $inventoryPath -Raw | ConvertFrom-Json
foreach($name in @('python.cfg','runtime.json')){$inventory.files | Add-Member -NotePropertyName $name -NotePropertyValue (HashFile (Join-Path $own $name)) -Force}
if((HashFile (Join-Path $root 'KharvoxLayer.dll')) -ne $inventory.engineLibrarySha256){throw 'Engine identity mismatch'}
foreach($item in $inventory.files.PSObject.Properties){$file=Join-Path $own $item.Name;if((HashFile $file) -ne $item.Value){throw ('Runtime identity mismatch: '+$item.Name)}}
[IO.File]::WriteAllText($inventoryPath,($inventory|ConvertTo-Json -Depth 10),$utf8)
& $python -I -c "import sys, PIL; print('PASS portable Python and Pillow')"
if($LASTEXITCODE){throw 'Portable Python failed'}
& $node -e "require(process.argv[1]); console.log('PASS portable Playwright')" (Join-Path $modules 'playwright')
if($LASTEXITCODE){throw 'Portable Node failed'}
Write-Output 'PASS portable paths and runtime identities'
if(!$CheckOnly){Start-Process -FilePath (Join-Path $root 'KharvoxLauncher.exe') -WorkingDirectory $root}
