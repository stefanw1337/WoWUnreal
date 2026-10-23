# WowUnreal standalone Windows client

Keep the entire folder together. Unreal Editor and the source project are not required.
This is a Development game build with logging, not an Editor launcher.
Use your own local WoW 3.3.5a Data folder; no Blizzard game data is bundled.

- `Launcher.exe`: online login.
- `Launcher.exe -offline`: offline character selection and world testing.
- `WowUnrealSettings.exe` (or `Launcher.exe -settings`): settings without starting the game.
- `Engine/` and `WowUnreal/`: standard UE packaged runtime folders. Keep beside Launcher.exe.
- `WowUnreal/Binaries/Win64/WowUnreal.exe`: internal game binary, started by the public launcher. Do not move it or start it directly.
- `Launcher/`: splash artwork and the approved W/U application icon.

The redundant root UE bootstrap WowUnreal.exe is omitted; use Launcher.exe. Start the game only with the root
`Launcher.exe` to get the splash, shared settings and startup handoff. The internal
binary is labelled Development; it is not a Shipping build.
- `Settings/GameUserSettings.ini`: shared video, graphics, audio, interface, keybindings and Data folder settings.
- `Settings/WowCredentials.json`: your saved server profiles, shared with development launchers using this settings folder. Packaging migrates the earlier Saved/WowCredentials.json only when no shared store exists. Existing profiles and the original file are preserved. Keep this personal file private when sharing a distribution.

Select the Data folder containing `common.MPQ` in the settings app. The folder can
be outside this distribution. Close the game before editing settings externally.
Changes are saved atomically with a `.bak` copy. Rebuilding preserves existing settings.

Borderless follows desktop size. Choose Windowed and 1920 x 1080 to run a 1080p
window on a 4K desktop. In-game video changes require Apply Video.

If a computer is missing the Microsoft C++ runtime, install the included
`Engine/Extras/Redist/en-us/vc_redist.x64.exe`.
The launcher/settings tool uses .NET Framework supplied with Windows.

Logs and runtime caches are normally under `WowUnreal/Saved/`.
The whole directory can be moved; the external Data folder setting must remain valid.
Existing gameplay bugs and unfinished features are not resolved by packaging.

For developers: `Scripts/BuildStandalone.ps1` builds both targets, prepares project-owned
shader graphs and cooks/packages the game into dist. `BuildDesktopClient.ps1` alone
only rebuilds the small launcher and settings tool. Material preparation never imports
or distributes MPQ textures, meshes, sounds, fonts or animations.

Weather settings are in the separate WowUnrealSettings.exe, on the Weather tab.
Each user can enter coordinates or click Use my location (Windows). This asks the
Windows location service with permission UI allowed. If access is denied, disabled
or unavailable, the previous saved location remains unchanged. The adjacent button
opens Windows location permissions. Save commits the selected location and real-weather
choice to the shared INI; restart the game to apply. Update the location here when travelling.
There is no fixed city and no automatic IP geolocation service. The game sends the
saved coordinates to Open-Meteo for weather only when real weather is enabled.

Interface → Mouse cursor selects Hardware (default) or Software for the entire game,
including login and character selection. Both use your own local WoW cursor art.
Save and restart the client. If native cursor creation is unavailable, the game
falls back to the same artwork as a software cursor.

Saved passwords now use Windows DPAPI for the current Windows user. The active legacy profile store is upgraded on load; old backups are not rewritten. Copying this folder to a different Windows account or machine may require re-entering saved passwords. Never distribute personal profiles. Depth of Field strength is adjustable in Graphics (0.5–4.0); the effect remains optional.

## Addons and loose file overrides

Use these folders beside the public `Launcher.exe` launcher:

- `Interface/AddOns/AddonName/AddonName.toc`: extracted WotLK addons, with their Lua/XML and other files beneath the same addon folder.
- `Interface/Icons/`: replacement UI icons at their original virtual paths.
- `Sound/`: replacement sounds, including the original subfolders and filenames.

Other original asset paths (for example `World/`, `Character/` and `Fonts/`) can be
created here as needed; there is no fixed folder whitelist. Files must use a format
already supported by the client. Merely renaming a PNG to BLP or an MP3 to WAV does
not convert it. Replacement files need the exact asset path the game requests.

Lookup priority is loose files beside this launcher, loose files beside the selected
WoW `Data` folder, loose files inside `Data` (legacy support), then MPQs in their
existing patch order. A duplicate addon is discovered once. Loose file priority is
per file, so keep complete addon versions together. Restart after adding/removing
addons or replacing assets because textures, models and sounds may already be cached.
Remove the override and restart to restore the underlying original asset.

These folders do not imply full addon compatibility: WoW API/event/secure UI support
is still partial. The client does not bundle addons or Blizzard assets.

## Screenshots

Bind Screenshot in Key Bindings (for example F12). Images include the visible UI
and are written to Screenshot/ beside the launcher, using unique filenames.
SDR images are PNG; HDR captures are EXR. A centered "Screenshot saved" appears
only after the file has been written and is hidden before the next capture.
Unbound modifier combinations are left alone for external screenshot tools.

## Visual Studio Programs
Run `Scripts/RegisterDesktopPrograms.ps1` after regenerating the UE solution. Programs contains Launcher, WoWSettings and OfflineEditor; right-click a project and Build. They are not implicitly rebuilt with every whole-solution build. Launcher/Settings builds preserve shared settings. OfflineEditor requires Python with PyInstaller and Pillow and remains a private tool.

Use `Scripts/BuildStandalone.ps1` to build/cook and assemble the complete distribution. UE Package alone produces Engine/, WowUnreal/ and its bootstrap; build Launcher separately for the same output folder.
