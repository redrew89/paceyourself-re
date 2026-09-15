#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <vector>
#include <unordered_set>
#include <string>
#include <format>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <atomic>
#include <Windows.h>
#include <cstdlib>

using namespace std::literals;

// Plugin version using CommonLibSSE-NG's version system
constexpr REL::Version PLUGIN_VERSION{ 2, 0, 0 };

// Function to get plugin version as string
std::string GetPluginVersionString()
{
    return std::format("{}.{}.{}",
        PLUGIN_VERSION.major(),
        PLUGIN_VERSION.minor(),
        PLUGIN_VERSION.patch());
}

// Alternative using string concatenation (C++17 compatible)
std::string GetPluginVersionStringCompat()
{
    return std::to_string(PLUGIN_VERSION.major()) + "." +
        std::to_string(PLUGIN_VERSION.minor()) + "." +
        std::to_string(PLUGIN_VERSION.patch());
}

// Get version with optional build number
std::string GetFullPluginVersionString()
{
    if (PLUGIN_VERSION.build() > 0) {
        return std::format("{}.{}.{}.{}",
            PLUGIN_VERSION.major(),
            PLUGIN_VERSION.minor(),
            PLUGIN_VERSION.patch(),
            PLUGIN_VERSION.build());
    }
    return GetPluginVersionString();
}

// Configuration structure to hold mod settings
struct MovementConfig {
    bool modActive = true;
    int combatRun = 0; // 0 = no change, 1 = run, 2 = walk
    bool walkInTowns = false;
    bool walkInTownsUnwalled = false;
    bool walkInDungeons = false;
    float maxDist = 6000.0f;
    bool detailLog = false;
};

// Global configuration instance (you'd load this from MCM or config file)
MovementConfig g_config;

// Cached form collections for performance
struct CachedForms {
    std::unordered_set<RE::FormID> interiorWorldspaces;
    std::unordered_set<RE::FormID> walledTownWorldspaces;
    std::unordered_set<RE::FormID> extraTownKeywords;
    std::unordered_set<RE::FormID> extraDunKeywords;

    RE::BGSKeyword* locTypeCity = nullptr;
    RE::BGSKeyword* locTypeTown = nullptr;
    RE::BGSKeyword* locTypeClearable = nullptr;

    RE::TESObjectREFR* locationMarker = nullptr;

    void InitializeKeywords() {
        auto dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            SKSE::log::warn("TESDataHandler not available yet, keywords will be initialized later");
            return;
        }

        // Try multiple methods to find keywords
        // Method 1: LookupByEditorID
        locTypeCity = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeCity");
        locTypeTown = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeTown");
        locTypeClearable = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeClearable");

        // Method 2: If EditorID lookup fails, try FormID lookup (Skyrim.esm)
        if (!locTypeCity) {
            locTypeCity = RE::TESForm::LookupByID<RE::BGSKeyword>(78184); // LocTypeCity
        }
        if (!locTypeTown) {
            locTypeTown = RE::TESForm::LookupByID<RE::BGSKeyword>(78182); // LocTypeTown  
        }
        if (!locTypeClearable) {
            locTypeClearable = RE::TESForm::LookupByID<RE::BGSKeyword>(1007232); // LocTypeClearable
        }

        // Method 3: Search through all keywords if still not found
        if (!locTypeCity || !locTypeTown || !locTypeClearable) {
            auto& keywords = dataHandler->GetFormArray<RE::BGSKeyword>();
            for (auto* keyword : keywords) {
                if (!keyword) continue;

                auto editorID = keyword->GetFormEditorID();
                if (!editorID) continue;

                std::string editorIDStr(editorID);
                if (!locTypeCity && editorIDStr == "LocTypeCity") {
                    locTypeCity = keyword;
                }
                else if (!locTypeTown && editorIDStr == "LocTypeTown") {
                    locTypeTown = keyword;
                }
                else if (!locTypeClearable && editorIDStr == "LocTypeClearable") {
                    locTypeClearable = keyword;
                }
            }
        }

        // Log results
        SKSE::log::info("=== Keyword Initialization Results ===");
        SKSE::log::info("LocTypeCity: {} ({:08X})",
            locTypeCity ? "Found" : "NOT FOUND",
            locTypeCity ? locTypeCity->GetFormID() : 0);
        SKSE::log::info("LocTypeTown: {} ({:08X})",
            locTypeTown ? "Found" : "NOT FOUND",
            locTypeTown ? locTypeTown->GetFormID() : 0);
        SKSE::log::info("LocTypeClearable: {} ({:08X})",
            locTypeClearable ? "Found" : "NOT FOUND",
            locTypeClearable ? locTypeClearable->GetFormID() : 0);
        SKSE::log::info("=== End Keyword Initialization ===");
    }
};

CachedForms g_forms;

// Override key state tracked natively
int g_overrideKeyCode = -1;
int g_runKeyCode = -1;
bool g_playerOverride = false;

// Input polling thread
static std::thread g_inputThread;
static std::atomic_bool g_inputThreadRunning{ false };

// Set native key codes (called from Papyrus during initialize)
void SetNativeOverrideKey(RE::StaticFunctionTag*, std::int32_t keyCode) {
    g_overrideKeyCode = keyCode;
    SKSE::log::info("SetNativeOverrideKey: {}", keyCode);
}

void SetNativeRunKey(RE::StaticFunctionTag*, std::int32_t keyCode) {
    g_runKeyCode = keyCode;
    SKSE::log::info("SetNativeRunKey: {}", keyCode);
}

// Handle override key press logic natively. Returns flags:
// bit0 = player manually overrode (should show disable shader/msg),
// bit1 = player choice aligns (should show enable shader/msg and reapply state),
// bit2 = request to reapply current player state via SetPlayerWalkRunState
int NativeMCM_HandleOverride(RE::StaticFunctionTag*, std::int32_t akKey) {
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return 0;

    bool isOverrideKey = (akKey == g_overrideKeyCode || akKey == g_runKeyCode);
    if (!isOverrideKey) return 0;

    // Determine current player movement mode and desired script mode
    auto playerControls = RE::PlayerControls::GetSingleton();
    if (!playerControls) return 0;

    bool currentPlayerMode = playerControls->data.running;
    bool setRun = ShouldRunHere(nullptr);

    bool isOverridden = (currentPlayerMode != setRun);
    g_playerOverride = isOverridden;

    int flags = 0;
    if (isOverridden) {
        flags |= 1; // player overrode
    } else {
        flags |= 2; // player choice aligns
        // When the override toggle key was used and player choice aligns, request reapply
        flags |= 4;
    }

    return flags;
}

bool GetNativePlayerOverride(RE::StaticFunctionTag*) {
    return g_playerOverride;
}

// Set native override flag from Papyrus (e.g., on keydown)
void SetNativePlayerOverride(RE::StaticFunctionTag*, bool state) {
    g_playerOverride = state;
}

// Poll keyboard state using Win32 - detects keydown/up transitions
void InputPollingLoop() {
    int prevOverrideState = 0;
    int prevRunState = 0;
    while (g_inputThreadRunning.load()) {
        if (g_overrideKeyCode != -1) {
            short state = GetAsyncKeyState(g_overrideKeyCode);
            int pressed = (state & 0x8000) != 0;
            if (pressed && !prevOverrideState) {
                // keydown - notify Papyrus to set override state on main thread
                if (auto papyrus = SKSE::GetPapyrusInterface()) {
                    papyrus->SendModEvent("PYS_NativeKey", "down", static_cast<float>(g_overrideKeyCode));
                }
            }
            if (!pressed && prevOverrideState) {
                // keyup
                // Send key-up mod event to Papyrus (main thread will handle game logic)
                if (auto papyrus = SKSE::GetPapyrusInterface()) {
                    papyrus->SendModEvent("PYS_NativeKey", "up", static_cast<float>(g_overrideKeyCode));
                }
            }
            prevOverrideState = pressed;
        }

        if (g_runKeyCode != -1) {
            short state = GetAsyncKeyState(g_runKeyCode);
            int pressed = (state & 0x8000) != 0;
            if (pressed && !prevRunState) {
                // run key down - notify Papyrus
                if (auto papyrus = SKSE::GetPapyrusInterface()) {
                    papyrus->SendModEvent("PYS_NativeKey", "down", static_cast<float>(g_runKeyCode));
                }
            }
            if (!pressed && prevRunState) {
                // run key up
                if (auto papyrus = SKSE::GetPapyrusInterface()) {
                    papyrus->SendModEvent("PYS_NativeKey", "up", static_cast<float>(g_runKeyCode));
                }
                // Request immediate movement evaluation via Papyrus/script if needed
            }
            prevRunState = pressed;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
}

// Gracefully stop the input polling thread
void StopInputThread() {
    if (!g_inputThreadRunning.load()) return;
    g_inputThreadRunning.store(false);
    if (g_inputThread.joinable()) {
        g_inputThread.join();
    }
    SKSE::log::info("Input polling thread stopped");
}

// Helper function to ensure keywords are initialized when needed
void EnsureKeywordsInitialized() {
    // Check if we already have the keywords
    if (g_forms.locTypeCity && g_forms.locTypeTown && g_forms.locTypeClearable) {
        return; // Already initialized
    }

    // Try to initialize if not done yet
    g_forms.InitializeKeywords();
}

// Helper function to check if location has specific keywords
bool CheckAdditionalKeywords(const std::unordered_set<RE::FormID>& keywords, RE::BGSLocation* location) {
    if (!location || keywords.empty()) return false;

    for (auto keywordID : keywords) {
        auto keyword = RE::TESForm::LookupByID<RE::BGSKeyword>(keywordID);
        if (keyword && location->HasKeyword(keyword)) {
            return true;
        }
    }
    return false;
}

// Helper function to check if player is in interior (including special worldspaces)
bool IsInInteriorActual(RE::TESObjectREFR* objectRef) {
    if (!objectRef) return false;

    auto parentCell = objectRef->GetParentCell();
    if (parentCell && parentCell->IsInteriorCell()) {
        return true;
    }

    auto worldspace = objectRef->GetWorldspace();
    if (worldspace && g_forms.interiorWorldspaces.count(worldspace->GetFormID())) {
        return true;
    }

    return false;
}

// Enhanced location debugging function
void DebugLocationKeywords(RE::BGSLocation* location) {
    if (!location) return;

    SKSE::log::info("=== Debugging Location Keywords ===");
    SKSE::log::info("Location: {} ({:08X})", location->GetName(), location->GetFormID());

    // Check our cached keywords
    if (g_forms.locTypeCity) {
        bool hasCity = location->HasKeyword(g_forms.locTypeCity);
        SKSE::log::info("Has LocTypeCity ({:08X}): {}", g_forms.locTypeCity->GetFormID(), hasCity);
    }

    if (g_forms.locTypeTown) {
        bool hasTown = location->HasKeyword(g_forms.locTypeTown);
        SKSE::log::info("Has LocTypeTown ({:08X}): {}", g_forms.locTypeTown->GetFormID(), hasTown);
    }

    // List all keywords on this location
    SKSE::log::info("All keywords on this location:");
    auto keywordArray = location->GetKeywords();
    if (!keywordArray.empty()) {
        for (std::uint32_t i = 0; i < keywordArray.size(); ++i) {
            auto keyword = keywordArray[i];
            if (keyword) {
                auto editorID = keyword->GetFormEditorID();
                SKSE::log::info("  Keyword {}: {} ({:08X}) - EditorID: {}",
                    i,
                    keyword->GetName() ? keyword->GetName() : "No Name",
                    keyword->GetFormID(),
                    editorID ? editorID : "No EditorID");
            }
        }
    }
    else {
        SKSE::log::info("  No keywords found on this location");
    }
    SKSE::log::info("=== End Location Keyword Debug ===");
}


// Main movement decision function - native implementation of ShouldRunHere()
// Add extra debug logging for combat state and config
bool ShouldRunHere(RE::StaticFunctionTag*) {
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return true;

    // Ensure keywords are initialized
    EnsureKeywordsInitialized();

    // Early exit conditions
    if (!g_config.modActive) {
        return true;
    }

    auto currentLoc = player->GetCurrentLocation();
    if (!currentLoc) {
        return true; // In wilderness - auto-run enabled
    }

    if (g_config.detailLog) {
        DebugLocationKeywords(currentLoc);
    }

    // Combat state check
    bool playerIsInCombat = player->IsInCombat() || player->AsActorState()->IsWeaponDrawn();
    bool changeCombatState = (g_config.combatRun != 0);

    if (g_config.detailLog) {
        SKSE::log::info("Combat state: {}, Weapon drawn: {}, playerIsInCombat: {}, combatRun: {}",
            player->IsInCombat(), player->AsActorState()->IsWeaponDrawn(), playerIsInCombat, g_config.combatRun);
    }

    if (playerIsInCombat) {
        if (!changeCombatState) {
            // Keep current movement state in combat
            auto playerControls = RE::PlayerControls::GetSingleton();
            if (g_config.detailLog) {
                SKSE::log::info("No combatRun override, returning current run state: {}", playerControls ? playerControls->data.running : true);
            }
            return playerControls ? playerControls->data.running : true;
        } else {
            // Combat movement override
            if (g_config.detailLog) {
                SKSE::log::info("combatRun override active, returning: {}", (g_config.combatRun == 1));
            }
            return (g_config.combatRun == 1);
        }
    }

    // Location analysis
    bool inInterior = IsInInteriorActual(player);

    // Enhanced town keyword checking
    bool hasTownKeywords = false;
    if (g_forms.locTypeCity && currentLoc->HasKeyword(g_forms.locTypeCity)) {
        hasTownKeywords = true;
        if (g_config.detailLog) {
            SKSE::log::info("Location has LocTypeCity keyword");
        }
    }
    if (g_forms.locTypeTown && currentLoc->HasKeyword(g_forms.locTypeTown)) {
        hasTownKeywords = true;
        if (g_config.detailLog) {
            SKSE::log::info("Location has LocTypeTown keyword");
        }
    }
    if (CheckAdditionalKeywords(g_forms.extraTownKeywords, currentLoc)) {
        hasTownKeywords = true;
        if (g_config.detailLog) {
            SKSE::log::info("Location has extra town keywords");
        }
    }

    // Town walking logic
    if (hasTownKeywords && g_config.walkInTowns) {
        SKSE::log::info("Town walking logic triggered - hasTownKeywords: {}, walkInTowns: {}",
            hasTownKeywords, g_config.walkInTowns);

        auto currentWorld = player->GetWorldspace();
        bool isWalledTown = currentWorld &&
            g_forms.walledTownWorldspaces.count(currentWorld->GetFormID());

        SKSE::log::info("  isWalledTown: {}, walkInTownsUnwalled: {}",
            isWalledTown, g_config.walkInTownsUnwalled);

        if (isWalledTown) {
            SKSE::log::info("  -> Walking in walled town");
            return false; // Walk in walled town
        }
        else if (g_config.walkInTownsUnwalled) {
            // Check for guild locations first (no marker needed)
            if (CheckAdditionalKeywords(g_forms.extraTownKeywords, currentLoc)) {
                SKSE::log::info("  -> Walking in guild/safe location");
                return false; // Walk in reasonably safe location
            }

            // Distance check for regular unwalled towns
            if (g_forms.locationMarker) {
                float distanceFromMarker = g_forms.locationMarker->GetDistance(player);
                SKSE::log::info("  -> Distance from marker: {} (max: {})",
                    distanceFromMarker, g_config.maxDist);
                if (distanceFromMarker <= g_config.maxDist) {
                    SKSE::log::info("  -> Walking in unwalled town (in range)");
                    return false; // Walk in unwalled town (in range)
                }
            }
            else {
                SKSE::log::info("  -> No location marker set, assuming in town");
                return false; // Walk in unwalled town (no marker check)
            }
        }
    }
    else {
        SKSE::log::info("Town walking logic skipped - hasTownKeywords: {}, walkInTowns: {}",
            hasTownKeywords, g_config.walkInTowns);
    }

    // Interior walking logic
    if (inInterior) {
        bool hasDunKeyword = false;
        if (g_forms.locTypeClearable && currentLoc->HasKeyword(g_forms.locTypeClearable)) {
            hasDunKeyword = true;
        }
        hasDunKeyword = hasDunKeyword || CheckAdditionalKeywords(g_forms.extraDunKeywords, currentLoc);

        if (!hasDunKeyword || (g_config.walkInDungeons && hasDunKeyword)) {
            return false; // Walk in peaceful interior or allowed dungeon
        }
    }

    // Default to running
    SKSE::log::info("Defaulting to running");
    return true;
}


// Enhanced function that combines decision logic with state setting
bool AutoSetPlayerMovement(RE::StaticFunctionTag*) {
    bool shouldRun = ShouldRunHere(nullptr);

    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return false;

    auto playerControls = RE::PlayerControls::GetSingleton();
    if (!playerControls) return false;

    // Get current walk-run state before potentially changing it
    bool currentState = playerControls->data.running;

    // Set the new walk-run state if different from current
    if (shouldRun != currentState) {
        playerControls->data.running = shouldRun;
    }

    return shouldRun;
}

// Configuration functions to be called from Papyrus
void SetMovementConfig(RE::StaticFunctionTag*, bool modActive, int combatRun,
    bool walkInTowns, bool walkInTownsUnwalled,
    bool walkInDungeons, float maxDist) {
    g_config.modActive = modActive;
    g_config.combatRun = combatRun;
    g_config.walkInTowns = walkInTowns;
    g_config.walkInTownsUnwalled = walkInTownsUnwalled;
    g_config.walkInDungeons = walkInDungeons;
    g_config.maxDist = maxDist;

    // Debug logging to verify settings are being applied
    SKSE::log::info("SetMovementConfig called:");
    SKSE::log::info("  modActive: {}", modActive);
    SKSE::log::info("  combatRun: {}", combatRun);
    SKSE::log::info("  walkInTowns: {}", walkInTowns);
    SKSE::log::info("  walkInTownsUnwalled: {}", walkInTownsUnwalled);
    SKSE::log::info("  walkInDungeons: {}", walkInDungeons);
    SKSE::log::info("  maxDist: {}", maxDist);
}

// Function to enable detailed debug logging
void SetDetailedLogging(RE::StaticFunctionTag*, bool enabled) {
    g_config.detailLog = enabled;
    SKSE::log::info("Detailed logging set to: {}", enabled);
}

// Function to force reinitialize keywords (for troubleshooting)
void ReinitializeKeywords(RE::StaticFunctionTag*) {
    SKSE::log::info("Force reinitializing keywords...");
    g_forms.InitializeKeywords();
}

// Function to set location marker reference
void SetLocationMarker(RE::StaticFunctionTag*, RE::TESObjectREFR* marker) {
    g_forms.locationMarker = marker;
}

// Functions to populate form collections (called during mod initialization)
void AddInteriorWorldspace(RE::StaticFunctionTag*, RE::TESWorldSpace* worldspace) {
    if (worldspace) {
        g_forms.interiorWorldspaces.insert(worldspace->GetFormID());
    }
}

void AddWalledTownWorldspace(RE::StaticFunctionTag*, RE::TESWorldSpace* worldspace) {
    if (worldspace) {
        g_forms.walledTownWorldspaces.insert(worldspace->GetFormID());
    }
}

void AddExtraTownKeyword(RE::StaticFunctionTag*, RE::BGSKeyword* keyword) {
    if (keyword) {
        g_forms.extraTownKeywords.insert(keyword->GetFormID());
    }
}

void AddExtraDunKeyword(RE::StaticFunctionTag*, RE::BGSKeyword* keyword) {
    if (keyword) {
        g_forms.extraDunKeywords.insert(keyword->GetFormID());
    }
}

// Debug functions to help troubleshoot MCM issues
void LogCurrentConfig(RE::StaticFunctionTag*) {
    SKSE::log::info("=== Current Movement Configuration ===");
    SKSE::log::info("modActive: {}", g_config.modActive);
    SKSE::log::info("combatRun: {}", g_config.combatRun);
    SKSE::log::info("walkInTowns: {}", g_config.walkInTowns);
    SKSE::log::info("walkInTownsUnwalled: {}", g_config.walkInTownsUnwalled);
    SKSE::log::info("walkInDungeons: {}", g_config.walkInDungeons);
    SKSE::log::info("maxDist: {}", g_config.maxDist);
    SKSE::log::info("detailLog: {}", g_config.detailLog);
    SKSE::log::info("=== End Configuration ===");
}

void LogCurrentLocation(RE::StaticFunctionTag*) {
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    // Ensure keywords are initialized
    EnsureKeywordsInitialized();

    auto currentLoc = player->GetCurrentLocation();
    if (currentLoc) {
        DebugLocationKeywords(currentLoc);

        auto worldspace = player->GetWorldspace();
        if (worldspace) {
            SKSE::log::info("Worldspace: {} ({:08X})", worldspace->GetName(), worldspace->GetFormID());
        }
    }
    else {
        SKSE::log::info("Player is in wilderness (no location)");
    }
}

bool GetCurrentConfig(RE::StaticFunctionTag*, int configType) {
    switch (configType) {
    case 0: return g_config.modActive;
    case 1: return g_config.walkInTowns;
    case 2: return g_config.walkInTownsUnwalled;
    case 3: return g_config.walkInDungeons;
    case 4: return (g_config.combatRun == 1);
    default: return false;
    }
}

// Native function to set player's walk-run state
// Returns the current walk-run state (true = running, false = walking)
bool SetPlayerWalkRunState(RE::StaticFunctionTag*, bool shouldRun) {
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return false;
    }
    auto playerControls = RE::PlayerControls::GetSingleton();
    if (!playerControls) {
        return false;
    }
    // Get current walk-run state before potentially changing it
    bool currentState = playerControls->data.running;
    // Set the new walk-run state if different from current
    if (shouldRun != currentState) {
        // Set the run state directly
        playerControls->data.running = shouldRun;
    }
    // Return the new state (true = running, false = walking)
    return shouldRun;
}

// Simple getter function for current walk-run state
bool GetPlayerWalkRunState(RE::StaticFunctionTag*) {
    auto playerControls = RE::PlayerControls::GetSingleton();
    if (!playerControls) {
        return true; // Default to running if we can't get controls
    }
    // Return current state (true = running, false = walking)
    return playerControls->data.running;
}

// Papyrus function to get plugin version
std::string GetPluginVersion(RE::StaticFunctionTag*) {
    return GetPluginVersionString();
}

// Register the native functions with Papyrus
bool RegisterPapyrusFunctions(RE::BSScript::IVirtualMachine* vm) {
    // Version function
    vm->RegisterFunction("GetPluginVersion", "PYS_UtilScript", GetPluginVersion);

    // Original functions
    vm->RegisterFunction("SetPlayerWalkRunState", "PYS_UtilScript", SetPlayerWalkRunState);
    vm->RegisterFunction("GetPlayerWalkRunState", "PYS_UtilScript", GetPlayerWalkRunState);

    // New movement logic functions
    vm->RegisterFunction("ShouldRunHere", "PYS_UtilScript", ShouldRunHere);
    vm->RegisterFunction("AutoSetPlayerMovement", "PYS_UtilScript", AutoSetPlayerMovement);

    // Configuration functions
    vm->RegisterFunction("SetMovementConfig", "PYS_UtilScript", SetMovementConfig);
    vm->RegisterFunction("SetDetailedLogging", "PYS_UtilScript", SetDetailedLogging);
    vm->RegisterFunction("ReinitializeKeywords", "PYS_UtilScript", ReinitializeKeywords);
    vm->RegisterFunction("SetLocationMarker", "PYS_UtilScript", SetLocationMarker);

    // Form collection functions
    vm->RegisterFunction("AddInteriorWorldspace", "PYS_UtilScript", AddInteriorWorldspace);
    vm->RegisterFunction("AddWalledTownWorldspace", "PYS_UtilScript", AddWalledTownWorldspace);
    vm->RegisterFunction("AddExtraTownKeyword", "PYS_UtilScript", AddExtraTownKeyword);
    vm->RegisterFunction("AddExtraDunKeyword", "PYS_UtilScript", AddExtraDunKeyword);

    // Debug functions
    vm->RegisterFunction("LogCurrentConfig", "PYS_UtilScript", LogCurrentConfig);
    vm->RegisterFunction("LogCurrentLocation", "PYS_UtilScript", LogCurrentLocation);
    vm->RegisterFunction("GetCurrentConfig", "PYS_UtilScript", GetCurrentConfig);

    // New location-tracking helpers
    vm->RegisterFunction("GetPlayerLocationInfo", "PYS_UtilScript", GetPlayerLocationInfo);
    vm->RegisterFunction("GetPlayerLocationName", "PYS_UtilScript", GetPlayerLocationName);
        // Initialization helper to populate native caches from Papyrus FormLists
        vm->RegisterFunction("InitializeNativeSystem", "PYS_UtilScript", InitializeNativeSystem);
        // Debug / scaffold: dump native cached state to log (useful until serialization is wired)
        vm->RegisterFunction("DumpNativeState", "PYS_UtilScript", DumpNativeState);

    // Initialization helper to populate native caches from Papyrus FormLists
    vm->RegisterFunction("InitializeNativeSystem", "PYS_UtilScript", InitializeNativeSystem);
    // Debug / scaffold: dump native cached state to log (useful until serialization is wired)
    vm->RegisterFunction("DumpNativeState", "PYS_UtilScript", DumpNativeState);
    vm->RegisterFunction("SaveNativeCache", "PYS_UtilScript", SaveNativeCache);
    vm->RegisterFunction("LoadNativeCache", "PYS_UtilScript", LoadNativeCache);
    // MCM migration helpers
    vm->RegisterFunction("NativeMCM_Initialize", "PYS_UtilScript", NativeMCM_Initialize);
    vm->RegisterFunction("NativeMCM_SetRunState", "PYS_UtilScript", NativeMCM_SetRunState);
    vm->RegisterFunction("SetNativeOverrideKey", "PYS_UtilScript", SetNativeOverrideKey);
    vm->RegisterFunction("SetNativeRunKey", "PYS_UtilScript", SetNativeRunKey);
    vm->RegisterFunction("NativeMCM_HandleOverride", "PYS_UtilScript", NativeMCM_HandleOverride);
    vm->RegisterFunction("GetNativePlayerOverride", "PYS_UtilScript", GetNativePlayerOverride);
    vm->RegisterFunction("SetNativePlayerOverride", "PYS_UtilScript", SetNativePlayerOverride);

    return true;
}

// SKSE plugin load function
SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);

    // Log the plugin version
    auto version = GetPluginVersionString();
    SKSE::log::info("Plugin version: {}", version);

    // Don't initialize keywords here - do it lazily when needed
    // This avoids issues with TESDataHandler not being ready yet
    SKSE::log::info("Plugin loaded, keywords will be initialized on first use");

    // Get Papyrus interface and register functions
    SKSE::GetPapyrusInterface()->Register(RegisterPapyrusFunctions);

    // Load persisted native cache from disk (best-effort)
    LoadNativeCacheFromDisk();

    // Start input polling thread for native-only key handling
    g_inputThreadRunning.store(true);
    g_inputThread = std::thread([]() { InputPollingLoop(); });
    // Ensure graceful shutdown on process exit
    std::atexit(StopInputThread);

    // Register SKSE serialization callbacks for co-save persistence
    if (auto serialization = SKSE::GetSerializationInterface()) {
        auto pluginHandle = SKSE::GetPluginHandle();
        serialization->SetUniqueID(pluginHandle, 'PYSR');
        serialization->SetSaveCallback(pluginHandle, [](auto ser) {
            SKSE::log::info("Serialization SaveCallback invoked");
            // Open a single record for our data
            ser->OpenRecord('PYSR', 1);

            // Write counts and formids for each set
            auto writeSet = [&](const std::unordered_set<RE::FormID>& s) {
                std::uint32_t count = static_cast<std::uint32_t>(s.size());
                ser->WriteRecordData(&count, sizeof(count));
                for (auto id : s) ser->WriteRecordData(&id, sizeof(id));
            };

            writeSet(g_forms.interiorWorldspaces);
            writeSet(g_forms.walledTownWorldspaces);
            writeSet(g_forms.extraTownKeywords);
            writeSet(g_forms.extraDunKeywords);

            // Marker ref
            std::uint32_t markerID = g_forms.locationMarker ? g_forms.locationMarker->GetFormID() : 0;
            ser->WriteRecordData(&markerID, sizeof(markerID));
        });

        serialization->SetLoadCallback(pluginHandle, [](auto ser) {
            SKSE::log::info("Serialization LoadCallback invoked");
            std::uint32_t type = 0, version = 0, length = 0;
            while (ser->GetNextRecordInfo(&type, &version, &length)) {
                if (type != 'PYSR') {
                    // skip unknown records
                    ser->ReadRecordData(nullptr, length);
                    continue;
                }

                // Read four sets
                auto readSet = [&](std::unordered_set<RE::FormID>& outSet) {
                    std::uint32_t count = 0;
                    ser->ReadRecordData(&count, sizeof(count));
                    for (std::uint32_t i = 0; i < count; ++i) {
                        std::uint32_t id = 0;
                        ser->ReadRecordData(&id, sizeof(id));
                        if (id) outSet.insert(id);
                    }
                };

                g_forms.interiorWorldspaces.clear();
                g_forms.walledTownWorldspaces.clear();
                g_forms.extraTownKeywords.clear();
                g_forms.extraDunKeywords.clear();

                readSet(g_forms.interiorWorldspaces);
                readSet(g_forms.walledTownWorldspaces);
                readSet(g_forms.extraTownKeywords);
                readSet(g_forms.extraDunKeywords);

                // Marker
                std::uint32_t markerID = 0;
                ser->ReadRecordData(&markerID, sizeof(markerID));
                if (markerID) {
                    auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(markerID);
                    if (ref) g_forms.locationMarker = ref;
                }
            }
        });

        serialization->SetRevertCallback(pluginHandle, [](auto) {
            SKSE::log::info("Serialization RevertCallback invoked");
            g_forms.interiorWorldspaces.clear();
            g_forms.walledTownWorldspaces.clear();
            g_forms.extraTownKeywords.clear();
            g_forms.extraDunKeywords.clear();
            g_forms.locationMarker = nullptr;
        });
    }

    return true;
}

// Bitmask flags returned by GetPlayerLocationInfo
enum LocationFlags : int {
    LF_None = 0,
    LF_InInterior = 1 << 0,
    LF_LocTypeCity = 1 << 1,
    LF_LocTypeTown = 1 << 2,
    LF_LocTypeClearable = 1 << 3,
    LF_ExtraTown = 1 << 4,
    LF_ExtraDun = 1 << 5,
    LF_WalledTown = 1 << 6,
    LF_WithinMarker = 1 << 7,
    LF_NoLocation = 1 << 8
};

// Returns bitflags describing the player's current location state.
int GetPlayerLocationInfo(RE::StaticFunctionTag*) {
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return LF_None;

    EnsureKeywordsInitialized();

    auto currentLoc = player->GetCurrentLocation();
    if (!currentLoc) {
        return LF_NoLocation;
    }

    int flags = LF_None;

    if (IsInInteriorActual(player)) flags |= LF_InInterior;

    if (g_forms.locTypeCity && currentLoc->HasKeyword(g_forms.locTypeCity)) flags |= LF_LocTypeCity;
    if (g_forms.locTypeTown && currentLoc->HasKeyword(g_forms.locTypeTown)) flags |= LF_LocTypeTown;
    if (g_forms.locTypeClearable && currentLoc->HasKeyword(g_forms.locTypeClearable)) flags |= LF_LocTypeClearable;

    if (CheckAdditionalKeywords(g_forms.extraTownKeywords, currentLoc)) flags |= LF_ExtraTown;
    if (CheckAdditionalKeywords(g_forms.extraDunKeywords, currentLoc)) flags |= LF_ExtraDun;

    auto currentWorld = player->GetWorldspace();
    if (currentWorld && g_forms.walledTownWorldspaces.count(currentWorld->GetFormID())) flags |= LF_WalledTown;

    if (g_forms.locationMarker) {
        float distanceFromMarker = g_forms.locationMarker->GetDistance(player);
        if (distanceFromMarker <= g_config.maxDist) flags |= LF_WithinMarker;
    }

    return flags;
}

// Returns the current location name or an empty string if none (wilderness)
std::string GetPlayerLocationName(RE::StaticFunctionTag*) {
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return std::string();

    auto currentLoc = player->GetCurrentLocation();
    if (!currentLoc) return std::string();

    auto name = currentLoc->GetName();
    return name ? std::string(name) : std::string();
}

// Initialization helper to populate native caches from Papyrus FormLists
void InitializeNativeSystem(RE::StaticFunctionTag*, RE::TESGlobal* PYS_Active, int combatRunSetting, bool walkInTowns, bool walkInTownsUnwalled, bool walkInDungeons, float maxDistance, RE::BGSFormList* interiorWorldspaces = nullptr, RE::BGSFormList* walledTownWorldspaces = nullptr, RE::BGSFormList* extraTownKeywords = nullptr, RE::BGSFormList* extraDunKeywords = nullptr) {
    bool modActive = true;
    if (PYS_Active) {
        // TESGlobal stores a float value; treat non-zero as true
        modActive = (PYS_Active->GetValue() != 0.0f);
    }

    SetMovementConfig(nullptr, modActive, combatRunSetting, walkInTowns, walkInTownsUnwalled, walkInDungeons, maxDistance);

    // Populate interior worldspaces
    if (interiorWorldspaces) {
        std::uint32_t count = interiorWorldspaces->GetSize();
        for (std::uint32_t i = 0; i < count; ++i) {
            auto form = interiorWorldspaces->GetAt(i);
            if (!form) continue;
            auto ws = form->As<RE::TESWorldSpace>();
            if (ws) AddInteriorWorldspace(nullptr, ws);
        }
    }

    // Populate walled town worldspaces
    if (walledTownWorldspaces) {
        std::uint32_t count = walledTownWorldspaces->GetSize();
        for (std::uint32_t i = 0; i < count; ++i) {
            auto form = walledTownWorldspaces->GetAt(i);
            if (!form) continue;
            auto ws = form->As<RE::TESWorldSpace>();
            if (ws) AddWalledTownWorldspace(nullptr, ws);
        }
    }

    // Populate extra town keywords
    if (extraTownKeywords) {
        std::uint32_t count = extraTownKeywords->GetSize();
        for (std::uint32_t i = 0; i < count; ++i) {
            auto form = extraTownKeywords->GetAt(i);
            if (!form) continue;
            auto kw = form->As<RE::BGSKeyword>();
            if (kw) AddExtraTownKeyword(nullptr, kw);
        }
    }

    // Populate extra dungeon keywords
    if (extraDunKeywords) {
        std::uint32_t count = extraDunKeywords->GetSize();
        for (std::uint32_t i = 0; i < count; ++i) {
            auto form = extraDunKeywords->GetAt(i);
            if (!form) continue;
            auto kw = form->As<RE::BGSKeyword>();
            if (kw) AddExtraDunKeyword(nullptr, kw);
        }
    }

    SKSE::log::info("InitializeNativeSystem: populated native caches (interior/walled/extra keywords)");
}

// Debug helper to dump current native cached state to log (placeholder for serialization)
void DumpNativeState(RE::StaticFunctionTag*) {
    SKSE::log::info("--- Dumping native cached state ---");

    SKSE::log::info("Interior worldspaces: {} entries", g_forms.interiorWorldspaces.size());
    for (auto id : g_forms.interiorWorldspaces) {
        SKSE::log::info("  {:08X}", id);
    }

    SKSE::log::info("Walled town worldspaces: {} entries", g_forms.walledTownWorldspaces.size());
    for (auto id : g_forms.walledTownWorldspaces) {
        SKSE::log::info("  {:08X}", id);
    }

    SKSE::log::info("Extra town keywords: {} entries", g_forms.extraTownKeywords.size());
    for (auto id : g_forms.extraTownKeywords) {
        SKSE::log::info("  {:08X}", id);
    }

    SKSE::log::info("Extra dungeon keywords: {} entries", g_forms.extraDunKeywords.size());
    for (auto id : g_forms.extraDunKeywords) {
        SKSE::log::info("  {:08X}", id);
    }

    if (g_forms.locTypeCity) SKSE::log::info("LocTypeCity cached: {:08X}", g_forms.locTypeCity->GetFormID());
    if (g_forms.locTypeTown) SKSE::log::info("LocTypeTown cached: {:08X}", g_forms.locTypeTown->GetFormID());
    if (g_forms.locTypeClearable) SKSE::log::info("LocTypeClearable cached: {:08X}", g_forms.locTypeClearable->GetFormID());

    if (g_forms.locationMarker) SKSE::log::info("Location marker ref cached: {:08X}", g_forms.locationMarker->GetFormID());

    SKSE::log::info("--- End dump ---");
}

// Native helper to be called from MCM initialize. Populates native caches and runs quick tests.
void NativeMCM_Initialize(RE::StaticFunctionTag*, RE::Actor* PlayerRef, RE::TESGlobal* PYS_Active, int combatRunSetting, bool walkInTowns, bool walkInTownsUnwalled, bool walkInDungeons, float maxDistance, RE::BGSFormList* interiorWorldspaces = nullptr, RE::BGSFormList* walledTownWorldspaces = nullptr, RE::BGSFormList* extraTownKeywords = nullptr, RE::BGSFormList* extraDunKeywords = nullptr) {
    SKSE::log::info("NativeMCM_Initialize called");
    // Populate config and caches
    InitializeNativeSystem(nullptr, PYS_Active, combatRunSetting, walkInTowns, walkInTownsUnwalled, walkInDungeons, maxDistance, interiorWorldspaces, walledTownWorldspaces, extraTownKeywords, extraDunKeywords);

    // Optionally set player reference marker or other initial state
    if (PlayerRef) {
        g_forms.locationMarker = nullptr; // safe default; actual marker set via SetLocationMarker
    }

    // Run native tests once for debugging
    TestNativeFunctions();
}

// Simple native wrapper invoked by MCM SetRunState to perform decision + application of run/walk state.
// Returns the applied run state (true = running, false = walking)
// Action flags returned to Papyrus
static constexpr std::uint32_t NMS_NONE = 0;
static constexpr std::uint32_t NMS_CHANGED_TO_RUN = 1 << 0;
static constexpr std::uint32_t NMS_CHANGED_TO_WALK = 1 << 1;
static constexpr std::uint32_t NMS_COOLDOWN = 1 << 2;

int NativeMCM_SetRunState(RE::StaticFunctionTag*, RE::Actor* akActor, bool playerOverride, bool inputRunPressed, float timeout) {
    if (!akActor) return NMS_NONE;
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return NMS_NONE;

    if (akActor != player) return NMS_NONE;

    // If player override or input pressed, let Papyrus handle scheduling and skip
    if (playerOverride || inputRunPressed) {
        return NMS_NONE;
    }

    // If sneaking, skip changes
    if (player->IsSneaking()) {
        return NMS_NONE;
    }

    // Cooldown check (use steady_clock)
    static auto lastToggle = std::chrono::steady_clock::time_point::min();
    auto now = std::chrono::steady_clock::now();
    float cooldownPeriod = timeout / 4.0f;
    if (lastToggle != std::chrono::steady_clock::time_point::min()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::duration<float>>(now - lastToggle).count();
        if (elapsed < cooldownPeriod) {
            return NMS_COOLDOWN;
        }
    }

    // Record previous state
    auto playerControls = RE::PlayerControls::GetSingleton();
    if (!playerControls) return NMS_NONE;
    bool prevState = playerControls->data.running;

    // Make decision and apply
    bool newState = AutoSetPlayerMovement(nullptr);

    if (newState != prevState) {
        lastToggle = now;
        return newState ? NMS_CHANGED_TO_RUN : NMS_CHANGED_TO_WALK;
    }

    return NMS_NONE;
}

static constexpr const char* kNativeCacheFile = "Data/PaceYourself_NativeCache.txt";

// Simple disk-based persistence (fallback to SKSE save integration later)
void SaveNativeCacheToDisk() {
    std::ofstream out(kNativeCacheFile, std::ios::trunc);
    if (!out) {
        SKSE::log::warn("SaveNativeCacheToDisk: failed to open {}", kNativeCacheFile);
        return;
    }

    auto writeSet = [&](const char* label, const std::unordered_set<RE::FormID>& s) {
        out << label << '\n';
        for (auto id : s) out << std::hex << std::setw(8) << std::setfill('0') << id << '\n';
    };

    writeSet("INTERIORS", g_forms.interiorWorldspaces);
    writeSet("WALLED", g_forms.walledTownWorldspaces);
    writeSet("EXTRA_TOWN_KW", g_forms.extraTownKeywords);
    writeSet("EXTRA_DUN_KW", g_forms.extraDunKeywords);

    if (g_forms.locationMarker) {
        out << "MARKER\n" << std::hex << std::setw(8) << std::setfill('0') << g_forms.locationMarker->GetFormID() << '\n';
    }

    out.close();
    SKSE::log::info("Saved native cache to {}", kNativeCacheFile);
}

void LoadNativeCacheFromDisk() {
    std::ifstream in(kNativeCacheFile);
    if (!in) {
        SKSE::log::info("No native cache file found: {}", kNativeCacheFile);
        return;
    }

    std::string line;
    std::string section;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        if (line == "INTERIORS" || line == "WALLED" || line == "EXTRA_TOWN_KW" || line == "EXTRA_DUN_KW" || line == "MARKER") {
            section = line;
            continue;
        }

        // Parse hex formid
        std::uint32_t id = 0;
        std::stringstream ss;
        ss << std::hex << line;
        ss >> id;
        if (id == 0) continue;

        if (section == "INTERIORS") g_forms.interiorWorldspaces.insert(id);
        else if (section == "WALLED") g_forms.walledTownWorldspaces.insert(id);
        else if (section == "EXTRA_TOWN_KW") g_forms.extraTownKeywords.insert(id);
        else if (section == "EXTRA_DUN_KW") g_forms.extraDunKeywords.insert(id);
        else if (section == "MARKER") {
            auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(id);
            if (ref) g_forms.locationMarker = ref;
        }
    }

    in.close();
    SKSE::log::info("Loaded native cache from {}", kNativeCacheFile);
}

// Papyrus-exposed save/load for debugging
void SaveNativeCache(RE::StaticFunctionTag*) { SaveNativeCacheToDisk(); }
void LoadNativeCache(RE::StaticFunctionTag*) { LoadNativeCacheFromDisk(); }