#requires -Version 7.0
[CmdletBinding()]
param([Parameter(Mandatory)][ValidateSet('Prepare','Publish')][string]$Phase,
      [Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root=Split-Path -Parent $PSScriptRoot
$work=Join-Path $root "out/release-$Version"
$evidence=Join-Path $root "evidence/release-validation/$Version"
$index=Get-Content -Raw -LiteralPath (Join-Path $root "evidence/packages-$Version.json") | ConvertFrom-Json
$commit=(& git -C $root rev-parse "v$Version^{}").Trim()
if($LASTEXITCODE -ne 0 -or $commit -notmatch '^[a-f0-9]{40}$'){throw 'Missing source tag'}
$null=New-Item -ItemType Directory -Path $work,$evidence -Force
$download=Join-Path $work 'downloaded'
$null=New-Item -ItemType Directory -Path $download -Force
$lines="protocol=https`nhost=github.com`n`n" | git credential fill
if($LASTEXITCODE -ne 0){throw 'Git credential helper failed'}
$credentials=@{}
foreach($line in $lines){$pair=$line -split '=',2;if($pair.Count -eq 2){$credentials[$pair[0]]=$pair[1]}}
try {
    $headers=@{Authorization="Bearer $($credentials.password)";Accept='application/vnd.github+json';'User-Agent'='FoveatedDLSS-private-release';'X-GitHub-Api-Version'='2022-11-28'}
    $api='https://api.github.com/repos/Beren5556/FOVEATED_DLSS'
    $user=Invoke-RestMethod https://api.github.com/user -Headers $headers
    $repo=Invoke-RestMethod $api -Headers $headers
    if($user.login -ne 'Beren5556' -or $repo.owner.login -ne 'Beren5556' -or -not $repo.private){throw 'Authenticated owner/private mismatch'}
    $releases=Invoke-RestMethod "$api/releases?per_page=100" -Headers $headers
    $matches=@($releases | Where-Object tag_name -eq "v$Version")
    if($matches.Count -gt 1){throw 'Duplicate release'}
    if($Phase -eq 'Prepare') {
        if($matches.Count) {
            $release=$matches[0]
            if(-not $release.draft -or $release.target_commitish -ne $commit){throw 'Existing release is not this draft'}
        } else {
            $body=Get-Content -Raw -LiteralPath (Join-Path $root "docs/RELEASE_NOTES_$Version.md")
            $payload=@{tag_name="v$Version";target_commitish=$commit;name="FoveatedDLSS $Version";body=$body;draft=$true;prerelease=$false} | ConvertTo-Json
            $release=Invoke-RestMethod "$api/releases" -Method Post -Headers $headers -ContentType 'application/json' -Body $payload
        }
        @{id=$release.id;tag="v$Version";commit=$commit;url=$release.html_url} | ConvertTo-Json |
            Set-Content -LiteralPath (Join-Path $work 'draft.json') -Encoding utf8NoBOM
        $paths=@($index.packages | ForEach-Object {Join-Path $root $_.artifact.path})+@(Join-Path $work 'SHA256SUMS.txt')
        $verified=@()
        foreach($path in $paths) {
            $name=[IO.Path]::GetFileName($path);$hash=(Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant();$bytes=(Get-Item -LiteralPath $path).Length
            $assets=Invoke-RestMethod "$api/releases/$($release.id)/assets" -Headers $headers
            $found=@($assets | Where-Object name -eq $name)
            if($found.Count){$asset=$found[0]}else{
                $url=($release.upload_url -split '\{')[0]+'?name='+[Uri]::EscapeDataString($name)
                $asset=Invoke-RestMethod $url -Method Post -Headers $headers -InFile $path -ContentType 'application/octet-stream'
            }
            if($asset.size -ne $bytes -or $asset.digest -ne "sha256:$hash"){throw "Remote asset differs: $name"}
            $binaryHeaders=$headers.Clone();$binaryHeaders.Accept='application/octet-stream'
            $destination=Join-Path $download $name
            Invoke-WebRequest $asset.url -Headers $binaryHeaders -OutFile $destination
            if((Get-FileHash -LiteralPath $destination).Hash -ne $hash -or (Get-Item -LiteralPath $destination).Length -ne $bytes){throw "Downloaded asset differs: $name"}
            $verified+=@{id=$asset.id;name=$name;sha256=$hash;bytes=$bytes;url=$asset.browser_download_url}
            Write-Host "PASS remote upload/download $name"
        }
        @{status='DRAFT_VERIFIED_BYTES';private=$true;source_commit=$commit;release_id=$release.id;assets=$verified} |
            ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $evidence 'draft-assets.json') -Encoding utf8NoBOM
    } else {
        if($matches.Count -ne 1){throw 'Release missing'}
        $release=$matches[0]
        $receipt=Get-Content -Raw -LiteralPath (Join-Path $evidence 'draft-assets.json') | ConvertFrom-Json
        $validation=Get-Content -Raw -LiteralPath (Join-Path $evidence 'download-verification.json') | ConvertFrom-Json
        $source=Get-Content -Raw -LiteralPath (Join-Path $evidence 'source-verification.json') | ConvertFrom-Json
        if($receipt.source_commit -ne $commit -or $validation.status -ne 'PASS' -or $source.status -ne 'PASS' -or $source.source_commit -ne $commit){throw 'Exact artifact/source verification missing'}
        if($release.id -ne $receipt.release_id -or $release.target_commitish -ne $commit){throw 'Release changed'}
        $assets=Invoke-RestMethod "$api/releases/$($release.id)/assets" -Headers $headers
        foreach($entry in $receipt.assets) {
            $found=@($assets | Where-Object id -eq $entry.id)
            if($found.Count -ne 1 -or $found[0].name -ne $entry.name -or $found[0].size -ne $entry.bytes -or $found[0].digest -ne "sha256:$($entry.sha256)"){throw 'Verified remote asset changed'}
        }
        if($release.draft){
            $release=Invoke-RestMethod "$api/releases/$($release.id)" -Method Patch -Headers $headers -ContentType 'application/json' -Body '{"draft":false,"prerelease":false,"make_latest":"true"}'
        }
        $release=Invoke-RestMethod "$api/releases/tags/v$Version" -Headers $headers
        $repo=Invoke-RestMethod $api -Headers $headers
        if(-not $repo.private -or $release.draft -or $release.prerelease){throw 'Published identity/visibility mismatch'}
        foreach($entry in $receipt.assets) {
            $found=@($release.assets | Where-Object id -eq $entry.id)
            if($found.Count -ne 1 -or $found[0].digest -ne "sha256:$($entry.sha256)" -or $found[0].size -ne $entry.bytes){throw 'Published asset changed'}
        }
        @{status='PASS';private=$true;source_commit=$commit;version=$Version;release=$release.html_url;release_id=$release.id;assets=$receipt.assets;
          downloaded_packages_and_consumers_verified=$true;source_inventory_verified=$true;game_or_vr_started=$false;real_inference=$false;previous_releases_preserved=$true} |
          ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $evidence 'receipt.json') -Encoding utf8NoBOM
        Write-Host "PASS published PRIVATE $($release.html_url)"
    }
} finally {$headers=$null;$binaryHeaders=$null;$credentials.Clear();$lines=$null}
