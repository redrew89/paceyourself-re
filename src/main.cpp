#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <unordered_set>
#include <string>
#include <format>
#include <chrono>
#include <thread>
#include <atomic>
#define NOMINMAX
#include <Windows.h>
#include <cstdlib>

using namespace std::literals;

// Plugin version using CommonLibSSE-NG's version system
constexpr REL::Version PLUGIN_VERSION{ 2, 5, 0 };

// Function to get plugin version as string
std::string GetPluginVersionString()
{
    return std::format("{}.{}.{}",
        PLUGIN_VERSION.major(),
        PLUGIN_VERSION.minor(),
        PLUGIN_VERSION.patch());
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

// Forward declarations (definitions appear later in this file; these are
// referenced earlier by NativeMCM_HandleOverride, RegisterPapyrusFunctions,
// and SKSEPluginLoad).
bool ShouldRunHere(RE::StaticFunctionTag*);
void NativeMCM_Initialize(RE::StaticFunctionTag*, RE::Actor* PlayerRef, RE::TESGlobal* PYS_Active, int combatRunSetting, bool walkInTowns, bool walkInTownsUnwalled, bool walkInDungeons, float maxDistance, RE::BGSListForm* interiorWorldspaces, RE::BGSListForm* walledTownWorldspaces, RE::BGSListForm* extraTownKeywords, RE::BGSListForm* extraDunKeywords);
void CheckLocationTransition();
void CheckCombatTransition();
void EvaluateRunState(const char* reason);
void HandleNativeKey(int keyCode, bool down);
void SetFeedbackConfig(RE::StaticFunctionTag*, int shaderFX, bool msgVerbose, float refreshTime, float timeout, bool detailLog);
void RequestRunStateEvaluation(RE::StaticFunctionTag*);

// Override key state tracked natively. Written from Papyrus / the main thread and
// read by the input polling thread, hence atomic.
std::atomic<int> g_overrideKeyCode{ -1 };
std::atomic<int> g_runKeyCode{ -1 };
std::atomic_bool g_playerOverride{ false };
std::atomic_bool g_runKeyHeld{ false };

// Reaction pipeline state. The pipeline only runs once Papyrus has initialized
// it for the current save (NativeMCM_Initialize), and is reset on revert.
std::atomic_bool g_pipelineActive{ false };

// Next scheduled SetRunState evaluation, as a steady_clock tick count (0 = none).
// Native equivalent of the MCM's RegisterForSingleUpdate: scheduling replaces any
// pending evaluation. The input polling thread fires it via the task interface.
std::atomic<std::chrono::steady_clock::rep> g_nextEvaluation{ 0 };

// Time of the last automatic walk/run toggle, for the cooldown. Reset on revert.
auto g_lastToggle = std::chrono::steady_clock::time_point::min();

void ScheduleEvaluation(float seconds) {
    auto due = std::chrono::steady_clock::now() +
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<float>(seconds));
    g_nextEvaluation.store(due.time_since_epoch().count());
}

// Input polling thread
static std::thread g_inputThread;
static std::atomic_bool g_inputThreadRunning{ false };

// Papyrus's Input.GetMappedKey() returns DirectInput scan codes (DIK_*),
// but GetAsyncKeyState() expects Win32 virtual-key codes (VK_*). Convert so
// the polling loop actually tests the key the user bound.
std::atomic<int> g_overrideKeyVK{ -1 };
std::atomic<int> g_runKeyVK{ -1 };

int DIKToVK(int dikCode) {
    if (dikCode < 0 || dikCode > 0xFF) {
        return -1; // mouse/gamepad codes aren't keyboard scan codes; unsupported by GetAsyncKeyState
    }

    // Some keys map differently depending on whether the extended-scancode path is used.
    // Fall back to the standard conversion if the EX form does not resolve a value.
    UINT vk = MapVirtualKeyW(static_cast<UINT>(dikCode), MAPVK_VSC_TO_VK_EX);
    if (vk == 0) {
        vk = MapVirtualKeyW(static_cast<UINT>(dikCode), MAPVK_VSC_TO_VK);
    }

    // Final safety net: if a key still resolves to 0, keep the original DIK code
    // only as a last resort for debugging; otherwise the polling loop would be blind.
    if (vk == 0) {
        SKSE::log::info("DIKToVK fallback: DIK {} did not resolve to a valid VK; leaving as-is", dikCode);
        return dikCode;
    }

    return static_cast<int>(vk);
}

// Set native key codes (called from Papyrus during initialize)
void SetNativeOverrideKey(RE::StaticFunctionTag*, std::int32_t keyCode) {
    int vk = DIKToVK(keyCode);
    g_overrideKeyCode = keyCode;
    g_overrideKeyVK = vk;
    SKSE::log::info("SetNativeOverrideKey: {} (VK {})", keyCode, vk);
}

void SetNativeRunKey(RE::StaticFunctionTag*, std::int32_t keyCode) {
    int vk = DIKToVK(keyCode);
    g_runKeyCode = keyCode;
    g_runKeyVK = vk;
    SKSE::log::info("SetNativeRunKey: {} (VK {})", keyCode, vk);
}

// Handle override key press logic natively. Returns flags:
// bit0 = player manually overrode (should show disable shader/msg),
// bit1 = player choice aligns (should show enable shader/msg)
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
    }

    return flags;
}

// Poll keyboard state using Win32 - detects keydown/up transitions
void InputPollingLoop() {
    int prevOverrideState = 0;
    int prevRunState = 0;
    int interiorCheckCounter = 0;
    while (g_inputThreadRunning.load()) {
        // Piggyback on this thread's cadence to catch location and combat/weapon-
        // drawn transitions quickly instead of waiting on Papyrus's condition-based
        // magic effects (which are throttled by the engine and collide with
        // cell-load hitches) or the ~30s SetRunState fallback poll.
        // Checked every ~200ms rather than every tick to keep the main-thread task
        // queue light; actual game-state reads happen inside the Check* functions
        // on the main thread via the task interface, never here.
        if (++interiorCheckCounter >= 5) {
            interiorCheckCounter = 0;
            if (auto task = SKSE::GetTaskInterface()) {
                task->AddTask([]() {
                    CheckLocationTransition();
                    CheckCombatTransition();
                });
            }
        }

        // Fire the scheduled SetRunState evaluation once it's due. The exchange
        // guarantees a given schedule is only fired once even if it's replaced
        // concurrently.
        auto due = g_nextEvaluation.load();
        if (due != 0 && std::chrono::steady_clock::now().time_since_epoch().count() >= due &&
            g_nextEvaluation.compare_exchange_strong(due, 0)) {
            if (auto task = SKSE::GetTaskInterface()) {
                task->AddTask([]() { EvaluateRunState("scheduled refresh"); });
            }
        }

        // Key transitions are handled on the main thread (HandleNativeKey), which
        // owns all game-state reads and the shader / message feedback.
        auto pollKey = [](int vk, int keyCode, int& prevState, const char* label) {
            if (vk == -1) return;
            int pressed = (GetAsyncKeyState(vk) & 0x8000) != 0;
            if (pressed != prevState) {
                if (pressed) {
                    SKSE::log::info("{} key detected: DIK {} -> VK {} (pressed)", label, keyCode, vk);
                }
                if (auto task = SKSE::GetTaskInterface()) {
                    bool down = pressed != 0;
                    task->AddTask([keyCode, down]() { HandleNativeKey(keyCode, down); });
                }
            }
            prevState = pressed;
        };

        pollKey(g_overrideKeyVK, g_overrideKeyCode, prevOverrideState, "Override");

        int runVK = g_runKeyVK;
        pollKey(runVK, g_runKeyCode, prevRunState, "Run");
        g_runKeyHeld = runVK != -1 && prevRunState != 0;

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

bool HasTownKeyword(RE::BGSLocation* location) {
    if (!location) return false;
    return (g_forms.locTypeCity && location->HasKeyword(g_forms.locTypeCity)) ||
        (g_forms.locTypeTown && location->HasKeyword(g_forms.locTypeTown));
}

// LocationCenterMarker location ref type (Skyrim.esm)
static constexpr RE::FormID kLocationCenterMarkerRefType = 0x0001BDF1;

// Finds the LocationCenterMarker for the town the player is in, read straight
// from the location's LCSR special refs. Replaces PYS_LocationMarkerQuest, which
// did the same lookup via a quest alias (LocationMarkerAlias, conditioned on
// LocTypeCity/LocTypeTown) that had to be restarted on every cell change.
// Checks the location itself first, then walks up through town-keyworded parents
// in case a town-tagged child location carries no center marker of its own.
RE::TESObjectREFR* GetTownCenterMarker(RE::BGSLocation* location) {
    // Cache by location: the poll calls this every ~200ms and the answer only
    // changes when the location does. Center markers are persistent refs, so the
    // pointer stays valid for the session.
    static RE::FormID cachedLocID = 0;
    static RE::TESObjectREFR* cachedMarker = nullptr;

    if (!location) return nullptr;
    if (location->GetFormID() == cachedLocID) return cachedMarker;

    RE::TESObjectREFR* found = nullptr;
    int depth = 0;
    for (auto loc = location; loc && !found && depth < 8; loc = loc->parentLoc, ++depth) {
        if (!HasTownKeyword(loc)) continue;
        for (auto& special : loc->specialRefs) {
            if (special.type && special.type->GetFormID() == kLocationCenterMarkerRefType) {
                found = RE::TESForm::LookupByID<RE::TESObjectREFR>(special.refData.refID);
                if (found) break;
            }
        }
    }

    cachedLocID = location->GetFormID();
    cachedMarker = found;

    if (g_config.detailLog) {
        SKSE::log::info("Town center marker for {} ({:08X}): {:08X}",
            location->GetName(), location->GetFormID(), found ? found->GetFormID() : 0);
    }
    return found;
}

// True when the player is within maxDist of the current town's center marker.
// Always false when there is no marker (wilderness, non-town location).
bool IsWithinTownCenterRange(RE::PlayerCharacter* player, RE::BGSLocation* location) {
    auto marker = GetTownCenterMarker(location);
    if (!marker) return false;
    return marker->GetPosition().GetDistance(player->GetPosition()) <= g_config.maxDist;
}

// Last-observed location inputs to ShouldRunHere, used only to detect
// transitions. Intentionally in-memory only (not part of SKSE co-save
// serialization): re-derived from live game state on the next check after any
// load, so there is nothing meaningful to persist.
struct LocationSnapshot {
    bool inInterior = false;
    RE::FormID locationID = 0;
    RE::FormID worldspaceID = 0;
    bool withinTownRange = false;

    bool operator==(const LocationSnapshot&) const = default;
};
static LocationSnapshot g_lastLocation;
static bool g_locationStateInitialized = false;

// Runs on the main thread (queued via the task interface from the input polling
// thread). Replaces the PYS_TrackerSpell / PYS_LocationTrackerScript magic effect
// and PYS_MarkerTracker's OnCellDetach: detects interior/exterior, location,
// worldspace and town-center-range changes the instant they happen, and runs
// EvaluateRunState (cooldown, shader FX, messages) in response. All four are
// folded into one snapshot so a single transition (e.g. entering a town) triggers
// one evaluation, not several.
void CheckLocationTransition() {
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    // Not fully loaded into a cell yet (e.g. mid loading-screen) - skip this tick
    // rather than risk evaluating against a half-attached state.
    if (!player->GetParentCell()) return;

    EnsureKeywordsInitialized();

    auto currentLoc = player->GetCurrentLocation();
    auto worldspace = player->GetWorldspace();

    LocationSnapshot now;
    now.inInterior = IsInInteriorActual(player);
    now.locationID = currentLoc ? currentLoc->GetFormID() : 0;
    now.worldspaceID = worldspace ? worldspace->GetFormID() : 0;
    // Range only affects the decision when unwalled-town walking is enabled;
    // skip it otherwise so walking around a town doesn't fire pointless events.
    now.withinTownRange = g_config.walkInTowns && g_config.walkInTownsUnwalled &&
        IsWithinTownCenterRange(player, currentLoc);

    if (!g_locationStateInitialized) {
        g_lastLocation = now;
        g_locationStateInitialized = true;
        return;
    }

    if (now == g_lastLocation) return;

    SKSE::log::info("Native location transition detected: interior {} -> {}, location {:08X} -> {:08X}, worldspace {:08X} -> {:08X}, in town range {} -> {}",
        g_lastLocation.inInterior, now.inInterior,
        g_lastLocation.locationID, now.locationID,
        g_lastLocation.worldspaceID, now.worldspaceID,
        g_lastLocation.withinTownRange, now.withinTownRange);

    g_lastLocation = now;

    EvaluateRunState("location transition");
}

// Single definition of "combat state" shared by the transition detector and
// ShouldRunHere, so the two can never disagree about whether we're in combat.
// Mirrors the old PYS_PlayerWeaponScript magic effect: in combat OR weapon drawn.
bool IsPlayerInCombatState(RE::PlayerCharacter* player) {
    if (!player) return false;
    auto actorState = player->AsActorState();
    return player->IsInCombat() || (actorState && actorState->IsWeaponDrawn());
}

// Last-observed combat state. Same rationale as g_lastLocation: in-memory
// only, re-derived from live game state on the first check after any load.
static bool g_lastCombatState = false;
static bool g_combatStateInitialized = false;

// Runs on the main thread alongside CheckLocationTransition. Replaces the
// PYS_PlayerWeaponScript magic effect: detects entering/leaving combat (or
// drawing/sheathing a weapon) and runs EvaluateRunState so the MCM "Preferred
// Combat State" is applied through the normal pipeline.
void CheckCombatTransition() {
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;
    if (!player->GetParentCell()) return;

    bool nowInCombat = IsPlayerInCombatState(player);

    if (!g_combatStateInitialized) {
        g_lastCombatState = nowInCombat;
        g_combatStateInitialized = true;
        return;
    }

    if (nowInCombat == g_lastCombatState) return;

    // Always track the state, even when the preference is "Do Nothing", so that
    // changing the MCM setting mid-combat doesn't produce a phantom transition.
    g_lastCombatState = nowInCombat;

    if (!g_config.modActive || g_config.combatRun == 0) return;

    SKSE::log::info("Native combat transition detected: now {} (combatRun: {})",
        nowInCombat ? "in combat" : "out of combat", g_config.combatRun);

    EvaluateRunState(nowInCombat ? "combat enter" : "combat exit");
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

    // Combat state check - must come before the wilderness early-out below, or the
    // combat preference would be ignored everywhere without a BGSLocation.
    bool playerIsInCombat = IsPlayerInCombatState(player);
    bool changeCombatState = (g_config.combatRun != 0);

    if (g_config.detailLog) {
        SKSE::log::info("Combat state: {}, playerIsInCombat (combat or weapon drawn): {}, combatRun: {}",
            player->IsInCombat(), playerIsInCombat, g_config.combatRun);
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

    auto currentLoc = player->GetCurrentLocation();
    if (!currentLoc) {
        return true; // In wilderness - auto-run enabled
    }

    if (g_config.detailLog) {
        DebugLocationKeywords(currentLoc);
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
            if (auto centerMarker = GetTownCenterMarker(currentLoc)) {
                float distanceFromMarker = centerMarker->GetPosition().GetDistance(player->GetPosition());
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

// Papyrus function to get plugin version
std::string GetPluginVersion(RE::StaticFunctionTag*) {
    return GetPluginVersionString();
}


// Register the native functions Papyrus still calls
bool RegisterPapyrusFunctions(RE::BSScript::IVirtualMachine* vm) {
    vm->RegisterFunction("GetPluginVersion", "PYS_UtilScript", GetPluginVersion);
    vm->RegisterFunction("SetMovementConfig", "PYS_UtilScript", SetMovementConfig);
    vm->RegisterFunction("NativeMCM_Initialize", "PYS_UtilScript", NativeMCM_Initialize);
    vm->RegisterFunction("SetNativeOverrideKey", "PYS_UtilScript", SetNativeOverrideKey);
    vm->RegisterFunction("SetNativeRunKey", "PYS_UtilScript", SetNativeRunKey);
    vm->RegisterFunction("SetFeedbackConfig", "PYS_UtilScript", SetFeedbackConfig);
    vm->RegisterFunction("RequestRunStateEvaluation", "PYS_UtilScript", RequestRunStateEvaluation);
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

    // Start input polling thread for native-only key handling
    g_inputThreadRunning.store(true);
    g_inputThread = std::thread([]() { InputPollingLoop(); });
    // Ensure graceful shutdown on process exit
    std::atexit(StopInputThread);

    // Register SKSE serialization callbacks for co-save persistence
    if (auto serialization = SKSE::GetSerializationInterface()) {
        serialization->SetUniqueID('PYSR');
        serialization->SetSaveCallback([](auto ser) {
            SKSE::log::info("Serialization SaveCallback invoked");
            // Open a single record for our data
            // Record version 2: marker ref dropped (now looked up natively per location)
            if (!ser->OpenRecord('PYSR', 2)) {
                SKSE::log::error("Failed to open serialization record");
                return;
            }

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
        });

        serialization->SetLoadCallback([](auto ser) {
            SKSE::log::info("Serialization LoadCallback invoked");
            std::uint32_t type = 0, version = 0, length = 0;
            while (ser->GetNextRecordInfo(type, version, length)) {
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

                // Version 1 records carry a trailing marker ref; read and discard it
                if (version < 2) {
                    std::uint32_t markerID = 0;
                    ser->ReadRecordData(&markerID, sizeof(markerID));
                }
            }
        });

        serialization->SetRevertCallback([](auto) {
            SKSE::log::info("Serialization RevertCallback invoked");
            g_forms.interiorWorldspaces.clear();
            g_forms.walledTownWorldspaces.clear();
            g_forms.extraTownKeywords.clear();
            g_forms.extraDunKeywords.clear();
            g_locationStateInitialized = false;
            g_combatStateInitialized = false;
            // Papyrus re-initializes the pipeline after the load completes
            g_pipelineActive = false;
            g_nextEvaluation = 0;
            g_playerOverride = false;
            g_lastToggle = std::chrono::steady_clock::time_point::min();
        });
    }

    return true;
}

// Initialization helper to populate native caches from Papyrus FormLists
void InitializeNativeSystem(RE::StaticFunctionTag*, RE::TESGlobal* PYS_Active, int combatRunSetting, bool walkInTowns, bool walkInTownsUnwalled, bool walkInDungeons, float maxDistance, RE::BGSListForm* interiorWorldspaces = nullptr, RE::BGSListForm* walledTownWorldspaces = nullptr, RE::BGSListForm* extraTownKeywords = nullptr, RE::BGSListForm* extraDunKeywords = nullptr) {
    bool modActive = true;
    if (PYS_Active) {
        // TESGlobal stores a float value; treat non-zero as true
        modActive = (PYS_Active->value != 0.0f);
    }

    SetMovementConfig(nullptr, modActive, combatRunSetting, walkInTowns, walkInTownsUnwalled, walkInDungeons, maxDistance);

    // Populate interior worldspaces
    if (interiorWorldspaces) {
        for (auto* form : interiorWorldspaces->forms) {
            if (!form) continue;
            auto ws = form->As<RE::TESWorldSpace>();
            if (ws) AddInteriorWorldspace(nullptr, ws);
        }
    }

    // Populate walled town worldspaces
    if (walledTownWorldspaces) {
        for (auto* form : walledTownWorldspaces->forms) {
            if (!form) continue;
            auto ws = form->As<RE::TESWorldSpace>();
            if (ws) AddWalledTownWorldspace(nullptr, ws);
        }
    }

    // Populate extra town keywords
    if (extraTownKeywords) {
        for (auto* form : extraTownKeywords->forms) {
            if (!form) continue;
            auto kw = form->As<RE::BGSKeyword>();
            if (kw) AddExtraTownKeyword(nullptr, kw);
        }
    }

    // Populate extra dungeon keywords
    if (extraDunKeywords) {
        for (auto* form : extraDunKeywords->forms) {
            if (!form) continue;
            auto kw = form->As<RE::BGSKeyword>();
            if (kw) AddExtraDunKeyword(nullptr, kw);
        }
    }

    SKSE::log::info("InitializeNativeSystem: populated native caches (interior/walled/extra keywords)");
}

// Called from MCM Initialize. Populates native config and caches, and enables the reaction pipeline.
void NativeMCM_Initialize(RE::StaticFunctionTag*, RE::Actor* /*PlayerRef*/, RE::TESGlobal* PYS_Active, int combatRunSetting, bool walkInTowns, bool walkInTownsUnwalled, bool walkInDungeons, float maxDistance, RE::BGSListForm* interiorWorldspaces = nullptr, RE::BGSListForm* walledTownWorldspaces = nullptr, RE::BGSListForm* extraTownKeywords = nullptr, RE::BGSListForm* extraDunKeywords = nullptr) {
    SKSE::log::info("NativeMCM_Initialize called");
    // Populate config and caches
    InitializeNativeSystem(nullptr, PYS_Active, combatRunSetting, walkInTowns, walkInTownsUnwalled, walkInDungeons, maxDistance, interiorWorldspaces, walledTownWorldspaces, extraTownKeywords, extraDunKeywords);
    g_pipelineActive = true;
}

// Decision + application of run/walk state with cooldown, used by EvaluateRunState.
// Returns action flags:
static constexpr std::uint32_t NMS_NONE = 0;
static constexpr std::uint32_t NMS_CHANGED_TO_RUN = 1 << 0;
static constexpr std::uint32_t NMS_CHANGED_TO_WALK = 1 << 1;
static constexpr std::uint32_t NMS_COOLDOWN = 1 << 2;

int NativeMCM_SetRunState(RE::StaticFunctionTag*, RE::Actor* akActor, bool playerOverride, bool inputRunPressed, float timeout) {
    if (!akActor) return NMS_NONE;
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return NMS_NONE;

    if (akActor != player) return NMS_NONE;

    // If player override or input pressed, leave the player's choice alone
    if (playerOverride || inputRunPressed) {
        return NMS_NONE;
    }

    // If sneaking, skip changes
    if (player->IsSneaking()) {
        return NMS_NONE;
    }

    // Cooldown check (use steady_clock)
    auto now = std::chrono::steady_clock::now();
    float cooldownPeriod = timeout / 4.0f;
    if (g_lastToggle != std::chrono::steady_clock::time_point::min()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::duration<float>>(now - g_lastToggle).count();
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
        g_lastToggle = now;
        return newState ? NMS_CHANGED_TO_RUN : NMS_CHANGED_TO_WALK;
    }

    return NMS_NONE;
}

// =======================
// Reaction pipeline
// =======================
// Native replacement for the MCM's SetRunState / OnNativeKey / ShowOverrideFeedback
// and its OnUpdate refresh loop. Everything here runs on the main thread: key
// transitions and the refresh timer are queued from the input polling thread,
// location / combat transitions call in directly from their Check* functions.

// MCM settings that only the reaction pipeline needs (SetFeedbackConfig)
struct FeedbackConfig {
    int shaderFX = 1;        // 0 = disabled, 1 = red/green, 2 = orange/blue, 3 = yellow/purple
    bool msgVerbose = true;
    float refreshTime = 30.0f;
    float timeout = 5.0f;
};
FeedbackConfig g_feedback;

static constexpr const char* kPluginName = "PaceYourself.esp";
static constexpr float kChangeShaderDuration = 1.0f;
static constexpr float kOverrideShaderDuration = 2.0f;
// While paused / between cells, retry at this interval instead of evaluating
static constexpr float kDeferredRetrySeconds = 0.5f;

// Shaders and messages from PaceYourself.esp, looked up by local form ID.
// Override / resume shaders are indexed by the MCM shaderFX setting (index 0 unused).
struct FeedbackForms {
    RE::TESEffectShader* changeIndicator = nullptr;  // white - automatic walk/run change
    RE::TESEffectShader* overrideShaders[4]{};       // player overrode the mod
    RE::TESEffectShader* resumeShaders[4]{};         // player choice matches the mod
    RE::BGSMessage* walkMsg = nullptr;
    RE::BGSMessage* runMsg = nullptr;
    RE::BGSMessage* pauseMsg = nullptr;
    RE::BGSMessage* resumeMsg = nullptr;
    bool resolved = false;

    void Resolve() {
        if (resolved) return;
        auto dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) return;

        auto shader = [&](RE::FormID id) { return dataHandler->LookupForm<RE::TESEffectShader>(id, kPluginName); };
        auto message = [&](RE::FormID id) { return dataHandler->LookupForm<RE::BGSMessage>(id, kPluginName); };

        changeIndicator = shader(0x00C);     // MuffleFXShader_PYSChangeIndicator (white)
        overrideShaders[1] = shader(0x004);  // MuffleFXShader_PYSDisable (red)
        resumeShaders[1] = shader(0x005);    // MuffleFXShader_PYSEnable (green)
        overrideShaders[2] = shader(0x006);  // MuffleFXShader_PYSDisableAlt (orange)
        resumeShaders[2] = shader(0x007);    // MuffleFXShader_PYSEnableAlt (blue)
        overrideShaders[3] = shader(0x00B);  // MuffleFXShader_PYSDisableAlt2 (yellow)
        resumeShaders[3] = shader(0x00A);    // MuffleFXShader_PYSEnableAlt2 (purple)
        walkMsg = message(0x008);            // PYS_WalkMsg
        runMsg = message(0x009);             // PYS_RunMsg
        pauseMsg = message(0x00D);           // PYS_PauseMsg
        resumeMsg = message(0x00E);          // PYS_ResumeMsg
        resolved = true;

        bool allFound = changeIndicator && walkMsg && runMsg && pauseMsg && resumeMsg;
        for (int i = 1; i < 4; ++i) {
            allFound = allFound && overrideShaders[i] && resumeShaders[i];
        }
        if (allFound) {
            SKSE::log::info("Feedback forms resolved from {}", kPluginName);
        } else {
            SKSE::log::warn("Some feedback forms were not found in {}; missing shaders/messages will be skipped", kPluginName);
        }
    }
};
FeedbackForms g_feedbackForms;

bool IsGamePaused() {
    auto ui = RE::UI::GetSingleton();
    return ui && ui->GameIsPaused();
}

void PlayFeedbackShader(RE::TESEffectShader* shader, float duration) {
    auto player = RE::PlayerCharacter::GetSingleton();
    if (player && shader) {
        player->ApplyEffectShader(shader, duration);
    }
}

// Equivalent of Message.Show() for the mod's notification-style messages
void ShowFeedbackMessage(RE::BGSMessage* message) {
    if (!message) return;
    RE::BSString text;
    message->GetDescription(text, message);
    if (!text.empty()) {
        RE::DebugNotification(text.c_str());
    }
}

void ShowOverrideFeedback(bool overrode, bool aligns) {
    if (!overrode && !aligns) return;
    g_feedbackForms.Resolve();

    int fx = g_feedback.shaderFX;
    if (fx > 0 && fx < 4) {
        PlayFeedbackShader(overrode ? g_feedbackForms.overrideShaders[fx] : g_feedbackForms.resumeShaders[fx], kOverrideShaderDuration);
    }

    SKSE::log::info("{}", overrode ? "Player manually overrode script preference" : "Player choice aligns with script preference");
    if (g_feedback.msgVerbose) {
        ShowFeedbackMessage(overrode ? g_feedbackForms.pauseMsg : g_feedbackForms.resumeMsg);
    }
}

// Decide and apply walk/run, play feedback, and schedule the next refresh.
void EvaluateRunState(const char* reason) {
    if (!g_pipelineActive || !g_config.modActive) return;

    auto player = RE::PlayerCharacter::GetSingleton();
    // Papyrus never ran while menus were open; match that by deferring until the
    // game resumes (or the player is attached to a cell again)
    if (!player || !player->GetParentCell() || IsGamePaused()) {
        ScheduleEvaluation(kDeferredRetrySeconds);
        return;
    }

    if (g_config.detailLog) {
        SKSE::log::info("EvaluateRunState: {}", reason);
    }

    // Holding the run key counts as a manual override
    bool runHeld = g_runKeyHeld;
    if (runHeld && !g_playerOverride) {
        g_playerOverride = true;
        ShowOverrideFeedback(true, false);
    }

    int flags = NativeMCM_SetRunState(nullptr, player, g_playerOverride, runHeld, g_feedback.timeout);

    if (flags & NMS_COOLDOWN) {
        SKSE::log::info("Cooldown active - deferring toggle");
        ScheduleEvaluation(g_feedback.timeout / 2.0f);
        return;
    }

    if (flags & (NMS_CHANGED_TO_RUN | NMS_CHANGED_TO_WALK)) {
        g_feedbackForms.Resolve();
        if (g_feedback.shaderFX != 0) {
            PlayFeedbackShader(g_feedbackForms.changeIndicator, kChangeShaderDuration);
        }
        if (g_feedback.msgVerbose) {
            ShowFeedbackMessage((flags & NMS_CHANGED_TO_RUN) ? g_feedbackForms.runMsg : g_feedbackForms.walkMsg);
        }
    }

    ScheduleEvaluation(g_feedback.refreshTime);
}

// Override / run key transitions from the input polling thread
void HandleNativeKey(int keyCode, bool down) {
    if (!g_pipelineActive || !g_config.modActive) return;
    // Ignore keys typed into menus / the console. A release after the menu
    // closes is still handled; it recomputes the override from scratch.
    if (IsGamePaused()) return;

    if (down) {
        if (keyCode == g_runKeyCode) {
            // Hold-to-run key: the override engages as soon as it's held, so give
            // feedback immediately. If there's no mismatch yet, still treat the
            // press as a manual override so the feedback isn't a silent no-op.
            int flags = NativeMCM_HandleOverride(nullptr, keyCode);
            bool overrode = (flags & 1) != 0;
            bool aligns = (flags & 2) != 0;
            if (!overrode && !aligns) {
                overrode = true;
            }
            g_playerOverride = overrode;
            ShowOverrideFeedback(overrode, aligns);
        } else {
            // Toggle key: feedback is shown on release, once the game has applied
            // the toggle; just mark the override as active for now
            g_playerOverride = true;
        }
        return;
    }

    // NativeMCM_HandleOverride stores the resulting override state itself
    int flags = NativeMCM_HandleOverride(nullptr, keyCode);
    ShowOverrideFeedback((flags & 1) != 0, (flags & 2) != 0);
}

// Papyrus: push the MCM settings the reaction pipeline uses
void SetFeedbackConfig(RE::StaticFunctionTag*, int shaderFX, bool msgVerbose, float refreshTime, float timeout, bool detailLog) {
    g_feedback.shaderFX = std::clamp(shaderFX, 0, 3);
    g_feedback.msgVerbose = msgVerbose;
    g_feedback.refreshTime = std::max(refreshTime, 1.0f);
    g_feedback.timeout = std::max(timeout, 0.1f);
    g_config.detailLog = detailLog;
    SKSE::log::info("SetFeedbackConfig: shaderFX {}, msgVerbose {}, refreshTime {}, timeout {}, detailLog {}",
        g_feedback.shaderFX, msgVerbose, g_feedback.refreshTime, g_feedback.timeout, detailLog);
}

// Papyrus: evaluate now (on the main thread) - replaces the MCM's SetRunState
void RequestRunStateEvaluation(RE::StaticFunctionTag*) {
    if (auto task = SKSE::GetTaskInterface()) {
        task->AddTask([]() { EvaluateRunState("Papyrus request"); });
    }
}
