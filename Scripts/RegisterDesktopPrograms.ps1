param([string]$Solution = (Join-Path (Split-Path -Parent $PSScriptRoot) "WowUnreal.sln"))
$ErrorActionPreference='Stop'
$s=[IO.File]::ReadAllText($Solution)
$folder=[regex]::Match($s, 'Project\([^
]+ = "Programs", "Programs", "(\{[^}]+\})"').Groups[1].Value
if (!$folder) { throw 'Programs solution folder missing' }
$configs=[regex]::Match($s,'(?s)GlobalSection\(SolutionConfigurationPlatforms\).*??
(.*?)EndGlobalSection').Groups[1].Value
if (!$s.Contains('{AE65B6A8-BA1F-5C8C-A2A1-2EBFE4D50D19}')) {
$s=$s.Replace("`nGlobal","`n"+@'
Project("{BC8A1FFA-BEE3-4634-8014-F334798102B3}") = "Launcher", "Tools\DesktopClient\Launcher.vcxproj", "{AE65B6A8-BA1F-5C8C-A2A1-2EBFE4D50D19}"
EndProject
'@+"`nGlobal")
$entries=''
foreach ($line in ($configs -split "`n")) { if ($line -match '^\s*(.*?)\s*=') {
$entries+="`t`t{AE65B6A8-BA1F-5C8C-A2A1-2EBFE4D50D19}."+$Matches[1]+".ActiveCfg = Development|x64`r`n"
} }
$s=$s.Replace('GlobalSection(ProjectConfigurationPlatforms) = postSolution',"GlobalSection(ProjectConfigurationPlatforms) = postSolution`r`n"+$entries.TrimEnd())
$s=$s.Replace('GlobalSection(NestedProjects) = preSolution',"GlobalSection(NestedProjects) = preSolution`r`n`t`t{AE65B6A8-BA1F-5C8C-A2A1-2EBFE4D50D19} = $folder")
}
if (!$s.Contains('{2CF0B272-8FBB-530A-BBA5-2F600B1E0C85}')) {
$s=$s.Replace("`nGlobal","`n"+@'
Project("{BC8A1FFA-BEE3-4634-8014-F334798102B3}") = "WoWSettings", "Tools\DesktopClient\WoWSettings.vcxproj", "{2CF0B272-8FBB-530A-BBA5-2F600B1E0C85}"
EndProject
'@+"`nGlobal")
$entries=''
foreach ($line in ($configs -split "`n")) { if ($line -match '^\s*(.*?)\s*=') {
$entries+="`t`t{2CF0B272-8FBB-530A-BBA5-2F600B1E0C85}."+$Matches[1]+".ActiveCfg = Development|x64`r`n"
} }
$s=$s.Replace('GlobalSection(ProjectConfigurationPlatforms) = postSolution',"GlobalSection(ProjectConfigurationPlatforms) = postSolution`r`n"+$entries.TrimEnd())
$s=$s.Replace('GlobalSection(NestedProjects) = preSolution',"GlobalSection(NestedProjects) = preSolution`r`n`t`t{2CF0B272-8FBB-530A-BBA5-2F600B1E0C85} = $folder")
}
if (!$s.Contains('{38760961-1A63-5A30-BDDA-A505C50E2BB8}')) {
$s=$s.Replace("`nGlobal","`n"+@'
Project("{BC8A1FFA-BEE3-4634-8014-F334798102B3}") = "OfflineEditor", "Tools\OfflineEditor\OfflineEditor.vcxproj", "{38760961-1A63-5A30-BDDA-A505C50E2BB8}"
EndProject
'@+"`nGlobal")
$entries=''
foreach ($line in ($configs -split "`n")) { if ($line -match '^\s*(.*?)\s*=') {
$entries+="`t`t{38760961-1A63-5A30-BDDA-A505C50E2BB8}."+$Matches[1]+".ActiveCfg = Development|x64`r`n"
} }
$s=$s.Replace('GlobalSection(ProjectConfigurationPlatforms) = postSolution',"GlobalSection(ProjectConfigurationPlatforms) = postSolution`r`n"+$entries.TrimEnd())
$s=$s.Replace('GlobalSection(NestedProjects) = preSolution',"GlobalSection(NestedProjects) = preSolution`r`n`t`t{38760961-1A63-5A30-BDDA-A505C50E2BB8} = $folder")
}
[IO.File]::WriteAllText($Solution,$s,[Text.UTF8Encoding]::new($false))
Write-Output 'Launcher, WoWSettings and OfflineEditor registered under Programs.'
