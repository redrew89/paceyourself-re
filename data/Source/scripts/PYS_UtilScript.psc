Scriptname PYS_UtilScript Hidden

; =======================
; Native Function Declarations
; =======================

; Original walk/run state functions
bool Function SetPlayerWalkRunState(bool shouldRun) Global Native
bool Function GetPlayerWalkRunState() Global Native

string Function GetPluginVersion() Global Native

; New native movement logic functions
bool Function ShouldRunHere() Global Native
bool Function AutoSetPlayerMovement() Global Native

; Configuration functions
Function SetMovementConfig(bool modActive, int combatRun, bool walkInTowns, bool walkInTownsUnwalled, bool walkInDungeons, float maxDist) Global Native
Function SetDetailedLogging(bool bLog) global native
Function ReinitializeKeywords() global native
Function SetLocationMarker(ObjectReference marker) Global Native

; Form collection functions
Function AddInteriorWorldspace(WorldSpace worldspace) Global Native
Function AddWalledTownWorldspace(WorldSpace worldspace) Global Native
Function AddExtraTownKeyword(Keyword keyword) Global Native
Function AddExtraDunKeyword(Keyword keyword) Global Native

; Debug functions
Function LogCurrentConfig() Global Native
Function LogCurrentLocation() Global Native
bool Function GetCurrentConfig(int configType) Global Native
; configType: 0=modActive, 1=walkInTowns, 2=walkInTownsUnwalled, 3=walkInDungeons, 4=combatRun

; =======================
; Helper Functions for Papyrus Compatibility
; =======================

; Initialize the native system with form lists (call this during mod startup)
Function InitializeNativeSystem(GlobalVariable PYS_Active, int combatRunSetting, bool walkInTowns, bool walkInTownsUnwalled, bool walkInDungeons, float maxDistance, FormList interiorWorldspaces = None, FormList walledTownWorldspaces = None, FormList extraTownKeywords = None, FormList extraDunKeywords = None) Global
    
    ; Set basic configuration
    bool modActive = (PYS_Active.GetValueInt() != 0)
    SetMovementConfig(modActive, combatRunSetting, walkInTowns, walkInTownsUnwalled, walkInDungeons, maxDistance)
    
    ; Populate interior worldspaces
    if interiorWorldspaces
        int i = 0
        int count = interiorWorldspaces.GetSize()
        while i < count
            WorldSpace ws = interiorWorldspaces.GetAt(i) as WorldSpace
            if ws
                AddInteriorWorldspace(ws)
            endif
            i += 1
        endwhile
    endif
    
    ; Populate walled town worldspaces
    if walledTownWorldspaces
        int i = 0
        int count = walledTownWorldspaces.GetSize()
        while i < count
            WorldSpace ws = walledTownWorldspaces.GetAt(i) as WorldSpace
            if ws
                AddWalledTownWorldspace(ws)
            endif
            i += 1
        endwhile
    endif
    
    ; Populate extra town keywords
    if extraTownKeywords
        int i = 0
        int count = extraTownKeywords.GetSize()
        while i < count
            Keyword kw = extraTownKeywords.GetAt(i) as Keyword
            if kw
                AddExtraTownKeyword(kw)
            endif
            i += 1
        endwhile
    endif
    
    ; Populate extra dungeon keywords
    if extraDunKeywords
        int i = 0
        int count = extraDunKeywords.GetSize()
        while i < count
            Keyword kw = extraDunKeywords.GetAt(i) as Keyword
            if kw
                AddExtraDunKeyword(kw)
            endif
            i += 1
        endwhile
    endif
    
EndFunction

; Update configuration at runtime (for MCM changes)
Function UpdateNativeConfig(GlobalVariable PYS_Active, int combatRunSetting, bool walkInTowns, bool walkInTownsUnwalled, bool walkInDungeons, float maxDistance) Global
    bool modActive = (PYS_Active.GetValueInt() != 0)
    
	PYS_MCMScript MCM = Game.GetFormFromFile(2817, "PaceYourself.esp") as PYS_MCMScript
    ; Log what we're trying to set
    MCM.LogMsg("UpdateNativeConfig called with:",false)
    MCM.LogMsg("  modActive: " + modActive,false)
    MCM.LogMsg("  combatRunSetting: " + combatRunSetting, false)
    MCM.LogMsg("  walkInTowns: " + walkInTowns,false )
    MCM.LogMsg("  walkInTownsUnwalled: " + walkInTownsUnwalled, false)
    MCM.LogMsg("  walkInDungeons: " + walkInDungeons,false)
    MCM.LogMsg("  maxDistance: " + maxDistance,false)
    
    SetMovementConfig(modActive, combatRunSetting, walkInTowns, walkInTownsUnwalled, walkInDungeons, maxDistance)
    
    ; Verify the settings took effect
    Utility.Wait(0.1) ; Small delay to ensure native code processed the change
    
    MCM.LogMsg("Verification - walkInTowns setting: " + GetCurrentConfig(1),false)
    MCM.LogMsg("Verification - walkInTownsUnwalled setting: " + GetCurrentConfig(2),false)
EndFunction

; Set the location marker for distance calculations
Function UpdateLocationMarker(ObjectReference marker) Global
    SetLocationMarker(marker)
EndFunction

; =======================
; Backward Compatibility Functions
; =======================

; Drop-in replacement for your original ShouldRunHere function
; Just calls the native version now
bool Function ShouldRunHereCompat() Global
    return ShouldRunHere()
EndFunction

; Main function that handles everything - replaces your Papyrus logic entirely
; Returns the new movement state (true = running, false = walking)
bool Function HandleMovementLogic() Global
    return AutoSetPlayerMovement()
EndFunction

; =======================
; Utility Functions
; =======================

; Get current movement mode (for compatibility with Game.GetPlayerMovementMode())
bool Function GetPlayerMovementMode() Global
    return GetPlayerWalkRunState()
EndFunction

; Force set movement state (direct wrapper)
bool Function ForceMovementState(bool shouldRun) Global
    return SetPlayerWalkRunState(shouldRun)
EndFunction

; =======================
; Debug/Logging Functions
; =======================

; Test function to verify native integration
Function TestNativeFunctions() Global
    
	PYS_MCMScript MCM = Game.GetFormFromFile(2817, "PaceYourself.esp") as PYS_MCMScript
	
	MCM.LogMsg("=== PYS Native Function Test ===", false)
    
    ; Test basic state functions
    bool currentState = GetPlayerWalkRunState()
    MCM.LogMsg("Current movement state: " + currentState, false)
    
    ; Test decision logic
    bool shouldRun = ShouldRunHere()
    MCM.LogMsg("Should run decision: " + shouldRun, false)
    
    ; Test auto movement
    bool newState = AutoSetPlayerMovement()
    MCM.LogMsg("Auto movement result: " + newState, false)
    
    ; Log current configuration and location
    LogCurrentConfig()
    LogCurrentLocation()
    
    MCM.LogMsg("=== Test Complete ===",false)
EndFunction

; MCM debugging function - call this from your MCM when settings change
Function DebugMCMSettings(GlobalVariable PYS_Active, int combatRunSetting, bool walkInTowns, bool walkInTownsUnwalled, bool walkInDungeons, float maxDistance) Global

	PYS_MCMScript MCM = Game.GetFormFromFile(2817, "PaceYourself.esp") as PYS_MCMScript
	
    MCM.LogMsg("=== MCM Debug - Before Update ===",false)
    LogCurrentConfig()
    
    UpdateNativeConfig(PYS_Active, combatRunSetting, walkInTowns, walkInTownsUnwalled, walkInDungeons, maxDistance)
    
    MCM.LogMsg("=== MCM Debug - After Update ===",false)
    LogCurrentConfig()
    
    ; Test the decision logic with current location
    LogCurrentLocation()
    bool shouldRun = ShouldRunHere()
    MCM.LogMsg("Movement decision with new settings: " + shouldRun,false)
EndFunction

; ---- UTILITY FUNCTIONS -- Now relocated to PYS_UtilScript


Function CheckGamepad()	Global
	
	PYS_MCMScript MCM = Game.GetFormFromFile(2817, "PaceYourself.esp") as PYS_MCMScript
	
	if Game.UsingGamepad()		
		MCM.LogMsg("Gamepad detected. Shutting down. Restart without gamepad to resume.")
		MCM.PlayerRef.RemoveSpell(MCM.PYS_TrackerSpell)
		MCM.PYS_Active.SetValueInt(0)
		MCM.PYS_globalToggle = false
		MCM.ToggleModFlag(false)
	else
		MCM.PYS_Active.SetValueInt(1)
		MCM.PYS_globalToggle = true
		MCM.ToggleModFlag(true)		
	endif

	DebugMCMSettings(MCM.PYS_Active, MCM.PYS_combatRun, MCM.PYS_walkInTowns, MCM.PYS_walkInTownsUnwalled, MCM.PYS_walkInDungeons, MCM.PYS_maxDist)

endFunction