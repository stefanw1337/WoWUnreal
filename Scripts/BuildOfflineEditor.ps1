param([string]$Python = 'python', [string]$OutputDirectory = '')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if (!$OutputDirectory) { $OutputDirectory=Join-Path $root 'PrivateTools/OfflineCharacterEditor' }
$source=Join-Path $root 'Tools/OfflineEditor'
Push-Location $source
try {
 & $Python -m PyInstaller --noconfirm --distpath "$root/Saved/OfflineEditorBuild/dist" --workpath "$root/Saved/OfflineEditorBuild/work" WowOfflineEditor.spec
 if ($LASTEXITCODE -ne 0) { throw 'OfflineEditor packaging failed. Install PyInstaller and Pillow in the selected Python environment.' }
 & robocopy "$root/Saved/OfflineEditorBuild/dist/WowOfflineEditor" $OutputDirectory /E /NJH /NJS /NFL /NDL /NP
 if ($LASTEXITCODE -ge 8) { throw 'OfflineEditor deployment failed' }
} finally { Pop-Location }
exit 0
