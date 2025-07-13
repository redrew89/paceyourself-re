Pace Yourself RE by redrew89

Description:

This mod is a rework and enhancement of Player walks Indoors SE by TemplarSwift. In its most basic functionality, this will automatically toggle the Auto Run state based on player location. By default, it will only set the player to walking in interiors that are not normally considered hostile (based on keyword checks), while the player is not in combat. The MCM allows for additional options, including toggles for walking in towns or dungeons, as well as a speed modifier that only takes effect when walking. Other technical options are also included, for players to adjust as needed in cases of different script loads and gameplay states. Additionally, there is now the option of feedback for the system, including both text notifications and Effect Shaders, to indicate different states and functions. Settings options for towns and unwalled towns have been reworked, to also take into account the player's distance from a given town's center marker, as defined in the game data. 

Now reworked into an SKSE DLL plugin for core behavior, improving responsiveness in most scenarios. 


Additional Technical Details:

There are several FormLists included that can be manipulated externally for additional control, ideally through FormList Manipulator, but other plugins can override them or manipulate them via scripting as well. These FormLists allow creators and players to define additional Keywords to be checked against the current player Location, both for dungeon and town keywords. 


Requirements:

SKSE
Address Library

Credits:

Player walks Indoors SE by TemplarSwift


Copyright (c) 2025, redrew89 - "Simplified BSD License"
Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


Changelog:

v2.0-Test

Partial refactor of core logic and state switching into SKSE DLL, built with CommonLibSSE-NG
Added PYS_UtilScript to define native functions and other utility functions
Removed depreciated code from PYS_MCMScript, added import for PYS_UtilScript, updated function calls

v1.3 

Fixes to player load game event to improve script stability
Fixes to main initialization routine to fix race condition issues

v1.2

Complete rework of scripts and functionality - NEW SAVE REQUIRED
Corrected critical error in ESP-FE; FormIDs have been renumbered and compacted to keep them in range for ESP-FE format
Added central logging to custom logfile, when Papyrus logging is enabled, option for detailed logging in MCM
Reworked script logic to be more performance-friendly and responsive
Expanded MCM with new features and settings options
Implemented manual override features, including manual override key option
Added Effect Shader feedback for various system functions, including accessibility options for different color perception states
Added Message Notification feedback for various system functions, with option to disable
Added Magic Effect mechanism to detect player weapon state and combat state
Added Quest Alias mechanism to detect location center markers for range calculations
Added Gamepad detection - when gamepad is detected, system will shut down until next game launch without gamepad
Pre-filled Keyword Form Lists, may revise in the future, or replace with a template FLM file that players can modify/add to as they please



