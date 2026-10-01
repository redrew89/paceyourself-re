Scriptname PYS_MCMScript extends ski_configbase conditional

import PYS_UtilScript ; Native functions now defined and implemented with DLL plugin - 2.0 rebuild

Actor Property PlayerRef Auto

; Global property for mod functionality
GlobalVariable Property PYS_Active Auto

; Formlist properties that can be manipulated by FLM
Formlist Property PYS_ExtraTownKeywordFLST auto
Formlist Property PYS_ExtraDunKeywordFLST auto
Formlist Property PYS_InteriorWorldspacesFLST Auto
Formlist Property PYS_WalledTownWorldspacesFLST Auto

; MCM settings - Exposed as conditional properties for possible use within the plugin
bool Property PYS_walkInTowns = false Auto conditional
bool Property PYS_walkInDungeons = false Auto conditional
bool Property PYS_globalToggle = true Auto conditional
bool Property PYS_walkInTownsUnwalled = false Auto conditional

; PYS_playerOverride is now managed natively by the SKSE plugin
bool Property PYS_detailLog = true auto conditional
bool Property PYS_msgVerbose = true auto conditional
bool Property PYS_hasSMC = false auto conditional

int Property PYS_overrideToggleKey = -1 Auto conditional
int Property PYS_runKey = -1 Auto conditional

int Property PYS_combatRun auto conditional ; 0 - Do nothing , 1 - Always run, 2 - Always walk
int Property PYS_shaderFX auto conditional ; 0 - None, 1 - red/green, 2 - orange/blue, 3 - yellow/purple
String[] PYS_combatStates
String[] PYS_shaderColorOpts

float Property PYS_maxDist = 6000.0 Auto conditional
float Property PYS_timeout = 5.0 Auto
float Property PYS_refreshTime = 30.0 Auto

; Internal variables for toggle options
int opt_walkInTowns
int opt_walkInDungeons
int opt_walkInTownsUnwalled
int opt_globalToggle
int opt_Timeout
int opt_RefreshTime
int opt_overrideToggleKey
int opt_runKey
int opt_maxDist
int opt_altFX
int opt_msgVerbose
int opt_combatRun
int opt_detailLog

; Flags - to control MCM option display
int flag_globalToggle
int flag_maxDist
int flag_keyMap
int flag_walkInTownsExt
int flag_disable

bool Property firstRun = true Auto

; --- MCM OPTIONS

int function GetVersion()
	;return 12  ; 1.2 - Initial release with script versioning
	;return 121 ; 1.2.1 - Hotfix
	;return 122 ; 1.2.2 - Bugfix release
	;return 200 ; 2.0.0 - SKSE rework
	;return 220  ; 2.2.0 - Optimzations and native override logic
	;return 221  ; 2.2.1 - Fix for VM freezing
	;return 223  ; 2.2.3 - Native combat/weapon state tracking
	return 250  ; 2.5.0 - Native reaction pipeline (feedback, cooldown, refresh)
endFunction

Event OnConfigInit()

	Debug.Notification("Pace Yourself RE: Initializing. Stand by.")
	debug.OpenUserLog("PYSRE")
	LogMsg("Logging started. Version " + GetVersion(), false)

	ModName = "Pace Yourself RE"
	Pages = New String[1]
	Pages[0] = "Main"

	if PlayerRef == none
		PlayerRef = Game.GetPlayer()
	endif

	flag_globalToggle = OPTION_FLAG_NONE
	flag_disable = OPTION_FLAG_DISABLED

	if !PYS_walkInTownsUnwalled
		flag_maxDist = OPTION_FLAG_DISABLED
	endif
	flag_keyMap = OPTION_FLAG_NONE
	if !PYS_walkInTowns
		flag_walkInTownsExt = OPTION_FLAG_DISABLED
	endif

	RegisterForSingleUpdate(PYS_refreshTime/3)

endEvent

event OnVersionUpdate(int a_version)

	if (a_version >= 250 && CurrentVersion < 250 ) && !firstRun
		LogMsg("Pace Yourself RE: Updating script to version 2.5.0")
		CurrentVersion = 250
		firstRun = true
		OnConfigInit()
	endIf

endEvent

Function ToggleModFlag(bool aVal)

	if aVal
		flag_globalToggle = OPTION_FLAG_NONE
	else
		flag_globalToggle = OPTION_FLAG_DISABLED
	endif

endfunction

Event OnPageReset(string Page)

	if Page == ""
		LoadCustomContent("PYS//PYS_Card.dds",76.0,23.0)
	elseif Page == "Main"
		UnloadCustomContent()
		SetTitleText("Pace Yourself RE")
		SetCursorPosition(0)
		SetCursorFillMode(TOP_TO_BOTTOM)
		AddHeaderOption("Main Configuration")
		opt_globalToggle = AddToggleOption("Mod Active:", PYS_globalToggle, flag_globalToggle)
		AddEmptyOption()
		if PYS_globalToggle
			opt_walkInTowns = AddToggleOption("Walk in Towns:", PYS_walkInTowns)
			opt_walkInTownsUnwalled = AddToggleOption("Include Unwalled Towns:", PYS_walkInTownsUnwalled, flag_walkInTownsExt)
			opt_maxDist = AddSliderOption("Max Distance From Center:", PYS_maxDist, "{0} units", flag_maxDist)
			AddEmptyOption()

			opt_walkInDungeons = AddToggleOption("Walk in Dungeons:", PYS_walkInDungeons)
			opt_combatRun = AddMenuOption("Preferred Combat State:", PYS_combatStates[PYS_combatRun])

			SetCursorPosition(1)
			AddHeaderOption("Debug")
			opt_msgVerbose = AddToggleOption("Verbose Messages: ", PYS_msgVerbose)
			opt_detailLog = AddToggleOption("Detailed Logging: ", PYS_detailLog)
			opt_altFX = AddMenuOption("Effect Shader Accessibility:", PYS_shaderColorOpts[PYS_shaderFX])
			AddEmptyOption()
			opt_runKey = AddKeyMapOption("Run Key:", PYS_runKey, flag_keyMap)
			opt_overrideToggleKey = AddKeyMapOption("Manual Override:", PYS_overrideToggleKey, flag_keyMap)
			AddEmptyOption()
			AddHeaderOption("Internal Functions")
			opt_Timeout = AddSliderOption("Time-out:", PYS_timeout, "{1} seconds")
			opt_RefreshTime = AddSliderOption("Refresh Rate:", PYS_refreshTime, "Every {0} seconds")
			AddEmptyOption()
			string versionString = PYS_UtilScript.GetPluginVersion()
			AddTextOption("SKSE Plugin Version:", versionString, flag_disable)

		elseif !PYS_globalToggle && flag_globalToggle == OPTION_FLAG_DISABLED
			AddTextOption("Pace Yourself RE disabled by gamepad. Please restart game without gamepad to enable.", none)
		endif
	else
		UnloadCustomContent()
	endif

endEvent

Event OnConfigClose()

	UpdateNativeConfig(PYS_Active, PYS_combatRun, PYS_walkInTowns, PYS_walkInTownsUnwalled, PYS_walkInDungeons, PYS_maxDist)
	PushFeedbackConfig()
	SetRunState(PlayerRef)

endEvent

Event OnOptionMenuOpen(int option)

	if option == opt_altFX
		LogMsg("Current altFX state: " + PYS_shaderFX + ":" + PYS_shaderColorOpts[PYS_shaderFX], false)
		SetMenuDialogOptions(PYS_shaderColorOpts)
		SetMenuDialogStartIndex(PYS_shaderFX)
		SetMenuDialogDefaultIndex(1)
	endif
	if option == opt_combatRun
		LogMsg("Current combatRun state: " + PYS_combatRun + ":" + PYS_combatStates[PYS_combatRun],false)
		SetMenuDialogOptions(PYS_combatStates)
		SetMenuDialogStartIndex(PYS_combatRun)
		SetMenuDialogDefaultIndex(0)
	endif

endEvent

Event OnOptionMenuAccept(int option, int index)

	; Check for valid index first - negative values mean user cancelled or error occurred
	if index < 0
		LogMsg("Menu cancelled or invalid index received: " + index, false)
		return
	endif

	if option == opt_combatRun
		if index >= 0 && index < PYS_combatStates.Length
			PYS_combatRun = index
			SetMenuOptionValue(opt_combatRun, PYS_combatStates[PYS_combatRun], false)
			LogMsg("PYS_combatRun set to: "+ PYS_combatRun,false)
		else
			LogMsg("Invalid combat run index: " + index, false)
		endif
	elseif option == opt_altFX
		if index >= 0 && index < PYS_shaderColorOpts.Length
			PYS_shaderFX = index
			SetMenuOptionValue(opt_altFX, PYS_shaderColorOpts[PYS_shaderFX], false)
			LogMsg("PYS_shaderFX set to: "+ PYS_shaderFX,false)
		else
			LogMsg("Invalid shader FX index: " + index, false)
		endif
	endif

	ForcePageReset()
	RegisterForSingleUpdate(PYS_timeout)

endEvent

event OnOptionDefault(int option)

	if option == opt_walkInTowns
		PYS_walkInTowns = false
		SetToggleOptionValue(option, PYS_walkInTowns, false)
	elseif option == opt_walkInDungeons
		PYS_walkInDungeons = false
		SetToggleOptionValue(option, PYS_walkInDungeons, false)
	elseif option == opt_walkInTownsUnwalled
		PYS_walkInTownsUnwalled = false
		SetToggleOptionValue(option, PYS_walkInTownsUnwalled,false)
	elseif option == opt_maxDist
		PYS_maxDist = 6000
		SetSliderOptionValue(option, PYS_maxDist, "{0} units", false)
	elseif option == opt_msgVerbose
		PYS_msgVerbose = true
		SetToggleOptionValue(option, PYS_msgVerbose,false)
	elseif option == opt_RefreshTime
		PYS_refreshTime = 30.0
		SetSliderOptionValue(option, PYS_refreshTime, "Every {0} seconds", false)
	elseif option == opt_Timeout
		PYS_timeout = 5.0
		SetSliderOptionValue(option, PYS_timeout, "{1} seconds", false)
	elseif option == opt_runKey
		PYS_runKey = Input.GetMappedKey("Run")
		SetKeymapOptionValue(option,PYS_runKey,false)
		PYS_UtilScript.SetNativeRunKey(PYS_runKey)
	elseif option == opt_overrideToggleKey
		PYS_overrideToggleKey = Input.GetMappedKey("Toggle Always Run")
		SetKeymapOptionValue(option,PYS_overrideToggleKey,false)
		PYS_UtilScript.SetNativeOverrideKey(PYS_overrideToggleKey)
	endIf


	LogMsg("Set " + option + " value to default.",false)

	ForcePageReset()
	RegisterForSingleUpdate(PYS_timeout)

endEvent

event OnOptionKeyMapChange(int option, int keyCode, string conflictControl, string conflictName)

	if (option == opt_runKey)
		bool continue = true
		if (conflictControl != "")
			string msg
			if (conflictName != "")
				msg = "This key is already mapped to:\n\"" + conflictControl + "\"\n(" + conflictName + ")\n\nAre you sure you want to continue?"
			else
				msg = "This key is already mapped to:\n\"" + conflictControl + "\"\n\nAre you sure you want to continue?"
			endif

			continue = ShowMessage(msg, true, "$Yes", "$No")
		endif

		if (continue)
			PYS_runKey = keyCode
			SetKeymapOptionValue(option, keyCode)
			PYS_UtilScript.SetNativeRunKey(PYS_runKey)
			LogMsg("Set run key: " + keyCode)
		endif
	endif

	if (option == opt_overrideToggleKey)
		bool continue = true
		if (conflictControl != "")
			string msg
			if (conflictName != "")
				msg = "This key is already mapped to:\n\"" + conflictControl + "\"\n(" + conflictName + ")\n\nAre you sure you want to continue?"
			else
				msg = "This key is already mapped to:\n\"" + conflictControl + "\"\n\nAre you sure you want to continue?"
			endif

			continue = ShowMessage(msg, true, "$Yes", "$No")
		endif

		if (continue)
			PYS_overrideToggleKey = keyCode
			SetKeymapOptionValue(option, keyCode)
			; Inform native plugin of the remapped override key
			PYS_UtilScript.SetNativeOverrideKey(PYS_overrideToggleKey)
			LogMsg("Set override key: " + keyCode)
		endif
	endif


	ForcePageReset()
	RegisterForSingleUpdate(PYS_timeout)

endEvent

string function GetCustomControl(int keyCode)
	if (keyCode == PYS_runKey)
		return "Run Key"
	elseif (keyCode == PYS_overrideToggleKey)
		return "Override Pace Yourself RE"
	else
		return ""
	endif
endFunction

Event OnOptionSelect(int option)

	if option == opt_globalToggle
		PYS_globalToggle = !PYS_globalToggle
		SetToggleOptionValue(option, PYS_globalToggle,false)
		PYS_Active.SetValueInt(PYS_globalToggle as Int)
		LogMsg(": Global toggle set to " + PYS_globalToggle, false)
		if PYS_globalToggle
			Initialize()
		endif

	elseif option == opt_walkInDungeons
		PYS_walkInDungeons = !PYS_walkInDungeons
		SetToggleOptionValue(option, PYS_walkInDungeons,false)
		LogMsg("Dungeon toggle set to " + PYS_walkInDungeons, false)

	elseif option == opt_walkInTowns
		PYS_walkInTowns = !PYS_walkInTowns
		SetToggleOptionValue(option, PYS_walkInTowns,false)
		LogMsg("Town toggle set to " + PYS_walkInTowns, false)
		if PYS_walkInTowns
			flag_walkInTownsExt = OPTION_FLAG_NONE
		else
			flag_walkInTownsExt = OPTION_FLAG_DISABLED
		endif
	elseif option == opt_walkInTownsUnwalled
		PYS_walkInTownsUnwalled = !PYS_walkInTownsUnwalled
		SetToggleOptionValue(option, PYS_walkInTownsUnwalled,false)
		LogMsg("Allow Unwalled Towns set to " + PYS_walkInTownsUnwalled, false)
		if PYS_walkInTownsUnwalled
			flag_maxDist = OPTION_FLAG_NONE
		else
			flag_maxDist = OPTION_FLAG_DISABLED
		endif
	elseif option == opt_msgVerbose
		PYS_msgVerbose = !PYS_msgVerbose
		SetToggleOptionValue(option, PYS_msgVerbose, false)
	elseif option == opt_runKey
		ShowMessage("Press a key to map this action.", false)
	elseif option == opt_overrideToggleKey
		ShowMessage("Press a key to map this action.", false)
	elseif option == opt_detailLog
		PYS_detailLog = !PYS_detailLog
		SetToggleOptionValue(option, PYS_detailLog, false)
	endif

	ForcePageReset()
	RegisterForSingleUpdate(PYS_timeout)

endEvent

Event OnOptionSliderOpen(int option)

	if option == opt_Timeout
		SetSliderDialogStartValue(PYS_timeout)
		SetSliderDialogRange(3.0,20.0)
		SetSliderDialogInterval(1.0)
	elseif option == opt_RefreshTime
		SetSliderDialogStartValue(PYS_refreshTime)
		SetSliderDialogRange(20.0,600.0)
		SetSliderDialogInterval(5.0)
	elseif option == opt_maxDist
		SetSliderDialogStartValue(PYS_maxDist)
		SetSliderDialogRange(2500,15000)
		SetSliderDialogInterval(500)
	endif

endEvent

Event OnOptionSliderAccept(int option, float value)

	if option == opt_Timeout
		PYS_timeout = value
		SetSliderOptionValue(option, value, "{1} seconds")
	elseif option == opt_RefreshTime
		PYS_refreshTime = value
		SetSliderOptionValue(option, value, "Every {0} seconds")
	elseif option == opt_maxDist
		PYS_maxDist = value
		SetSliderOptionValue(option, value, "{0} units")
	endif

	ForcePageReset()
	RegisterForSingleUpdate(PYS_timeout)

endEvent

Event OnOptionHighlight(int option)

	if option == opt_globalToggle
		SetInfoText("Globally enables or disables the mod.")
	elseif option == opt_walkInDungeons
		SetInfoText("Enables auto-walking in dungeons. (Keyword: LocTypeClearable)")
	elseif option == opt_walkInTowns
		SetInfoText("Enables auto-walking in towns and cities. (Keywords: LocTypeTown, LocTypeCity)")
	elseif option == opt_Timeout
		SetInfoText("Adjust the timeout for toggle attempts.")
	elseif option == opt_RefreshTime
		SetInfoText("Adjust how often the system refreshes.")
	elseif option == opt_walkInTownsUnwalled
		SetInfoText("Disable to automatically switch to walking in non-walled towns in exterior worldspaces (e.g., Riverwood, Morthal, etc.)")
	elseif option == opt_maxDist
		SetInfoText("Maximum distance from center of town locations to continue walking, while Include Unwalled Towns is enabled.")
	elseif option == opt_runKey
		SetInfoText("Optional hold-to-run key used by the native input polling thread.")
	elseif option == opt_overrideToggleKey
		SetInfoText("Optional key to override script and manually switch between walk and run. Duplicates normal Toggle Auto Run key functions. Failsafe.")
	elseif option == opt_altFX
		SetInfoText("Select color options for Effect Shaders, or disable entirely.")
	elseif option == opt_combatRun
		SetInfoText("Select preferred movement state during combat, or do nothing.")
	elseif option == opt_msgVerbose
		SetInfoText("Disable text notifications for mod events and functions.")
	elseif option == opt_detailLog
		SetInfoText("Adds additional details to user log output.")
	endif

endEvent

; --- CORE EVENTS

Event OnUpdate()

	self.UnregisterForUpdate()
	if firstRun
		Initialize()
		return ; Initialize already requested an evaluation
	endif

	SetRunState(PlayerRef)

endEvent

; Key handling, location / combat reactions, shader and message feedback and the
; refresh loop all live in the native plugin (EvaluateRunState / HandleNativeKey).

Function SetRunState(Actor akActor)
	; Kept as the single entry point used by the MCM; the native side decides,
	; applies, plays feedback and schedules its own refresh.
	if akActor != PlayerRef
		LogMsg("Invalid actor - skipping", false)
		return
	endif
	PYS_UtilScript.RequestRunStateEvaluation()
endFunction

Function PushFeedbackConfig()
	PYS_UtilScript.SetFeedbackConfig(PYS_shaderFX, PYS_msgVerbose, PYS_refreshTime, PYS_timeout, PYS_detailLog)
endFunction

Function Initialize()


	PYS_UtilScript.CheckGamepad(self)

	if !PYS_globalToggle || PYS_Active.GetValueInt() == 0
		return
	endif

	PYS_combatStates = new String[3]
	PYS_combatStates[0] = "Do Nothing"
	PYS_combatStates[1] = "Always Run"
	PYS_combatStates[2] = "Always Walk"

	PYS_shaderColorOpts = new String[4]
	PYS_shaderColorOpts[0] = "Disabled"
	PYS_shaderColorOpts[1] = "Red/Green"
	PYS_shaderColorOpts[2] = "Orange/Blue"
	PYS_shaderColorOpts[3] = "Yellow/Purple"

	; Default the MCM keys to the game's own bindings when unset
	if PYS_overrideToggleKey == -1
		PYS_overrideToggleKey = Input.GetMappedKey("Toggle Always Run")
		if PYS_overrideToggleKey == -1
			PYS_overrideToggleKey = 58 ; CAPS LOCK
		endif
	endif
	if PYS_runKey == -1
		PYS_runKey = Input.GetMappedKey("Run")
		if PYS_runKey == -1
			PYS_runKey = 54 ; SHIFT
		endif
	endif

	; Inform native plugin of chosen keys (polled by the native input thread)
	PYS_UtilScript.SetNativeOverrideKey(PYS_overrideToggleKey)
	PYS_UtilScript.SetNativeRunKey(PYS_runKey)

	; Delegate native cache population and initialization to the plugin
	PYS_UtilScript.NativeMCM_Initialize(PlayerRef, PYS_Active, PYS_combatRun, PYS_walkInTowns, PYS_walkInTownsUnwalled, PYS_walkInDungeons, PYS_maxDist, PYS_InteriorWorldspacesFLST, PYS_WalledTownWorldspacesFLST, PYS_ExtraTownKeywordFLST, PYS_ExtraDunKeywordFLST)

	; Key, location and combat reactions are handled natively now; drop the mod
	; event registrations that older versions stored in the save
	UnregisterForModEvent("PYS_NativeKey")
	UnregisterForModEvent("PYS_LocationChanged")
	UnregisterForModEvent("PYS_CombatStateChanged")

	PushFeedbackConfig()

	if firstRun || ModName == ""
		LogMsg("Mod Initialized.")
		firstRun = false
	elseif !firstRun && ModName != ""
		LogMsg("Mod Reinitialized.")
	endif

	SetRunState(PlayerRef)

endFunction

Function LogMsg(string aMsg, bool bPrint = true, bool bLogging = true)

	if bLogging && PYS_detailLog
		debug.TraceUser("PYSRE", Utility.GetCurrentRealTime() + ": " + Modname + ": " + aMsg)
	endif
	if bPrint && PYS_msgVerbose
		debug.notification(Modname + ": " + aMsg)
	endif

endfunction
