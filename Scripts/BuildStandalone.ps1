param(
    [string]$EngineRoot = 'F:\Epic Games\UE_5.8',
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot),
    [string]$OutputDirectory = '',
    [switch]$SkipBuild,
    [switch]$SkipPrepare
)
$ErrorActionPreference = 'Stop'
$ProjectRoot = [IO.Path]::GetFullPath($ProjectRoot)
if (!$OutputDirectory) { $OutputDirectory = Join-Path $ProjectRoot 'dist' }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$project = Join-Path $ProjectRoot 'WowUnreal.uproject'
$build = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
$uat = Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat'
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$archive = Join-Path $ProjectRoot 'Saved\StandalonePackage'
$logs = Join-Path $ProjectRoot 'Saved\Logs'
New-Item -ItemType Directory -Force -Path $logs | Out-Null
$nativeIcon = Join-Path $ProjectRoot 'Build\Windows\Application.ico'
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $nativeIcon) | Out-Null
& (Join-Path $PSScriptRoot 'BuildDesktopIcon.ps1') -OutputPath $nativeIcon -SourcePath (Join-Path $ProjectRoot 'Content\Splash\WowUnrealProjectIcon.png')
if (!$SkipBuild) {
    & $build WowUnrealEditor Win64 Development "-Project=$project" -WaitMutex -NoUBA -MaxParallelActions=4
    if ($LASTEXITCODE -ne 0) { throw 'Editor build failed.' }
}
if (!$SkipPrepare) {
    $prepareArgs = '"{0}" -run=WowPrepareStandalone -preparestandalone -unattended -nosplash -nullrhi -abslog="{1}"' -f $project,(Join-Path $logs 'StandalonePrepare.log')
    $prepare = Start-Process -FilePath $editor -ArgumentList $prepareArgs -WindowStyle Hidden -PassThru -Wait
    if ($prepare.ExitCode -ne 0) { throw 'Standalone material preparation failed.' }
}
if (!$SkipBuild) {
    & $build WowUnreal Win64 Development "-Project=$project" -WaitMutex -NoUBA -MaxParallelActions=4
    if ($LASTEXITCODE -ne 0) { throw 'Game build failed.' }
}
& $uat BuildCookRun "-project=$project" -noP4 -platform=Win64 -clientconfig=Development -skipbuild -cook -stage -pak -compressed -package -prereqs -archive "-archivedirectory=$archive" -map=/Game/Maps/WowWorld -unattended -utf8output -nodebuginfo -NoCompileEditor -AdditionalCookerOptions=-DisablePlugins=FabLauncher
if ($LASTEXITCODE -ne 0) { throw 'Standalone packaging failed. Existing distribution was not replaced.' }
$payload = $archive
if (Test-Path -LiteralPath (Join-Path $archive 'Windows\WowUnreal')) { $payload = Join-Path $archive 'Windows' }
$game = Join-Path $payload 'WowUnreal\Binaries\Win64\WowUnreal.exe'
if (!(Test-Path -LiteralPath $game)) { throw 'Packaged game executable missing.' }
# Copy only the UAT archive, never Saved caches, extracted assets or the source project.
$client = $OutputDirectory
New-Item -ItemType Directory -Force -Path $client | Out-Null
& robocopy $payload $client /E /XD Saved /XF *.pdb Manifest_*.txt /NJH /NJS /NFL /NDL /NP
if ($LASTEXITCODE -ge 8) { throw 'Copying the packaged client failed.' }
# Tidy the client root: build bookkeeping and the obsolete nested package (dist\Windows, from
# before the payload was copied flat) are not part of the client.
Get-ChildItem -LiteralPath $client -Filter 'Manifest_*.txt' -File -ErrorAction SilentlyContinue | Remove-Item -Force
$nested = Join-Path $client 'Windows'
if ((Test-Path -LiteralPath (Join-Path $nested 'WowUnreal\Binaries')) -and (Test-Path -LiteralPath (Join-Path $client 'WowUnreal\Binaries'))) {
    Remove-Item -LiteralPath $nested -Recurse -Force
}
# The root bootstrap bypasses our startup settings. Keep only the internal game exe.
$bootstrap = [IO.Path]::GetFullPath((Join-Path $OutputDirectory 'WowUnreal.exe'))
if ($bootstrap -ne ([IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\') + '\WowUnreal.exe')) { throw 'Unexpected bootstrap path' }
if (Test-Path -LiteralPath $bootstrap) { Remove-Item -LiteralPath $bootstrap -Force }

& (Join-Path $PSScriptRoot 'BuildDesktopClient.ps1') -EngineRoot $EngineRoot -ProjectRoot $ProjectRoot -OutputDirectory $OutputDirectory
if (!(Test-Path -LiteralPath (Join-Path $OutputDirectory 'Launcher.exe'))) { throw 'Desktop launcher missing.' }
# No machine-specific development paths are necessary for this distribution.
$pathsFile = Join-Path $OutputDirectory 'ClientPaths.ini'
if (Test-Path -LiteralPath $pathsFile) {
    Copy-Item -LiteralPath $pathsFile -Destination (Join-Path $logs 'StandalonePreviousClientPaths.ini') -Force
    Set-Content -LiteralPath $pathsFile -Value '; Standalone: WowUnreal/Binaries/Win64/WowUnreal.exe is resolved beside the launcher.' -Encoding utf8
}
Write-Output "Standalone Development client ready in $OutputDirectory. User Settings were preserved."
