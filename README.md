# WoWUnreal

A World of Warcraft 3.3.5a (Wrath of the Lich King, build 12340) client built on **Unreal Engine 5.8** for Windows. It reads the WoW data you already own (MPQs) at runtime, connects to 3.3.5a private servers, and runs the original interface and addons through its own Lua 5.1 and FrameXML implementation.

This repository is **Stefan's (stefanw1337) continued development** of [Clancey/WowUnreal](https://github.com/Clancey/WowUnreal). The fork adds, among other things: a Windows launcher and settings app, realistic lighting and weather (optionally your real local weather, translated per zone), rain puddles and wet ground, UE water and waterfalls, stone relief on walls, Lumen lighting, HD character support, VR (OpenXR), DLSS/FSR, an offline mode, server-specific data folders, and a large amount of protocol, UI and addon compatibility work.

**No Blizzard assets are included.** Every texture, model, sound and database comes from your own legal copy of a 3.3.5a `Data` folder (MPQs).

## Contents

- [Vision](#vision)
- [Status](#status)
- [Download (pre-built)](#download-pre-built)
- [Features](#features)
- [Customising your client](#customising-your-client)
- [Goals](#goals)
- [Wishlist](#wishlist)
- [Known issues](#known-issues)
- [Building from source](#building-from-source)
- [Project layout](#project-layout)
- [Contributing](#contributing)
- [Fair play](#fair-play)
- [A request to AzerothCore and TrinityCore developers](#a-request-to-azerothcore-and-trinitycore-developers)
- [Credits](#credits)
- [Legal](#legal)
- [Licence](#licence)

## Vision

My vision for WoWUnreal is a more realistic World of Warcraft: darker, physically based lighting (Lumen), a real sky with weather and time of day, wet ground and puddles, relief on stone and brick, and real water. On the graphics side the next step is NVIDIA DLSS 5 Neural Rendering, on top of the DLSS and FSR upscaling and frame generation that are already in.

WoWUnreal reads any `Data` folder with MPQs, so you are free to take it wherever you like, even your own WoW-like game that isn't WoW at all. That freedom is a good thing, and it's part of why this is open source. I only have one wish: if you pick this project up, keep the realistic vision going.

– Stefan

## Status

**A work in progress.** WoWUnreal is playable, but it has never been played through from start to finish. It is open for anyone to contribute to, or to take up the torch and carry it on themselves. It speaks the standard 3.3.5a protocol, so it works with servers based on AzerothCore or TrinityCore. Tested against:
- AzerothCore 3.3.5a (with the optional companion module in `Server/mod-wowunreal`)
- TheraWoW and TormentWoW (each with its own client Data folder)

WoWUnreal is an independent project. It is not affiliated with AzerothCore, TrinityCore or any private server.

## Download (pre-built)

Pre-built Windows packages are attached to the [GitHub releases](../../releases). You need Windows 10/11 64-bit, a DirectX 12 graphics card and a legal copy of a World of Warcraft 3.3.5a `Data` folder (the MPQs).

1. Unzip the package anywhere you can write to (not `Program Files`). It contains the client folder and `Server\mod-wowunreal`, the optional AzerothCore module for server owners.
2. Run `WowUnrealSettings.exe`, select your own `World of Warcraft 3.3.5a\Data` folder, save and close. Under Servers you can give each server its own Data folder.
3. Start `Launcher.exe` and log in to your server.

If the game doesn't start, install the Visual C++ runtime from `Engine\Extras\Redist\en-us\vc_redist.x64.exe`. The packaged client is a Development build, so it logs more than a final build would.

## Features

### Graphics & lighting
- Display mode (fullscreen, borderless, windowed), resolution, refresh rate, quality presets from Low to Cinematic, and a render scale of 50–150 % (Options > Video).
- Anti-aliasing and upscaling options are FXAA, TAA, TSR (the default), AMD FSR (Native AA to Ultra Performance), NVIDIA DLSS (Quality to Ultra Performance) and NVIDIA DLAA. Each vendor option shows up only when its plugin and the right hardware are present.
- Frame generation uses NVIDIA DLSS Frame Generation (with Reflex, and a choice of frame multiplier where the GPU supports it), or AMD frame generation when FSR is selected. Only one runs at a time, and it stays off while the VR headset is on.
- "Sync" option: G-Sync/FreeSync first, with VSync as a fallback. When frames run slow, VSync is released briefly instead of dropping to 30 fps.
- Volumetric fog, hardware ray-traced shadows, AMD Anti-Lag 2 and motion blur (off by default), all under Options > Video > Advanced.
- Lumen global illumination and reflections (experimental, Options > Video > Advanced, off by default). Surface cards for terrain tiles and models are built at runtime.
- Darker, more realistic lighting: darker nights and shadows, and much stronger lamps and fires that also bounce light onto walls and floors (experimental, Options > Experimental > World & Sky).
- Contact shadows from Lumen short-range ambient occlusion (experimental, on by default, Options > Experimental > World & Sky).
- WoW's flat glow cards on braziers, torches and lamps are replaced with real point lights and a flickering procedural flame. Street lamps light up at dusk, and windows that were made to glow at night light up after dark.
- Banners, flags, tents and rugs get cloth shading, and cloth on models sways in the weather's wind.
- Gamma slider (default 2.2). Ctrl+R shows the FPS, optionally with Game/Render/RHI/GPU frame times and upscaler, frame-generation and VSync status. With frame generation on, it shows both presented and rendered fps.

### Sky, weather & time of day
- Sky, light and fog come from Light.dbc and blend between zones, drawn on UE's sky atmosphere. The sky clock follows the realm's time.
- Realistic 3D cumulus clouds that tower up in storms, plus sun glare rays, light shafts past trees and mountains, and a lens flare (experimental, on by default, Options > Experimental > World & Sky).
- A star field and a shaded moon whose phase follows today's real date. An optional setting makes the moon rise and set at realistic times (experimental, off by default, same tab).
- Moon size 100–500 % and sun size 100–1000 %; a bigger sun gives softer shadows (Options > Experimental > World & Sky). For a photographic moon, put your own moon map at User\Sky\Moon.jpg.
- Distance fog slider: far land fades into the sky colour, from off up to 200 % of WoW's amount (Options > Experimental > Weather, and in the settings app).
- Weather types are rain, thunderstorm, snow, blizzard, sandstorm/dust, ash fall with glowing embers, fog and overcast. They can mix, for example fog with rain, or a sun shower.
- Weather source, chosen in the settings app's Weather tab:
  - The realm's weather (the default). Zones the realm leaves empty get light, slow-moving local weather fronts.
  - Real weather.
  - The client's own zone simulation.
- Real local weather: optional, uses Open-Meteo with the location you enter or get from Windows location, translated per zone. Rain at home can be a sandstorm in Tanaris, ash in Searing Gorge, or snow in Dun Morogh, and in Alterac or Grizzly Hills it falls as rain or snow depending on your real temperature.
- Extreme weather allows blizzards, thick sandstorms and strong thunderstorms at full strength. Without it, all weather stays at or below 30 % (experimental, off by default, Options > Experimental > Weather). Race starting valleys always stay gentle.
- Thunderstorms have branching lightning drawn with the Lightning Bolt spell's texture, coloured flashes at night, and thunder.
- Weather knows when you are indoors:
  - Rain and snow stop under real building roofs, and blow a short way into doorways that face the wind.
  - Weather fog stays out of rooms but can still be seen through the door.
  - Caves and mines get their own haze.
- Weather changes gradually when you cross a zone border, and is set at once after login, teleports and loading screens.
- Rain wets terrain and building floors and forms soft-edged puddles that grow over time, show raindrop rings and mirror the sky. They dry slowly once the rain stops, and footsteps splash through them.
- Weather on the camera (Options > Interface > Camera, also Options > Experimental > Weather):
  - Raindrops and melting snow on the lens.
  - Drips after you leave water.
  - Frost in the corners and on the minimap, with a blue tint in cold places. It thaws in shelter or near a fire.
- Subtle zone atmosphere adds patchy haze, dust or cold mist that suits the zone (experimental, on by default, Options > Experimental > Weather).
- Tumbleweeds roll downwind through dry zones and race in sandstorms. They are local and cosmetic only, with no collision.

### Water
- Rivers and lakes use UE single-layer water (depth absorption, refraction, reflections) with extra local waves.
- The ocean has a wind-driven swell that steepens near the shore, plus shoreline surge, beach foam and whitecaps. The sea follows the weather, and swimmers and the camera ride the waves.
- Waterfalls use flowing foam layers, with spray mist at the base and sediment puffs under the surface.
- Lava and slime glow.
- A small wake forms while wading or swimming, with splash sounds. Underwater, the picture is colour-graded and sound is muffled.

### World rendering
- Maps, models, textures, sounds and databases are read at runtime from your own 3.3.5a MPQs (ADT/WDT/WDL, M2, WMO, BLP, DBC). Nothing is imported or shipped.
- Terrain streams in with 4-layer texture splatting, a simplified middle-distance level of detail and WDL distant terrain. Tiles and large buildings are built in small time slices to avoid hitches.
- WMO buildings use Nanite and their authored collision, and doodads are instanced.
- Original room and portal visibility: a building's rooms and Stormwind's districts are drawn only when you are in them or can see them through an opening. A related option hides the low-detail mountain backdrop over Stormwind Keep while you are in the city. Both are experimental, on by default, in Options > Experimental > World & Sky.
- Stone relief: stone and brick walls get a parallax relief with the gaps pressed in (experimental, on by default). Cobbled streets, floors and roads can get it too, but that's still rough and off by default. Both are under Options > Experimental > World & Sky and need a restart.
- Ambient animals inside buildings (such as the birds over Stormwind) are animated. Chests and doors play their opening animations, and objects fade out when they despawn, as in the original client.
- Boats, zeppelins and elevators move on the client using the same path data as the server. You can ride them, including continent changes on board.
- Area triggers are reported the way the original client does, so dungeon portals, inns and exploration objectives work.
- Quest objects and gathering nodes get a glint (experimental, on by default, Options > Experimental > Effects).
- Loot beams: a pillar of light over each corpse you can still loot, coloured by its best item. Servers running mod-loot-beam can colour it before you open the loot (experimental, off by default, Options > Experimental > Effects).

### Characters & animation
- Characters are built from race, gender and customization, with composited skin textures, geosets, attached gear, weapon enchant effects and sheathed weapons.
- The original M2 particle emitters are fully simulated (emission, gravity, colour/alpha/scale tracks, trails, bone following). Spell missiles fly to their target and turn toward the struck point on critical hits.
- Mounts are drawn for your own character as well as others, including the taxi gryphon on flight paths.
- Feet are placed on slopes and steps (foot IK), four-legged creatures and mounts tilt with the ground, and NPCs are visually fitted to the terrain.
- Your character turns its head toward your target or where the camera looks (Options > Interface > Camera). The NPC you target looks back at you; only you see this.
- HD character models from an HD pack (a patch-H or patch-HD MPQ plus HD_Data textures) can be used for your own character. Humanoid NPCs and other players can also be swapped in one at a time, nearest first (experimental, off by default, Options > Experimental > HD Models; greyed out without HD data).
- Stealth looks for you and stealthed party members: Shadow cloak (the default), Classic, Predator heat shimmer, or Shadow only. An optional cone in front of each nearby enemy shows how close you can get: blue is safe, amber may notice you, red has seen you (Options > Experimental > Classes).
- Death and the ghost world: the world drains to grey while you lie dead, the spirit world is a pale, cold blue haze, and ghosts glow with a soft rim.
- Under Options > Experimental > Character (all experimental):
  - Collision with NPCs, creatures and players.
  - Standing up before moving: moving while seated plays the stand-up animation first.
  - A switch to turn off attacking back automatically when something hits you.
- The barber shop previews the chosen look on your own model with the camera on the face. Fishing draws a line from the rod tip to the bobber.

### Interface & addons
- The original in-game interface (FrameXML Lua and XML from your MPQs) runs on the client's own Lua 5.1 VM, with memory and instruction limits. The login, realm list, character select and character create screens are native replicas drawn with the original glue art and strings.
- Addons load from Interface\AddOns, either in the User folder beside Launcher.exe or beside your WoW Data folder. Common 3.3.5 addons (Ace3, Bartender4, DBM and others) get compatibility functions they expect. API coverage is still partial.
- The AddOns list, at character select and in the Game Menu, lets you enable and disable addons, load out-of-date addons and reload the UI. It also shows each addon's load result or error count.
- SavedVariables are kept per account and per character in WoW's own format. For each realm you can use your WoW client's WTF settings: a fresh copy each launch, copy once, keep separate, or share with backup (/wowsettings).
- Macros work as in the original client: 36 account-wide and 18 per character, stored in macros-cache.txt.
- The Key Bindings window has two presets, search, character-specific bindings and extra commands (Toggle Torch, and isometric camera turning).
- Native replicas drawn with the stock art: talents (with dual spec and glyphs), Auction House, mailbox, achievements, Dungeon Finder and Raid Browser, the social window (Friends, Who, Guild), inspect, quest log, trainer and vendor.
- The world map zooms from zone up to the cosmic view and shows quest POIs, dungeon floors, and flight-master routes with prices. It has three styles: Classic, Blizzard's satellite map, and Satellite v2, which is built from top-down photos the client takes the first time you stand in each map tile in daylight (Options > Interface > Camera, on by default).
- Nameplates show level, cast bar and a quest-objective mark. V cycles between names only, names and nameplates, and target only.
- Chat bubbles (optionally also for party chat) and floating combat text. Combat text can add up hits on the same target, and the heals and resource gains you receive.
- Super-tracked quests (Ctrl-click in the objectives tracker) get a marker over the objective area with the quest title and distance, and an arrow at the screen edge when it is off screen. A corpse marker shows the way back to your body.
- Wrath-style interface options pages: Quests, Names, Combat, Display, Action Bars, Unit Frames, Tooltips, Camera, Mouse.
  - UI scale 64–150 %, with a separate value for VR.
  - Separate sizes for unit frames, action bars and the minimap, plus tooltip size and fade time.
  - Hold Ctrl and drag to move a frame; Reset frames puts them back.
- Immersive interface: the UI fades out while you move, and in combat only the action bars and unit frames stay, at 40 % (experimental, off by default, Options > Experimental > Effects).
- The cursor can be hardware or software, uses the WoW gauntlet art, and has an adjustable size. Custom cursor sets can come from folders under User\Cursor, including animated .ani and per-race sets (experimental, Options > Interface > Mouse).
- Loose-file overrides: a replacement icon, sound or any other asset placed at its original path is used instead of the MPQ version.

### Gameplay & quality of life
- Auto Loot with a choice of reverse key (Shift, Ctrl, Alt or none), and an option to open the loot window at the mouse.
- Four extra action bars, empty-slot display, cooldown numbers on buttons, and holding an action key to recast as soon as the spell is ready.
- Quest options: instant quest text, automatic tracking of new quests (remembered per character), quest levels in titles, tracking every quest in the current zone, and a classic native objective tracker as a fallback.
- Show Helm and Show Cloak, synced with the server.
- The vendor window has checkboxes to sell grey items and repair automatically.
- Destroying an item always asks for confirmation. Epic and better items, and Bind-on-Equip items that are not yet bound, need "DELETE" typed.
- Ctrl+T lights a burning torch on your belt with a flickering light, when darker lighting is on. It is local only.
- Dressing room preview. Ctrl-clicking a mount shows it there too (experimental, on by default, Options > Experimental > Effects).
- Ground painted with ice is slippery: stops slide about a metre and turns drift, without going faster than normal.

### Camera & photo mode
- Photo mode (hold F12) is purely local:
  - Free camera, field of view and roll.
  - Depth of field with character, centre or manual focus and an aperture setting.
  - Nine filters, plus exposure, contrast, saturation, white balance, vignette, grain, bloom and chromatic aberration.
  - Letterbox, poses and freeze time.
  - Hiding your own character, or other players and NPCs.
  - Time of day and weather.
- Screenshots (tap F12 or Print Screen) are saved as PNG, or EXR when HDR output is on, with unique names in the Screenshot folder, and an on-screen notice confirms each one.
- Depth of field with autofocus at the screen centre and an adjustable strength, plus manual focus test sliders (experimental, off by default, Options > Experimental > Camera & Lens).
- First-person immersion: a close-in view with your own body visible when you look down, and an optional helmet view (experimental, off by default, Options > Experimental > Character).
- Two extra camera styles, only one active at a time (experimental, Options > Experimental > Camera & Lens):
  - Isometric (Diablo-style): fixed high angle, 12–45 m zoom, and roofs and ceilings above you cut away. Turn with the middle mouse button or the , and . keys.
  - Don't Starve: a distant top-down 2D look with no zoom or turning.
- Camera turn sensitivity (Options > Interface > Camera).

### Audio
- Zone music and ambience come from the game data, with day and night variants and crossfades. Loop Music keeps music going between tracks. There are separate volumes for master, effects, music and ambience, and an option to keep sound playing in the background.
- Unit sounds as in the original client: creature aggro, attack, wound and death, NPC greetings and farewells, weapon swings and impacts, other players' combat grunts, and sounds and music scripted by the server.
- Rain and storm loops play in bad weather and sound softer through walls when you are indoors.
- Sound is muffled when the camera is underwater, and world audio is muted during loading screens.
- The intro movie before character select can play every time, the first time only, or never. It can be skipped and can show subtitles (.srt or Blizzard .sbt).
- A new character's first login plays the race intro flyby with narration over the live world, or a class intro movie if the server provides one.

### VR
- Seated VR through OpenXR (SteamVR, Meta, Virtual Desktop and others) in the game world. Ctrl+Shift+V switches it on and off, and every launch starts with VR off (experimental, Options > Experimental > Virtual Reality).
- The whole interface moves onto panels around you that fade in when you look at them. Nameplates, quest markers, chat bubbles and combat text become cards in the world at their real depth.
- An option keeps the headset view always in first person with your body visible. Look-down gain and leaning over let you see your feet while seated.
- You can aim with your head (centre crosshair) or with the mouse.
- Stare-to-click: resting the crosshair on something in the world or on a UI button clicks it after 0.5–2 s (experimental, off by default).
- Comfort presets (Comfortable, Balanced, Immersive), plus snap turning (15–90°), a comfort vignette, movement acceleration, head stillness, depth of field in the headset, and a Recenter button.
- VR graphics levels (Potato, Performance, Balanced, Full) apply only while the headset is on. Frame generation and motion blur are always off in the headset.
- Personal space: monsters in melee range are drawn slightly further back so they don't fill the lens. Their server position is unchanged.

### Launcher & settings app
- Launcher.exe starts the game with a splash screen and startup progress, then hands focus to the game window. It won't start a second copy, and after a crash it reports the exit code and the faulting module.
- WowUnrealSettings.exe (or `Launcher.exe -settings`) edits settings without starting the game. Tabs: Data / Display, Servers, Graphics, Weather, Sound, Interface, Experimental, and a raw INI editor.
- Data / Display: a WoW Data folder picker that checks for common.MPQ, a language choice from your installed locales, display mode, window resolution, VSync, an FPS limit, and a "Safe video: 1080p" button.
- Servers: a Data folder per server. The login screen uses the default folder, and after you log in the client switches to that server's data (music, textures, models, addons) before showing the realm list.
- Graphics: the individual UE quality groups (shadows, textures, view distance and others), render scale, upscaler, depth of field, Lumen and ray tracing.
- Weather: weather source, coordinates or "Use my location (Windows)" with a link to the Windows location permissions, distance fog, and the lens effects.
- Experimental: the same switches as in game, plus stealth look and camera style, so you can turn off a test that breaks the game from outside it.
- Changes save automatically about once a second and wait while the game is running. Files are written atomically with a .bak copy.
- Saved passwords are encrypted with Windows DPAPI for the current Windows user. Settings, saved servers, addons and screenshots live in the User folder beside Launcher.exe.

### Networking & servers
- Uses the standard 3.3.5a protocol (build 12340): SRP6 login, realm list and an encrypted world session, so it works with AzerothCore and TrinityCore servers.
- Native realm list with sortable columns and category tabs. Character select has a 3D preview, the realm name and an AddOns button, and deleting a character needs "DELETE" typed.
- The login screen shows the original animated 3D login scene, with Classic, TBC and WotLK themes where your data has them. The theme also picks which saved servers are listed. HD or custom login artwork is optional (Options > Video on the login screen).
- Saved server profiles hold an alias, address and port, plus Terms of Use, Official Site and Server Account buttons on the login screen.
- Servers can provide extras:
  - A news panel (Interface\Glues\ServerNews.txt).
  - The server alert, fetched from the patchlist address as the original client does.
  - Their own login and class intro movies, with rules in MovieRules.txt.
- Basic Warden handling answers common Warden requests so servers don't disconnect the client. It is not meant to pass full checks.
- Optional AzerothCore module, Server/mod-wowunreal:
  - It recognises the client through a hidden addon-message handshake.
  - It can turn away clients that don't complete the handshake.
  - It tells the client which extras the realm allows, so the server accepts them instead of treating them as cheating:
    - a first-person lock for the realm (working today);
    - weather gameplay: slower movement in blizzards and sandstorms, and tiny damage from the fire specks in ash storms;
    - sea waves that lift you up and down while you swim.
  - The weather and wave flags are already sent, but the client and the module don't act on them yet. They're on the wishlist.
- Offline mode (`Launcher.exe -offline`) gives you the login screen, a local character list and the world without any server.

### Developer tools
- Model viewer map (/Game/Maps/ModelViewer): cycle race, gender and customization, apply a weapon or starter gear, draw or sheathe weapons, and spawn any creature display ID, with an orbit camera.
- Offline mode keeps characters in OfflineCharacters.json with race start locations, and dungeon portals work offline from a table of AzerothCore teleports.
- Tools/OfflineEditor (Python/Tk) edits offline characters, picks a start point on the map, and can move or fly the running offline character live.
- The Options > Test page in non-Shipping builds has weather preview and mixing with a strength slider, sea state, a test clock, and foot IK and ground tilt switches. Nothing on it is saved.
- Commandlets:
  - `-run=WowMapCreator` creates the test and viewer maps.
  - `-run=WowMpqExtract` dumps stock interface files from your MPQs for reference.
  - `-run=WowPrepareStandalone -preparestandalone` builds the materials the project generates in code, before cooking.
- About 200 UE automation tests (parsers, network packets, UI, combat, water, weather, lighting and more) in Source/WowTests and the modules' Tests folders, plus Python tests for the offline editor.
- In-game diagnostics: the /fstack frame-stack inspector (Alt freezes it, Ctrl+C copies it), /wowdiag frame dumps to the log, and the `-debughud` overlay.
- Build scripts: Scripts/BuildStandalone.ps1 builds, prepares materials, cooks and packages. Create-Share-Build.cmd makes a clean copy without your personal files.

## Customising your client

Everything personal and everything you add lives in the `User` folder next to `Launcher.exe`. Restart the game after adding or removing files there.

| Folder | What goes there |
|---|---|
| `User\Settings` | Your settings (`GameUserSettings.ini`) and saved server profiles. Passwords are encrypted for your Windows account. Never share this folder. |
| `User\Interface\AddOns\<Name>\<Name>.toc` | WotLK (3.3.5a) addons, unpacked, one folder per addon. |
| `User\Interface\Icons`, `User\Sound`, `User\World`, ... | Replacement game files at their original paths, in a format the client already reads (renaming a PNG to `.blp` does not convert it). Files here win over the MPQs. |
| `User\Cursor\<Set name>\` | Your own mouse cursors (see below). |
| `User\Sky\Moon.jpg` or `Moon.png` | A photo of the Moon for the night sky (see below). |
| `User\UserSounds\Thunder\` | Your own thunder sounds (see below). |
| `User\Screenshot` | Screenshots (PNG; EXR when HDR is on). |

The game log is `WowUnreal\Saved\Logs\WowUnreal.log`.

### Moon photo

The night sky draws a stylised moon with the right phase for the date. To use a real photo, put an equirectangular colour map of the Moon in `User\Sky\` as `Moon.jpg` or `Moon.png`. NASA's public-domain Lunar Reconnaissance Orbiter colour map works well. Nothing is shipped with the client; the file is only read from your own folder.

### Thunder and lightning sounds

During thunderstorms, lightning strikes come with thunder. By default the client generates its own rumble. To use real recordings, put `.wav`, `.ogg` or `.mp3` files in `User\UserSounds\Thunder\`:
- a file with `close` in its name is used for near strikes (a sharp crack);
- a file with `distant` in its name is used for far-away rumbles;
- any other file can be used for both.

The client picks a different file each time and follows your ambience volume.

### Cursor sets

Every folder in `User\Cursor` is a cursor set. Choose it under Options > Interface > Mouse.
- Formats: `.png`, `.tga`, `.bmp`, `.jpg`, `.blp`, `.cur`, `.ico` or animated `.ani`, up to 256 x 256.
- File names: name files like the client's cursors (`Point`, `Attack`, `Speak`, `Buy`, `Quest`, `QuestTurnIn`, `Inspect`, `Pickup`, `Interact`, `Cast`, `UnablePoint` ...). Windows cursor sets work as they are (`Normal`, `Link Select`, `Precision`, `Unavailable` ...).
- Choosing files by hand: write lines like `Attack = red.ani` in a `cursors.txt` file.
- Per race or faction: a set can contain a folder per race (`Human`, `Orc`, `NightElf`, `BloodElf` ...) or `Alliance` / `Horde`; your character's folder is used.
- Click point: taken from `.cur`/`.ani` files, or from `hotspot.txt` lines like `Point 3 2` (pixels).

### Real weather

In `WowUnrealSettings.exe` > Weather, enter coordinates or click *Use my location* (Windows location service), and turn on real weather. Your local weather is then translated per zone: rain at home can become a sandstorm in Tanaris or snow in the mountains. The coordinates are only sent to Open-Meteo, and only while real weather is on.

### Screenshots and photo mode

Tap **F12** (or Print Screen) for a screenshot; the key can be changed in Key Bindings. Hold **F12** for photo mode: a free camera with lens, filters, poses, time of day and weather. Tap F12 there to take a photo; hold F12 again or press Esc to leave. Everything in photo mode is local; nothing is sent to the server.

## Goals

- **Custom servers.** First-class support for private servers with their own content, starting with **TheraWoW** and **TormentWoW**. That means each server's own client `Data` folder and patch MPQs, custom maps and database files, and the large addon packs these servers ship (ElvUI and similar).
- **Conquest of Azeroth (CoA).** Logging in to CoA, the open-source continuation of Project Ascension, and showing the interface elements its server provides, once CoA is back online.
- **HD packs.** Full support for 3.3.5a HD texture and model packs (`patch-H`/HD data): HD login screens, an HD model for your own character, and HD models for NPCs and other players. Some of this is already in and marked experimental.
- **Other expansions.** Vanilla (1.12) and The Burning Crusade (2.4.3) clients are a later goal; parts of the login screen and data loading already know about them, but they are untested.
- **Realism.** DLSS 5 Neural Rendering and further work on the darker realistic lighting, sky and weather (see the Wishlist below).

## Wishlist

These are things we planned or dreamed about but didn't get to. If you want to help, pick one and open a pull request. It doesn't have to be in this order.

### A more realistic world
- NVIDIA DLSS 5 Neural Rendering, and DLSS as its own upscaler next to FSR
- Darker, more natural nights lit by the sun, the moon and the weather
- Real lights for lamps, lanterns and wagons, fog and light shafts in buildings, and glowing windows in Stormwind
- A bright-grey overcast sky on cloudy days (today a fully overcast real-weather day can look almost black)
- Real astronomy as an option: day and night, sunrise, sunset and moonrise from your own location and date
- Mixed and changing weather, wind that moves foliage and particles, tumbleweeds in barren zones, per-zone climate tuning, and an optional weather feed shared by a whole realm
- Stone relief on streets and ground that's ready to be on by default: smoother stone edges, and the dark only in the gaps (or moss there) instead of inside the stones
- Normal and roughness maps and real material types (bark, stone, metal, cloth), cleaner foliage edges and denser ground clutter at the highest settings
- Water that follows the sky's brightness, wakes and ripples from every player and NPC, and a reflection quality setting
- Birds and butterflies that flap and fly their paths

### More servers and expansions
- Full support for custom servers like TheraWoW and TormentWoW, including their addon packs (ElvUI) and the AIO addon framework
- Conquest of Azeroth (CoA): logging in and showing the interface elements the server provides, once CoA is back online
- Vanilla (1.12) and The Burning Crusade (2.4.3) servers and clients
- Two-factor login codes (for example AzerothCore's TOTP)
- Hardening against untrusted servers: size limits on packets and compressed data, a clean disconnect with a reason, and fuzz-tested parsers
- Weather and wave gameplay through the companion module: slower movement in blizzards and sandstorms, tiny damage from fire specks in ash storms, sea waves lifting you up and down, all accepted by the server; plus server news

### The whole game, playable
- Battlegrounds and arenas (queueing, the real PvP frame) and Wrath vehicles
- Crafting windows, gathering, lockpicking, the pet spellbook and autocast, the stable master, inspect and pet talents, and placing glyphs in their sockets
- The last few hundred stock Lua API functions, so every Blizzard window and popular addon (DBM, Bagnon, Details, Ace3 option windows ...) works
- Chat links, the chat configuration window, full tooltips (set bonuses, enchants, spell formulas), target-of-target and focus frames, a zoomable world map with fog of war, and minimap tracking dots
- Combat text options, aura icons on nameplates, and immersive sleep/sap/stun effects
- Footsteps by surface, weapon swing and block sounds, and jump and landing sounds
- Other players sitting, kneeling, sleeping and swimming the way the original client shows them

### Characters and presentation
- HD packs: every HD model loading cleanly, and HD character select framed right
- Character creation with the original art, texts and sounds
- First person checked for every race (helmets, big weapons, mounts), with an optional helmet visor
- 3D login scenes per expansion (Dark Portal, Outland, Icecrown) and per server, with login music
- Photo mode presets, an orbit/dolly camera, more poses and click-to-focus
- Depth of field that keeps your target in focus

### VR
- Remembering VR between sessions, a body that always shows properly when you look down, motion controllers with a laser pointer, and a mirror of the interface on the monitor

### Speed
- Building other players, terrain, models and buildings off the game thread, so busy cities and travelling don't stutter
- A custom terrain shader with fewer draw calls per tile
- Decoding MP3 music and sounds off the game thread

### Launcher
- An AddOns folder per server in the settings app, and clearer addon loader messages
- A macOS port (postponed)

## Known issues

The biggest known issue is everything we don't know yet. Not every class, race and level has been tested, and nobody has played the game through from start to finish in WoWUnreal, so expect plenty of problems nobody has found yet. Please report what you find, or fix it and send a pull request.

- Warden: WoWUnreal can't answer Warden like the original client (see the request to server developers below)
- Hundreds of smaller fixes were made but never confirmed in game. They're tracked in our own test log, too many to list here
- The calendar still has open problems
- Waterfall mist: where the Northshire waterfall runs out into the pond, the mist still hangs a little above the water
- TormentWoW: ElvUI still raises some Lua errors
- With real weather on, a fully overcast day can look almost black
- Weather particles can show under roofs at some camera angles, and snow outside isn't visible from indoors
- Some solid objects don't block movement yet, and some stairs stop the character
- Quest rewards can show as "Reward Item 1234" with a "?" icon
- Other players can get stuck in a wall for a few seconds and are shown running instead of swimming
- Loading other players and new areas can stutter in busy places
- FSR frame generation can crash when it's switched on and then off again in game
- Closing the game window can leave the client or launcher running in the background
- VR always starts off unless you start the client with `-vr` (Ctrl+Shift+V toggles it in game)

## Building from source

Requirements:
- Windows 10/11, Visual Studio 2022 (Desktop C++ and .NET desktop workloads)
- Unreal Engine 5.8
- The engine plugins in the table below, installed in `Plugins/` or in the engine. NVIDIA DLSS/Streamline and AMD FSR are linked by the C++ code, so they are required.
- A legal copy of a World of Warcraft 3.3.5a `Data` folder (the MPQs) to run it (enGB tested)

Steps:
1. Clone the repository and install the plugins listed below.
2. Build the editor: `Engine\Build\BatchFiles\Build.bat WowUnrealEditor Win64 Development -Project="<path>\WowUnreal.uproject"`
3. Build and package a client: `Scripts\BuildStandalone.ps1 -EngineRoot "<UE 5.8 folder>"`. This builds the editor, prepares the project's materials (`-run=WowPrepareStandalone`; they are generated in code), builds the game, cooks, packages into `dist\` and builds the launcher.
4. `Create-Share-Build.cmd` makes a clean, shareable copy (`dist-share\`) without your account, settings, logs or any MPQ.

Prebuilt `lua.lib` (Lua 5.1.5) and `StormLibRAD.lib` (StormLib) are included under `Source/ThirdParty` so the project links out of the box.

### Engine plugins

UE expects every plugin listed in `WowUnreal.uproject` to be installed. If you don't have one of the optional ones, remove or disable its entry.

| Plugin | What WoWUnreal uses it for | Required to build | Where to get it |
|---|---|---|---|
| Water | The surface material (`Water_Material_CustomMesh`) and foam textures for rivers, lakes, ocean and waterfalls | Yes: `WowWorld.Build.cs` depends on the `Water` module | Ships with UE 5.8 |
| FSR | AMD FSR upscaling and AMD frame generation; the FPS overlay reads its frame-interpolation state | Yes on Win64: `WowUnreal.Build.cs` links `FFXFrameInterpolation` | AMD GPUOpen (FSR plugin for UE 5.8) |
| DLSS | NVIDIA DLSS upscaling and DLAA | Yes: `WowClient.Build.cs` links `DLSSBlueprint` | NVIDIA developer site (DLSS plugin for UE 5.8) |
| StreamlineDLSSG | NVIDIA DLSS Frame Generation and its mode choice | Yes: `WowClient.Build.cs` links `StreamlineDLSSGBlueprint` and `StreamlineBlueprint` | NVIDIA developer site (Streamline plugins for UE 5.8) |
| StreamlineReflex | NVIDIA Reflex, switched on together with DLSS Frame Generation | Yes: `WowClient.Build.cs` links `StreamlineReflexBlueprint` | NVIDIA developer site (Streamline plugins for UE 5.8) |
| OpenXR | The seated VR headset view; the code talks to it through UE's `HeadMountedDisplay` module | No (not linked by the C++), but needed at runtime for VR | Ships with UE 5.8 |
| AntiLag2 | The AMD Anti-Lag 2 option (sets `r.AMD.AntiLag2`) | No (not linked by the C++) | AMD GPUOpen (Anti-Lag 2 plugin for UE 5.8) |
| ProceduralMeshComponent | Meshes built at runtime: particles, spell meshes, rain and snow, puddles, waterfall spray, tumbleweeds, VR interface panels, stealth cones, fishing line | Yes: WowAssets, WowWorld and WowUnreal depend on it | Ships with UE 5.8 |
| Paper2D | Not used anywhere in the source | No | Ships with UE 5.8 |
| ModelingToolsEditorMode | Editor only; not used anywhere in the source | No | Ships with UE 5.8 |

### Command-line switches

Launcher.exe passes every switch except `-settings` on to the game.

- `-settings`: Launcher.exe opens the settings window instead of starting the game.
- `-offline`: plays without a server. You get the login screen, a local character list and the world.
- `-offlineloading`: goes straight into the offline world behind a loading screen, skipping the login screen.
- `-offlinewowx=` `-offlinewowy=` `-offlinewowz=` `-offlineyaw=`: sets the offline spawn point in WoW coordinates.
- `-offlineworld -offlinerace=<id> -offlinegender=0|1`: spawns a test character of that race directly in the world, without the glue screens.
- `-vr`: turns VR on as soon as you enter the world (normally every launch starts with VR off).
- `-vrui`: puts the interface on the VR panel host without a headset, for testing.
- `-autologin`: logs in with the saved default server profile and enters the world with the first character.
- `-account=<alias>`: with `-autologin`, uses this saved profile instead of the default.
- `-createchar [-charname=<name>]`: with `-autologin`, creates a Human Mage if the account has no characters (for testing).
- `-wowdata=<folder>`: uses this WoW Data folder for this run instead of the saved one.
- `-GameUserSettingsINI=<file>`: the settings file to use. Saved server profiles are read from WowCredentials.json in the same folder. The launcher passes `User\Settings\GameUserSettings.ini`.
- `-loginexpansion=classic|tbc|wotlk`: login screen theme for this run.
- `-loginstatic`: skips the 3D login scene.
- `-timeofday=<minutes>`: starts the sky at this time (0–1439) and freezes it.
- `-moondate=<ISO 8601 date>`: shows the moon phase for this date.
- `-darkerlighting` / `-standardlighting`: forces a lighting profile for this run without changing the saved option.
- `-weather=rain|snow|blizzard|storm|dust|fog|ash` and `-weathergrade=<0..1>`: fixed weather for this run (for testing).
- `-weatherdiagnostics`: logs what the weather is drawing every few seconds.
- `-testscene=login|character|terrain|wmo|ui|network`: starts in a test scene instead of the normal login flow.
- `-startpos`: a free-flying camera over Northshire instead of a character, for viewing terrain.
- `-autoscreenshot=<file>` `-autoscreenshotdelay=<s>` `-autoquitdelay=<s>`: automatic capture and quit for scripted checks.
- `-noheightfog`, `-legacyskydome`, `-engineskydome`, `-noskydome`: sky debugging.
- `-debughud`: draws the debug HUD (FPS, camera position and more).
- `-ensures`: makes packaged builds report engine ensures again. They are off by default because the report froze the game for several seconds.
- `-preparestandalone`: needed with `-run=WowPrepareStandalone` to build the generated materials (`-paperdollonly` and `-rainonly` limit it).

## Project layout

| Folder | What it is |
|---|---|
| `Source/WowData` | File formats: MPQ (StormLib), DBC, ADT/WDT/WDL, M2/skin, WMO, BLP |
| `Source/WowAssets` | Models, characters, equipment, particles and materials |
| `Source/WowWorld` | Terrain, doodads, WMOs, water, sky, weather |
| `Source/WowNetwork` | Auth (SRP6), realm list, world protocol |
| `Source/WowUI` | Lua 5.1 VM, FrameXML/addon loader, native UI widgets |
| `Source/WowClient` | Preferences, key bindings, credentials, screenshots |
| `Source/WowUnreal` | Game mode, controllers, login/character screens, VR |
| `Source/WowTests` | Automation tests |
| `Tools/DesktopClient` | Launcher and settings app (C#) |
| `Server/mod-wowunreal` | Optional AzerothCore module |
| `Scripts` | Build, packaging and helper scripts |
| `specs` | Design notes per subsystem |

## Contributing

WoWUnreal has always been a work in progress, and it is now open for anyone to continue.

**This repository isn't actively maintained for now.** Pull requests and issues are welcome, but they may not be reviewed or answered quickly, or at all. The best way to carry the project on is to fork it and take up the torch yourself. Because of GPL-3.0, anyone who shares builds of their fork also shares its source, so improvements can always find their way back.

Bug fixes, features, server compatibility and documentation all help. (These guidelines are adapted from the original WowUnreal README.)

### Getting started

1. Fork this repository (stefanw1337/WoWUnreal). It has Clancey's original code plus all the newer work.
2. Create a feature branch: `git checkout -b feature/your-feature-name`
3. Make your changes and test them thoroughly
4. Commit with clear, descriptive messages
5. Push the branch to your fork and open a pull request to this repository, so the change can be reviewed and merged here

### Code style

- Follow the Unreal Engine coding standard (PascalCase, `F` prefix for structs, `U`/`A` prefix for UObject/AActor)
- Use meaningful names and comment non-obvious logic
- Add tests for new functionality where practical (`Source/WowTests`)

### Documentation

- Update the relevant file in `specs/` for protocol or file-format changes
- Add examples for new APIs

### Before opening a pull request

- Build the editor and a packaged client on Windows
- Run the automation tests
- Check that the game starts, logs in and enters the world without crashing
- Check that your change doesn't break existing behaviour, and that no WoW data or personal files are committed

### Reporting issues

1. Describe the problem and how to reproduce it
2. Note the server (AzerothCore, TrinityCore or the private server's name), your client language, and the WoWUnreal version or commit
3. Attach the relevant part of the log (`WowUnreal\Saved\Logs\WowUnreal.log` in a packaged build, `Saved/Logs` in the editor) and screenshots if useful. Remove account names, passwords and anything else personal first.

### Architecture

- `specs/overview.md`: architecture and goals
- `specs/networking.md`: network protocol
- `specs/`: one file per subsystem
- `SPEC.md` and `IMPLEMENTATION_PLAN.md`: the original project specification and plan

## Fair play

WoWUnreal exists to play World of Warcraft in a new engine, not to cheat. We do not condone using it, or code from it, to cheat, bot or exploit, or to attack, overload or otherwise harm servers, their staff or their players. Respect each server's rules, and don't use WoWUnreal on a server that does not allow third-party clients. All game rules stay on the server. Server owners can decide which WoWUnreal features their realm allows (for example a first-person lock) with the companion module in `Server/mod-wowunreal`.

## A request to AzerothCore and TrinityCore developers

WoWUnreal speaks the standard 3.3.5a protocol, so it works with servers based on AzerothCore or TrinityCore. It cannot answer Warden the way the original client does, because Warden checks the memory and files of Blizzard's own executable, which WoWUnreal does not run. Depending on a server's settings, it may therefore be flagged or disconnected.

We would love to see WoWUnreal accepted by default, alongside the original client. Some possible ways:
- a known-client rule, so that Warden results from WoWUnreal are logged instead of causing a kick, while server-side anti-cheat (such as movement checks) stays fully on;
- built-in support for the open handshake in `Server/mod-wowunreal`, which lets a realm recognise WoWUnreal releases (`WowUnreal.RequireClient`, `WowUnreal.ReleaseSecrets`) and tell the client which features it allows.

We know that a client which announces itself can be imitated, so we are not asking anyone to switch protection off. We are asking for a supported way to let players choose this client. We're happy to help with pull requests; please open an issue to talk about it.

## Credits

- **Clancey**, for the original [WowUnreal](https://github.com/Clancey/WowUnreal) that this fork continues.
- **Stefan (stefanw1337)**, for this fork.
- **Lua 5.1.5** (Lua.org, PUC-Rio), **StormLib** (Ladislav Zezula) and **pugixml** (Arseny Kapoulkine), all MIT. Their licence texts are in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
- **Unreal Engine** by Epic Games, **NVIDIA DLSS, Streamline and Reflex**, and **AMD FidelityFX Super Resolution and Anti-Lag 2**.
- **DLSS 5 Neural Rendering** was tested on AMD hardware with [DLSS-NR-on-AMD](https://github.com/danielblnc/DLSS-NR-on-AMD) by Daniel Blanco (danielblnc). It is a separate third-party project, not included with WoWUnreal and not endorsed by NVIDIA or AMD; if you want to try it, get it from its own page.
- **Weather data by [Open-Meteo.com](https://open-meteo.com)**, licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/), used when real weather is on.
- **AzerothCore**: the offline mode's dungeon portal table comes from AzerothCore's `areatrigger_teleport` data, and `Server/mod-wowunreal` is built for AzerothCore.
- **Loot beams** were inspired by [Loot Beam by manatactical](https://github.com/manatactical/wxl-loot-beam), and the client supports its server module mod-loot-beam. Our implementation is written independently.
- **Warcraft-style cursor sets** by **THTH** on [rw-designer.com](https://www.rw-designer.com) were used to build and test the cursor-set feature. They are not included; download the ones you like and put them in `User\Cursor`.
- The **Moon photo** how-to suggests NASA's public-domain Lunar Reconnaissance Orbiter colour map. It is not included.
- The splash screen and icon are Stefan's own AI artwork.
- World of Warcraft, its art and its data belong to **Blizzard Entertainment** (see Legal).

## Legal

World of Warcraft and Blizzard Entertainment are trademarks or registered trademarks of Blizzard Entertainment, Inc. This project is not affiliated with, endorsed by, or associated with Blizzard Entertainment. It ships no Blizzard game data, art, audio or interface code; you must supply your own legal copy of a 3.3.5a `Data` folder (MPQs). The project is unofficial and is meant for use with private servers you are permitted to use.

Third-party code in `Source/ThirdParty` keeps its own licence: Lua 5.1.5 (MIT), StormLib (MIT), pugixml (MIT). See [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) for the licence texts and the other notices.

## Licence

This fork is released under the **GNU General Public License v3.0** (see `LICENSE`). You may use, change and share it; if you distribute a build or a modified version, you must also make its source available under the same licence.

The GPL-3.0 licence covers this fork's own changes and additions. The original code comes from [Clancey/WowUnreal](https://github.com/Clancey/WowUnreal); that repository has no licence file, and the copyright in that original code stays with its author.

`Server/mod-wowunreal` is a module for AzerothCore, which is GPL-2.0-or-later, so it can be built together with AzerothCore under GPL-3.0.
