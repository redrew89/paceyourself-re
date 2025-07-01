# Pace Yourself RE SKSE Rebuild 2.0.0

A reimagined and expanded rework of Player Walks Indoors SE by TemplarSwift
by redrew89
📖 Description

Pace Yourself RE is a lightweight, immersive quality-of-life mod that automatically toggles the player's auto-run state based on location, combat status, and customizable settings. It improves immersion by encouraging more grounded movement—so you’re no longer charging through every tavern like a mammoth on skooma.

Originally implemented entirely in Papyrus, core functionality has now been ported to a native SKSE DLL plugin (built with CommonLibSSE-NG) to provide better responsiveness, efficiency, and reliability.

Includes robust Mod Configuration Menu (MCM) support, various visual feedback options, and extended compatibility hooks for modders.
⚙️ Features

    Automatically switch between walk/run based on:
        Interior/exterior location
        Dungeon/town keywords
        Player combat state
        Distance from location center markers (for towns)
    Player override detection when using the auto-run toggle
    Feedback options:
        Text notifications
        Effect shaders (with accessibility options)
    Optional logging to a custom logfile when Papyrus logging is enabled
    Exposed FormLists for keyword customization (dungeons/towns)
    Now using native C++ SKSE plugin for core state switching

🧰 Requirements

    SKSE64
    Address Library for SKSE Plugins

📦 Installation

Install with a mod manager (MO2 or Vortex recommended). Load order is flexible. Configure settings in the MCM menu in-game.
📓 Changelog
v2.0 (SKSE DLL Upgrade)

    Refactored core logic into an SKSE plugin using CommonLibSSE-NG
    Implemented PYS_UtilScript with native SetPlayerWalkRunState() and GetPlayerWalkRunState() functions
    Updated MCM script and removed deprecated logic
    Remove player movement speed adjustment. Use Skyrim Motion Control

v1.3

    Improved player load event stability
    Fixed race conditions in initialization

v1.2

    Full script rework (NEW SAVE REQUIRED)
    Keyword-based behavior detection
    Custom log file and detailed debug options
    Expanded MCM settings
    Manual override support
    Shader and message feedback for key events
    Gamepad-aware toggle (disables auto-toggling if a controller is detected)

🤝 Credits

    Original concept: TemplarSwift — Player Walks Indoors SE
    Rework and plugin development: redrew89

🪪 License

Simplified BSD License

This software is provided "as is", without warranty of any kind. See included LICENSE file for details.

This is an SKSE rebuild of the [Pace Yourself RE](https://www.nexusmods.com/skyrimspecialedition/mods/151365) 

### Requirements
* [XMake](https://xmake.io) [2.8.2+]
* C++23 Compiler (MSVC, Clang-CL)

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

