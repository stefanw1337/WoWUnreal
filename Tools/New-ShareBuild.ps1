param(
    [string]$ProjectRoot = 'E:\WowUnreal',
    [string]$UnrealPak = 'F:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealPak.exe',
    [switch]$CheckOnly
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# Build from runtime files only. Never copy the personal distribution wholesale.
$root = (Resolve-Path -LiteralPath $ProjectRoot).Path.TrimEnd('\')
$source = Join-Path $root 'dist'
$target = Join-Path $root 'dist-share'
$build = Join-Path $root ('dist-share.build-' + [Guid]::NewGuid().ToString('N'))
$audit = Join-Path $env:TEMP ('WowShareAudit-' + [Guid]::NewGuid().ToString('N'))
$marker = 'SHARE-BUILD.json'
$packageRoot = Join-Path $root 'Saved\StandalonePackage\Windows'
$packageBinaries = Join-Path $packageRoot 'WowUnreal\Binaries'
$utf8 = New-Object System.Text.UTF8Encoding($false)

function Assert-ChildPath([string]$Path, [string]$Parent) {
    $full = [IO.Path]::GetFullPath($Path)
    if (!$full.StartsWith([IO.Path]::GetFullPath($Parent).TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Output path is outside its intended parent.'
    }
    return $full
}
function Assert-NoLinks([string]$Path) {
    $item = Get-Item -LiteralPath $Path -Force
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Linked path refused: $Path" }
    if ($item.PSIsContainer) {
        foreach ($child in Get-ChildItem -LiteralPath $Path -Force) { Assert-NoLinks $child.FullName }
    }
}
function Assert-SafeText([string]$Path) {
    if ([IO.Path]::GetExtension($Path).ToLowerInvariant() -notin @('.ini','.json','.xml','.cfg','.txt','.modules','.uplugin')) { return }
    # Do not print values: failure reports must not themselves disclose a secret.
    $text = [IO.File]::ReadAllText($Path)
    if ($text -match '(?im)^\s*(?:[+\-]?)(?:ProtectedPassword|Password|Username|AccountAlias|WeatherLatitude|WeatherLongitude|DataSource|LastCharacter|LastPosition|SpawnPosition)\s*[:=]\s*[^\s";#]' -or
        $text -match '(?i)"(?:ProtectedPassword|Password|Username|WeatherLatitude|WeatherLongitude)"\s*:' -or
        $text -match '(?i)[A-Z]:[\\/]+Users[\\/]' -or
        $text -match '(?i)-(?:autologin|account=|password=|spawnue=)') {
        throw ('Personal data or launch override detected in ' + [IO.Path]::GetFileName($Path))
    }
}
function Is-RuntimeFile([IO.FileInfo]$File, [string]$Relative) {
    if ($Relative -match '(?i)(^|[\\/])(Saved|Settings|Logs|Screenshots?|Crashes|Cache|DerivedDataCache|Intermediate|Interface|UserSounds|Sky)([\\/]|$)') { return $false }
    if ($File.Name -match '(?i)credentials|clientpaths|gameusersettings|lastposition|world-entry|autologin') { return $false }
    if ($File.Extension.ToLowerInvariant() -in @('.log','.bak','.tmp','.pdb','.dmp','.lnk','.sav','.mpq','.bat','.cmd','.ps1')) { return $false }
    return $true
}

$required = @('Launcher.exe','WowUnrealSettings.exe',
    'WowUnreal\Binaries\Win64\WowUnreal.exe',
    'WowUnreal\Content\Paks\WowUnreal-Windows.pak',
    'WowUnreal\Content\Paks\WowUnreal-Windows.utoc',
    'WowUnreal\Content\Paks\WowUnreal-Windows.ucas','Launcher\Splash.png','Launcher\WowUnreal.ico')
foreach ($rel in $required) {
    if (!(Test-Path -LiteralPath (Join-Path $source $rel) -PathType Leaf)) { throw "Missing runtime file: $rel" }
}
if (!(Test-Path -LiteralPath $UnrealPak -PathType Leaf)) { throw 'UnrealPak is required to inspect packaged configuration.' }
Assert-NoLinks $source
$allowedTrees = @('Engine','Launcher','WowUnreal\Binaries',
    'WowUnreal\Content','WowUnreal\Plugins')
$files = New-Object 'System.Collections.Generic.List[object]'
foreach ($rel in @('Launcher.exe','WowUnrealSettings.exe')) {
    $files.Add([pscustomobject]@{File=(Get-Item -LiteralPath (Join-Path $source $rel)); Relative=$rel})
}
foreach ($tree in $allowedTrees) {
    $dir = Join-Path $source $tree
    if (!(Test-Path -LiteralPath $dir)) { continue }
    foreach ($file in Get-ChildItem -LiteralPath $dir -Recurse -File -Force) {
        $rel = $file.FullName.Substring($source.Length + 1)
        # Game binaries: only what our own package produced. Injected tools added to the
        # personal copy (OptiScaler dxgi.dll, neural-rendering runtimes, ReShade ...) stay out.
        if ($rel -like 'WowUnreal\Binaries\*' -and (Test-Path -LiteralPath $packageBinaries)) {
            if (!(Test-Path -LiteralPath (Join-Path $packageRoot $rel))) { continue }
        }
        if (Is-RuntimeFile $file $rel) {
            Assert-SafeText $file.FullName
            $files.Add([pscustomobject]@{File=$file; Relative=$rel})
        }
    }
}
# Inspect files hidden inside every pak, not only loose INI files.
try {
    [void][IO.Directory]::CreateDirectory($audit)
    $pakIndex = 0
    foreach ($entry in $files | Where-Object { $_.File.Extension -eq '.pak' }) {
        $listing = & $UnrealPak $entry.File.FullName -List 2>&1
        if ($LASTEXITCODE -ne 0) { throw 'Pak inventory failed.' }
        $paths = @($listing | ForEach-Object {
            if ("$_" -match 'Display: "([^"]+)" offset:') { $Matches[1] }
        })
        if ($paths.Count -eq 0) { throw 'Could not validate pak inventory.' }
        foreach ($path in $paths) {
            if ($path -match '(?i)(^|/)(Saved|Settings|Logs|Screenshots?|Crashes|Interface|UserSounds|Sky)/|credentials|clientpaths|\.mpq$|\.sav$|\.lnk$|(^|/)GameUserSettings\.ini$') {
                throw 'A pak contains a personal profile or non-shareable runtime path. Repackage cleanly first.'
            }
        }
        $unpack = Join-Path $audit ([string]$pakIndex++)
        $null = & $UnrealPak $entry.File.FullName -Extract $unpack '-Filter=*Config/*' 2>&1
        if ($LASTEXITCODE -ne 0) { throw 'Packaged configuration inspection failed.' }
        if (Test-Path -LiteralPath $unpack) {
            foreach ($file in Get-ChildItem -LiteralPath $unpack -Recurse -File) { Assert-SafeText $file.FullName }
        }
    }
    if ($CheckOnly) { Write-Host "PASS: $($files.Count) runtime files passed sharing checks. No export changed."; return }

    [void][IO.Directory]::CreateDirectory($build)
    $manifest = New-Object 'System.Collections.Generic.List[object]'
    foreach ($entry in $files) {
        $destination = Assert-ChildPath (Join-Path $build $entry.Relative) $build
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination))
        $before = (Get-FileHash -LiteralPath $entry.File.FullName -Algorithm SHA256).Hash
        Copy-Item -LiteralPath $entry.File.FullName -Destination $destination
        $after = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
        if ($before -ne $after) { throw 'A runtime file changed while packaging; retry after the build has finished.' }
        $manifest.Add([pscustomobject]@{Path=$entry.Relative; SHA256=$after})
    }
    [void][IO.Directory]::CreateDirectory((Join-Path $build 'User\Settings'))
    [IO.File]::WriteAllText((Join-Path $build 'User\Settings\GameUserSettings.ini'), @'
; Clean sharing profile. Select your own Data folder in WowUnrealSettings.exe.
[WowUnreal.ClientPreferences]
DataSource=
WeatherSource=0
WeatherLocationConfigured=False
'@, $utf8)
    [IO.File]::WriteAllText((Join-Path $build 'START-HERE.txt'), @'
1. Open WowUnrealSettings.exe and select your own World of Warcraft 3.3.5a Data folder. Save and close.
2. Open Launcher.exe and enter your own login details.

This export contains no saved account, personal settings, weather coordinates, logs or screenshots.
Original WoW data is not included. The User folder (settings, addons, screenshots) belongs to this copy only.
To share an updated build, create a fresh export; do not share a copy you have played in.
'@, $utf8)
    [IO.File]::WriteAllText((Join-Path $build 'ClientPaths.ini'), '; Portable root marker. Start Launcher.exe.', $utf8)
    $manifest.Add([pscustomobject]@{Path='ClientPaths.ini'; SHA256=(Get-FileHash -LiteralPath (Join-Path $build 'ClientPaths.ini') -Algorithm SHA256).Hash})
    $report = [ordered]@{Format=1; CreatedUtc=[DateTime]::UtcNow.ToString('o'); CleanProfile=$true; Files=$manifest}
    [IO.File]::WriteAllText((Join-Path $build $marker), ($report | ConvertTo-Json -Depth 5), $utf8)
    # Publish only a fully copied, hash-checked export. Preserve the previous one.
    if (Test-Path -LiteralPath $target) {
        Assert-NoLinks $target
        if (!(Test-Path -LiteralPath (Join-Path $target $marker))) { throw 'Existing dist-share is not a managed export; it has been left untouched.' }
        $old = Join-Path $root ('dist-share.previous-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
        $null = Assert-ChildPath $target $root
        $null = Assert-ChildPath $old $root
        Move-Item -LiteralPath $target -Destination $old
    }
    $null = Assert-ChildPath $build $root
    $null = Assert-ChildPath $target $root
    Move-Item -LiteralPath $build -Destination $target
    & (Join-Path $PSScriptRoot 'Prune-ShareBuilds.ps1') -ProjectRoot $root -Apply
    Write-Host "PASS: clean share build ready at $target ($($files.Count) runtime files)."
}
finally {
    if (Test-Path -LiteralPath $audit) {
        $null = Assert-ChildPath $audit $env:TEMP
        Remove-Item -LiteralPath $audit -Recurse -Force
    }
}
