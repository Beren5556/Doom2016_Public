param([Parameter(Mandatory=$true)][string]$GameDirectory,
    [string]$ExpectedEngineSha256='8a72c744b83dea42df0482e951b99a2f38c74c14c833b0ff346d3942dbcf992a')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$tests=Join-Path $root 'build/installer-tests'
New-Item -ItemType Directory -Path $tests -Force | Out-Null
$csc=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
$installer=Join-Path $root 'dist/DOOM2016VRDLSS-Installer.exe'
$runner=Join-Path (Split-Path -Parent $installer) 'InstallerTests.exe'
& $csc /nologo /target:exe /platform:x64 /optimize+ ('/win32manifest:'+(Join-Path $root 'installer/app.manifest')) ('/reference:'+$installer) /reference:System.Windows.Forms.dll /reference:System.Drawing.dll /reference:System.Web.Extensions.dll /reference:System.IO.Compression.dll ('/out:'+$runner) (Join-Path $root 'installer/InstallerTests.cs')
if($LASTEXITCODE -ne 0){throw 'Test compilation failed'}
$case=Join-Path $tests ([Guid]::NewGuid().ToString('N'))
& $runner $GameDirectory $case $ExpectedEngineSha256
if($LASTEXITCODE -ne 0){throw 'Installer tests failed'}
& $runner --preview (Join-Path $tests 'installer.png')
if($LASTEXITCODE -ne 0){throw 'Installer preview failed'}
# WinForms startup only. Never invoke the bundled launch action in this check.
$probe=Start-Process -FilePath $installer -WindowStyle Hidden -PassThru
try {
    Start-Sleep -Seconds 2
    if($probe.HasExited){throw 'Installer GUI exited unexpectedly'}
    Write-Output 'PASS GUI process remains running.'
}finally{if(!$probe.HasExited){$probe.Kill();$probe.WaitForExit()};$probe.Dispose()}
