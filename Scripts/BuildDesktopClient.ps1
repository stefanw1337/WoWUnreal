param(
    [string]$EngineRoot = 'F:\Epic Games\UE_5.8',
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot),
    [string]$OutputDirectory = '',
    [ValidateSet('All','Launcher','Settings')][string]$Program = 'All'
)
$ErrorActionPreference = 'Stop'
$source = Join-Path $ProjectRoot 'Tools\DesktopClient\WowUnrealDesktop.cs'
$output = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else { Join-Path $ProjectRoot 'dist' }
$compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
New-Item -ItemType Directory -Force -Path $output | Out-Null
# User overrides live beside the public launcher, outside the packaged payload.
# Create missing directories only (under User\, the current layout); never replace user files.
foreach ($relative in @('Interface\AddOns','Interface\Icons','Sound','Screenshot','Sky')) {
    New-Item -ItemType Directory -Force -Path (Join-Path (Join-Path $output 'User') $relative) | Out-Null
}
$startupSource = Join-Path $ProjectRoot 'Tools\DesktopClient\StartupPresentation.cs'
$assets = Join-Path $output 'Launcher'
New-Item -ItemType Directory -Force -Path $assets | Out-Null
$icon = Join-Path $assets 'WowUnreal.ico'
& (Join-Path $PSScriptRoot 'BuildDesktopIcon.ps1') -OutputPath $icon
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'Content\Splash\WowUnrealSplashNoLogo.png') -Destination (Join-Path $assets 'Splash.png') -Force
foreach ($settingsApp in @($false,$true)) {
    if (($Program -eq 'Launcher' -and $settingsApp) -or ($Program -eq 'Settings' -and !$settingsApp)) { continue }
    $name = if ($settingsApp) { 'WowUnrealSettings.exe' } else { 'Launcher.exe' }
    $compileArgs = @('/nologo','/target:winexe',"/win32icon:$icon","/out:$(Join-Path $output $name)",'/reference:System.Windows.Forms.dll','/reference:System.Drawing.dll','/reference:System.Core.dll','/reference:System.Device.dll','/reference:System.Web.Extensions.dll')
    if ($settingsApp) { $compileArgs += '/define:SETTINGS_APP' }
    & $compiler @compileArgs $source $startupSource
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $name" }
}
$dataPath = ''
$legacyData = Join-Path $ProjectRoot 'Saved\Config\WowUnrealLauncher.json'
if (Test-Path -LiteralPath $legacyData) { $dataPath = (Get-Content -LiteralPath $legacyData -Raw | ConvertFrom-Json).DataSource }
if ([string]::IsNullOrWhiteSpace($dataPath)) { $dataPath = ' ' }
$project = Join-Path $ProjectRoot 'WowUnreal.uproject'
$engine = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$client = Join-Path $output $(if ($Program -eq 'Settings') { 'WowUnrealSettings.exe' } else { 'Launcher.exe' })
$initArgs = '--initialize "{0}" "{1}" "{2}"' -f $project,$engine,$dataPath
$init = Start-Process -FilePath $client -ArgumentList $initArgs -WindowStyle Hidden -PassThru -Wait
if ($init.ExitCode -ne 0) { throw 'Settings initialization failed. See dist/initialize-error.log.' }
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'Tools\DesktopClient\README.md') -Destination (Join-Path $output 'README.md')
Write-Output "Desktop executables built in $output. Existing dist settings preserved. No client launched."
