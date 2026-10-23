# Third-party notices

WoWUnreal (GPL-3.0, see `LICENSE`) includes or uses the following third-party software and data. Each keeps its own licence.

## Included in the source and in the pre-built client

### Lua 5.1.5
Source: `Source/ThirdParty/lua`. https://www.lua.org

```
Copyright (C) 1994-2012 Lua.org, PUC-Rio.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

### StormLib
Source: `Source/ThirdParty/StormLib`. https://github.com/ladislav-zezula/StormLib

```
Copyright (c) 1999-2013 Ladislav Zezula

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

### pugixml
Source: `Source/ThirdParty/pugixml`. https://pugixml.org

```
Copyright (c) 2006-2026 Arseny Kapoulkine

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

## Used by the client, not part of this repository

- **Unreal® Engine** by Epic Games, Inc., under the Unreal Engine End User License Agreement. Unreal® is a trademark or registered trademark of Epic Games, Inc. in the United States of America and elsewhere. The pre-built client contains the Unreal Engine runtime.
- **NVIDIA DLSS, Streamline and Reflex** plugins, under NVIDIA's licence terms for those SDKs. NVIDIA, DLSS and Reflex are trademarks of NVIDIA Corporation. The pre-built client contains their runtime libraries.
- **AMD FidelityFX Super Resolution (FSR)** and **AMD Anti-Lag 2** plugins by Advanced Micro Devices, Inc., under their own licences (FidelityFX: MIT). AMD and FidelityFX are trademarks of Advanced Micro Devices, Inc.

## Data and services

- **Open-Meteo** (https://open-meteo.com). With real weather switched on, the client fetches weather data from Open-Meteo. Weather data by Open-Meteo.com, licensed under CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/).
- **AzerothCore** (https://www.azerothcore.org). The offline mode's table of dungeon and raid portal destinations (`Source/WowUnreal/WowAreaTriggerTeleports.inl`) was generated from AzerothCore's `areatrigger_teleport` data, GPL-2.0-or-later.

## World of Warcraft

World of Warcraft and Blizzard Entertainment are trademarks or registered trademarks of Blizzard Entertainment, Inc. WoWUnreal contains no Blizzard game data, art, audio or interface code; it reads the game data at runtime from your own legal copy of a 3.3.5a `Data` folder (MPQs).
