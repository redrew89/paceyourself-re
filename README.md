# Pace Yourself RE SKSE Rebuild 2.5.0

This is an SKSE rebuild of [Pace Yourself RE](https://www.nexusmods.com/skyrimspecialedition/mods/151365), which automatically switches the player between walking and running based on where they are and what they're doing.

## Features
* Walk in towns and cities, including walled-town worldspaces
* Optionally walk in unwalled towns, within a configurable distance of the town center
* Walk in peaceful interiors, and optionally in dungeons
* Preferred movement state in combat (do nothing, always run, or always walk)
* Manual override: use your run or Toggle Always Run key to take over, and the mod pauses until your choice matches its own again
* Visual feedback: a white shader on automatic switches, and a colored shader pair (red/green, orange/blue or yellow/purple) when you override or resume
* All settings in an MCM

Detection, decisions and feedback all run natively in the SKSE plugin. Location changes, combat and drawn weapons, and key presses are picked up immediately, rather than waiting on Papyrus polling or magic effects.

## Requirements
* [SKSE64](https://skse.silverlock.org/)
* [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
* [SkyUI](https://www.nexusmods.com/skyrimspecialedition/mods/12604) (for the MCM)
* Recommended: [FormList Manipulator](https://www.nexusmods.com/skyrimspecialedition/mods/74037), which reads the included `_FLM.ini` to add extra safe-location and dungeon keywords

Gamepad users: the mod disables itself while a gamepad is in use, unless Skyrim Motion Control is installed.

## Upgrading to 2.5.0
2.5.0 removes and renumbers records in `PaceYourself.esp`, so **it is not save compatible with earlier versions**. Before installing it on an existing save, uninstall the previous version and clean the save, or start a new game.

## How it works
| Layer | Responsibility |
|---|---|
| SKSE plugin (`src/main.cpp`) | Location, combat and key detection; the walk/run decision; applying it; cooldown and refresh scheduling; shader and message feedback |
| Papyrus (`data/source/scripts`) | The SkyUI MCM, initialization on game load, and sending settings to the plugin |
| `PaceYourself.esp` | The MCM quest, the effect shaders and messages, the active-state global, and the FormLists filled by FLM |

The plugin looks up the shaders and messages in `PaceYourself.esp` by form ID. If the ESP's records are renumbered, update the IDs in `FeedbackForms::Resolve()` to match.

### Roadmap
3.0.0 is reserved for a fully native release: the MCM replaced by SKSE Menu Framework, and no ESP.

## Building

### Requirements
* [XMake](https://xmake.io) [2.8.2+]
* C++23 Compiler (MSVC, Clang-CL)
* [Pyro](https://github.com/fireundubh/pyro) and the Skyrim SE Creation Kit script sources, to compile the Papyrus scripts

## Getting Started
```bat
git clone --recurse-submodules https://github.com/redrew89/paceyourself-re
cd paceyourself-re
```

### Build
To build the project, run the following command:
```bat
xmake build
```

> ***Note:*** *This will generate a `build/windows/` directory in the **project's root directory** with the build output.*

Test builds are made in `releasedbg` mode (`xmake f -m releasedbg`), which produces the DLL plus a PDB for crash logs.

### Papyrus Scripts
`skyrimse.ppj` compiles `data/source/scripts` into `data/scripts`:
```bat
pyro -i skyrimse.ppj
```

> ***Note:*** *The project imports the base game's script sources from a fixed Skyrim install path. Edit the second `<Import>` in `skyrimse.ppj` to point at your own `Data\Source\Scripts`.*

### Mod Files
`data/` holds the mod as it's installed: the ESP, the FLM and KID inis, the MCM image, the Papyrus sources and compiled scripts, and the built DLL and PDB under `SKSE/plugins`.

### Build Output (Optional)
If you want to redirect the build output, set one of or both of the following environment variables:

- Path to a Skyrim install folder: `XSE_TES5_GAME_PATH`

- Path to a Mod Manager mods folder: `XSE_TES5_MODS_PATH`

### Project Generation (Optional)
If you want to generate a Visual Studio project, run the following command:
```bat
xmake project -k vsxmake
```

> ***Note:*** *This will generate a `vsxmakeXXXX/` directory in the **project's root directory** using the latest version of Visual Studio installed on the system.*

### Upgrading Packages (Optional)
If you want to upgrade the project's dependencies, run the following commands:
```bat
xmake repo --update
xmake require --upgrade
```
