param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectPath,
    [Parameter(Mandatory = $true)]
    [string]$OutputFile,
    [switch]$ForceSelection
)

$ErrorActionPreference = 'Stop'

function Resolve-CompatibleDataPath([string]$CandidatePath) {
    if ([string]::IsNullOrWhiteSpace($CandidatePath)) {
        return $null
    }

    $cleanPath = [Environment]::ExpandEnvironmentVariables($CandidatePath.Trim().Trim('"'))
    if (-not (Test-Path -LiteralPath $cleanPath -PathType Container)) {
        return $null
    }

    $fullPath = [IO.Path]::GetFullPath($cleanPath)
    $nestedDataPath = Join-Path $fullPath 'Data'
    if (Test-Path -LiteralPath (Join-Path $nestedDataPath 'common.MPQ') -PathType Leaf) {
        $fullPath = $nestedDataPath
    }

    if (-not (Test-Path -LiteralPath (Join-Path $fullPath 'common.MPQ') -PathType Leaf)) {
        return $null
    }

    return $fullPath.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
}

$settingsDirectory = Join-Path $ProjectPath 'Saved\Config'
$settingsPath = Join-Path $settingsDirectory 'WowUnrealLauncher.json'
$candidates = New-Object 'System.Collections.Generic.List[string]'
$lastConfiguredPath = ''
$portableSettings = Join-Path $ProjectPath 'dist\Settings\GameUserSettings.ini'
if (Test-Path -LiteralPath $portableSettings) {
    $section = ''
    foreach ($line in [IO.File]::ReadAllLines($portableSettings)) {
        if ($line.Trim() -match '^\[(.+)\]$') { $section = $Matches[1] }
        elseif ($section -eq 'WowUnreal.ClientPreferences' -and $line -match '^DataSource=(.*)$') {
            $candidates.Add($Matches[1].Trim().Trim('"'))
        }
    }
}

if (Test-Path -LiteralPath $settingsPath -PathType Leaf) {
    try {
        $settings = Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json
        if ($settings.DataSource) {
            $lastConfiguredPath = [string]$settings.DataSource
            $candidates.Add($lastConfiguredPath)
        }
    }
    catch {
        # A damaged user setting should reopen the selector, not block launch.
    }
}

if ($env:WOWUNREAL_DATA_SOURCE) {
    $candidates.Add($env:WOWUNREAL_DATA_SOURCE)
}
if ($env:USERPROFILE) {
    $candidates.Add((Join-Path $env:USERPROFILE 'Downloads\World of Warcraft 3.3.5a\Data'))
    $candidates.Add((Join-Path $env:USERPROFILE 'Downloads\World of Warcraft 3.3.5a - MarbelWoW\Data'))
}

$resolvedPath = $null
foreach ($candidate in $candidates) {
    $resolvedPath = Resolve-CompatibleDataPath $candidate
    if ($resolvedPath) {
        break
    }
}

function Save-ResolvedDataPath([string]$DataPath) {
    # Share a new folder selection with the executable and in-game settings.
    if (Test-Path -LiteralPath $portableSettings) {
        $lines = [Collections.Generic.List[string]]::new()
        $lines.AddRange([IO.File]::ReadAllLines($portableSettings))
        $section = ''; $index = -1; $insertAt = -1
        for ($i=0; $i -lt $lines.Count; $i++) {
            if ($lines[$i].Trim() -match '^\[(.+)\]$') {
                $section=$Matches[1]
                if ($section -eq 'WowUnreal.ClientPreferences') { $insertAt=$i+1 }
            } elseif ($section -eq 'WowUnreal.ClientPreferences' -and $lines[$i] -match '^DataSource=') { $index=$i; break }
        }
        $newLine='DataSource='+$DataPath
        if ($index -lt 0 -or $lines[$index] -ne $newLine) {
            if ($index -ge 0) { $lines[$index]=$newLine }
            elseif ($insertAt -ge 0) { $lines.Insert($insertAt,$newLine) }
            else { $lines.Add(''); $lines.Add('[WowUnreal.ClientPreferences]'); $lines.Add($newLine) }
            $tempPath=$portableSettings+'.'+[Guid]::NewGuid().ToString('N')+'.tmp'
            [IO.File]::WriteAllLines($tempPath,$lines,[Text.UTF8Encoding]::new($false))
            [IO.File]::Replace($tempPath,$portableSettings,$portableSettings+'.launcher.bak')
        }
    }
    New-Item -ItemType Directory -Path $settingsDirectory -Force | Out-Null
    [PSCustomObject]@{
        DataSource = $DataPath
        UpdatedUtc = [DateTime]::UtcNow.ToString('o')
    } | ConvertTo-Json | Set-Content -LiteralPath $settingsPath -Encoding UTF8
    # cmd.exe reads this one-line handoff file through SET /P. ASCII avoids a
    # UTF-8 BOM becoming part of the path in Windows PowerShell 5.1.
    Set-Content -LiteralPath $OutputFile -Value $DataPath -Encoding ASCII -NoNewline
}

if ($resolvedPath -and -not $ForceSelection) {
    Save-ResolvedDataPath $resolvedPath
    exit 0
}

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()

$form = New-Object System.Windows.Forms.Form
$form.Text = 'WowUnreal - External Data'
$form.StartPosition = 'CenterScreen'
$form.FormBorderStyle = 'FixedDialog'
$form.MaximizeBox = $false
$form.MinimizeBox = $false
$form.ClientSize = New-Object System.Drawing.Size(650, 245)
$form.BackColor = [System.Drawing.Color]::FromArgb(18, 18, 18)
$form.ForeColor = [System.Drawing.Color]::Gainsboro
$form.Font = New-Object System.Drawing.Font('Segoe UI', 10)

$title = New-Object System.Windows.Forms.Label
$title.Text = 'External game data is required'
$title.Font = New-Object System.Drawing.Font('Segoe UI Semibold', 16)
$title.ForeColor = [System.Drawing.Color]::FromArgb(255, 220, 70)
$title.Location = New-Object System.Drawing.Point(24, 20)
$title.AutoSize = $true
$form.Controls.Add($title)

$description = New-Object System.Windows.Forms.Label
$description.Text = 'Select the installation folder or its Data subfolder. The files remain on this computer.'
$description.Location = New-Object System.Drawing.Point(27, 58)
$description.Size = New-Object System.Drawing.Size(595, 42)
$description.ForeColor = [System.Drawing.Color]::Silver
$form.Controls.Add($description)

$pathBox = New-Object System.Windows.Forms.TextBox
$pathBox.Location = New-Object System.Drawing.Point(29, 104)
$pathBox.Size = New-Object System.Drawing.Size(482, 28)
$pathBox.BackColor = [System.Drawing.Color]::FromArgb(35, 35, 35)
$pathBox.ForeColor = [System.Drawing.Color]::WhiteSmoke
$pathBox.BorderStyle = 'FixedSingle'
$pathBox.Text = if ($resolvedPath) { $resolvedPath } else { $lastConfiguredPath }
$form.Controls.Add($pathBox)

$browseButton = New-Object System.Windows.Forms.Button
$browseButton.Text = 'Browse...'
$browseButton.Location = New-Object System.Drawing.Point(521, 101)
$browseButton.Size = New-Object System.Drawing.Size(100, 32)
$browseButton.FlatStyle = 'Flat'
$browseButton.BackColor = [System.Drawing.Color]::FromArgb(55, 55, 55)
$browseButton.ForeColor = [System.Drawing.Color]::WhiteSmoke
$form.Controls.Add($browseButton)

$statusLabel = New-Object System.Windows.Forms.Label
$statusLabel.Location = New-Object System.Drawing.Point(29, 143)
$statusLabel.Size = New-Object System.Drawing.Size(592, 26)
$form.Controls.Add($statusLabel)

$continueButton = New-Object System.Windows.Forms.Button
$continueButton.Text = 'Continue'
$continueButton.Location = New-Object System.Drawing.Point(401, 188)
$continueButton.Size = New-Object System.Drawing.Size(105, 34)
$continueButton.FlatStyle = 'Flat'
$continueButton.BackColor = [System.Drawing.Color]::FromArgb(90, 20, 20)
$continueButton.ForeColor = [System.Drawing.Color]::FromArgb(255, 220, 70)
$form.Controls.Add($continueButton)

$cancelButton = New-Object System.Windows.Forms.Button
$cancelButton.Text = 'Cancel'
$cancelButton.Location = New-Object System.Drawing.Point(516, 188)
$cancelButton.Size = New-Object System.Drawing.Size(105, 34)
$cancelButton.FlatStyle = 'Flat'
$cancelButton.BackColor = [System.Drawing.Color]::FromArgb(45, 45, 45)
$cancelButton.ForeColor = [System.Drawing.Color]::WhiteSmoke
$cancelButton.DialogResult = [System.Windows.Forms.DialogResult]::Cancel
$form.Controls.Add($cancelButton)
$form.CancelButton = $cancelButton
$form.AcceptButton = $continueButton

$updateValidation = {
    $validPath = Resolve-CompatibleDataPath $pathBox.Text
    $continueButton.Tag = $validPath
    $continueButton.Enabled = [bool]$validPath
    if ($validPath) {
        $statusLabel.Text = 'Compatible data source found.'
        $statusLabel.ForeColor = [System.Drawing.Color]::FromArgb(105, 220, 120)
    }
    elseif ([string]::IsNullOrWhiteSpace($pathBox.Text)) {
        $statusLabel.Text = 'Choose a folder to continue.'
        $statusLabel.ForeColor = [System.Drawing.Color]::Silver
    }
    else {
        $statusLabel.Text = 'No compatible archive set was found in this folder.'
        $statusLabel.ForeColor = [System.Drawing.Color]::FromArgb(255, 125, 105)
    }
}

$pathBox.Add_TextChanged($updateValidation)
$browseButton.Add_Click({
    $folderBrowser = New-Object System.Windows.Forms.FolderBrowserDialog
    $folderBrowser.Description = 'Select the installation folder or its Data subfolder.'
    $folderBrowser.ShowNewFolderButton = $false
    if (Test-Path -LiteralPath $pathBox.Text -PathType Container) {
        $folderBrowser.SelectedPath = $pathBox.Text
    }
    if ($folderBrowser.ShowDialog($form) -eq [System.Windows.Forms.DialogResult]::OK) {
        $pathBox.Text = $folderBrowser.SelectedPath
    }
    $folderBrowser.Dispose()
})

$continueButton.Add_Click({
    if ($continueButton.Tag) {
        $script:selectedPath = [string]$continueButton.Tag
        $form.DialogResult = [System.Windows.Forms.DialogResult]::OK
        $form.Close()
    }
})

& $updateValidation
$dialogResult = $form.ShowDialog()
$form.Dispose()

if ($dialogResult -ne [System.Windows.Forms.DialogResult]::OK -or
    [string]::IsNullOrWhiteSpace($script:selectedPath)) {
    if (Test-Path -LiteralPath $OutputFile) {
        Remove-Item -LiteralPath $OutputFile -Force
    }
    exit 2
}

Save-ResolvedDataPath $script:selectedPath
exit 0
