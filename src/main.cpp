#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <vector>
#include <unordered_set>
#include <string>
#include <format>

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

    return true;
}