param([Parameter(Mandatory=$true)][string]$PackageDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$package=[IO.Path]::GetFullPath($PackageDirectory)
$csc=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
$source=Join-Path $root 'installer/Installer.cs'
$dist=Join-Path $root 'dist'
New-Item -ItemType Directory -Path $dist -Force | Out-Null
$refs=@('/reference:System.Windows.Forms.dll','/reference:System.Drawing.dll','/reference:System.Web.Extensions.dll','/reference:System.IO.Compression.dll','/reference:System.IO.Compression.FileSystem.dll')
$refs+=('/win32manifest:'+(Join-Path $root 'installer/app.manifest'))
$launcher=Join-Path $package 'runtime/DoomVR.exe'
& $csc /nologo /target:winexe /platform:x64 /optimize+ /define:LAUNCHER $refs ('/out:'+$launcher) $source
if($LASTEXITCODE -ne 0){throw 'Launcher compilation failed'}
$manifestPath=Join-Path $package 'package.json'
$manifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$manifest.Files | Add-Member -NotePropertyName 'DoomVR.exe' -NotePropertyValue ((Get-FileHash -LiteralPath $launcher -Algorithm SHA256).Hash.ToLowerInvariant()) -Force
[IO.File]::WriteAllText($manifestPath,($manifest|ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
$zipPath=Join-Path $package 'payload.zip'
if(Test-Path -LiteralPath $zipPath){Remove-Item -LiteralPath $zipPath}
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory((Join-Path $package 'runtime'),$zipPath,[IO.Compression.CompressionLevel]::Optimal,$false)
$temporaryDirectory=Join-Path $dist ('.building-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporaryDirectory | Out-Null
$temporary=Join-Path $temporaryDirectory 'DOOM2016VRDLSS-Installer.exe'
& $csc /nologo /target:winexe /platform:x64 /optimize+ $refs ('/resource:'+$zipPath+',payload.zip') ('/resource:'+$manifestPath+',package.json') ('/out:'+$temporary) $source
if($LASTEXITCODE -ne 0){throw 'Installer compilation failed'}
$final=Join-Path $dist 'DOOM2016VRDLSS-Installer.exe'
Move-Item -LiteralPath $temporary -Destination $final -Force
[IO.Directory]::Delete($temporaryDirectory)
Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $dist 'package.json') -Force
$hash=(Get-FileHash -LiteralPath $final -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText((Join-Path $dist 'SHA256SUMS.txt'),($hash+'  DOOM2016VRDLSS-Installer.exe'+[Environment]::NewLine),[Text.UTF8Encoding]::new($false))
Write-Output ('Built '+$final)
Write-Output ('SHA256 '+$hash)
