param([string]$ProjectRoot='E:\WowUnreal',[switch]$Apply)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root=(Resolve-Path -LiteralPath $ProjectRoot).Path.TrimEnd('\')
function Assert-Contained([string]$Path,[string]$Parent) {
    $full=[IO.Path]::GetFullPath($Path)
    if (!$full.StartsWith($Parent.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) {throw 'Share path escaped its parent.'}
    return $full
}
function Assert-NoLinks([string]$Path) {
    $item=Get-Item -LiteralPath $Path -Force
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {throw 'Linked share directory refused.'}
    if ($item.PSIsContainer) {foreach ($child in Get-ChildItem -LiteralPath $Path -Force) {Assert-NoLinks $child.FullName}}
}
$versions=@(Get-ChildItem -LiteralPath $root -Directory -Force | Where-Object {$_.Name -cmatch '^dist-share\.previous-\d{8}-\d{6}-\d{3}$'} | Sort-Object Name -Descending)
$removed=0;$bytes=0L
# Keep the newest rollback export, as well as the current dist-share.
foreach ($version in @($versions | Select-Object -Skip 1)) {
    try {
        $path=Assert-Contained $version.FullName $root
        Assert-NoLinks $path
        $marker=Join-Path $path 'SHARE-BUILD.json'
        if (!(Test-Path -LiteralPath $marker -PathType Leaf)) {throw 'Unmanaged export.'}
        $manifest=Get-Content -LiteralPath $marker -Raw | ConvertFrom-Json
        if ($manifest.Format -ne 1 -or $manifest.CleanProfile -ne $true -or @($manifest.Files).Count -eq 0) {throw 'Invalid export marker.'}
        $allowed=@{'SHARE-BUILD.json'=$true;'START-HERE.txt'=$true;'Settings\GameUserSettings.ini'=$true}
        foreach ($entry in $manifest.Files) {
            $null=Assert-Contained (Join-Path $path $entry.Path) $path
            $allowed[$entry.Path]=$true
        }
        $files=@(Get-ChildItem -LiteralPath $path -File -Recurse -Force)
        foreach ($file in $files) {
            $relative=$file.FullName.Substring($path.Length+1)
            if (!$allowed.ContainsKey($relative)) {throw 'Additional files found; preserve this copy.'}
        }
        $settings=Join-Path $path 'Settings\GameUserSettings.ini'
        if (Test-Path -LiteralPath $settings) {
            $content=Get-Content -LiteralPath $settings -Raw
            $expected="; Clean sharing profile. Select your own Data folder in WowUnrealSettings.exe.`n[WowUnreal.ClientPreferences]`nDataSource=`nWeatherSource=0`nWeatherLocationConfigured=False"
            if ($content.Replace("`r",'').Trim() -cne $expected) {throw 'Modified profile found; preserve this copy.'}
        }
        $size=($files | Measure-Object Length -Sum).Sum
        if ($Apply) {
            # Revalidate the exact absolute target immediately before deletion.
            $null=Assert-Contained (Resolve-Path -LiteralPath $path).Path $root
            Assert-NoLinks $path
            Remove-Item -LiteralPath $path -Recurse -Force
        }
        $removed++;$bytes+=$size
        Write-Output ((@{ $true='REMOVED';$false='WOULD REMOVE'}[$Apply.IsPresent])+' '+$version.Name)
    } catch {Write-Warning ('Preserved '+$version.Name+': '+$_.Exception.Message)}
}
Write-Output ("Share retention: {0} old exports, {1:N2} GiB; current and newest previous export retained. Apply={2}" -f $removed,($bytes/1GB),$Apply.IsPresent)
