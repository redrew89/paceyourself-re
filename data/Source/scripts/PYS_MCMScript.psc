Scriptname PYS_MCMScript extends ski_configbase conditional

import PYS_UtilScript ; Native functions now defined and implemented with DLL plugin - 2.0 rebuild

Actor Property PlayerRef Auto 

; Global property for mod functionality
GlobalVariable Property PYS_Active Auto

; Tracking spell used with marker tracking method
Spell Property PYS_TrackerSpell Auto

; Quest to track Location center markers
Quest Property PYS_LocationMarkerQuest Auto

Message Property PYS_WalkMsg auto
Message Property PYS_RunMsg auto
Message Property PYS_PauseMsg Auto
Message Property PYS_ResumeMsg Auto


; Visual feedback Effect Shaders
EffectShader Property MuffleFXShader_PYSEnable Auto
EffectShader Property MuffleFXShader_PYSDisable Auto
EffectShader Property MuffleFXShader_PYSEnableAlt Auto
EffectShader Property MuffleFXShader_PYSDisableAlt Auto
EffectShader Property MuffleFXShader_PYSEnableAlt2 Auto
EffectShader Property MuffleFXShader_PYSDisableAlt2 Auto
EffectShader Property MuffleFXShader_PYSChangeIndicator Auto


; Keyword properties for selective behavior
Keyword Property LocTypeClearable Auto
Keyword Property LocTypeCity Auto
Keyword Property LocTypeTown Auto

; Formlist properties that can be manipulated by FLM
Formlist Property PYS_ExtraTownKeywordFLST auto
Formlist Property PYS_ExtraDunKeywordFLST auto
Formlist Property PYS_InteriorWorldspacesFLST Auto
Formlist Property PYS_WalledTownWorldspacesFLST Auto

; Internal booleans for tracker effect script behavior - Exposed as conditional properties for possible use within the plugin
bool Property PYS_walkInTowns = false Auto conditional
bool Property PYS_walkInDungeons = false Auto conditional
bool Property PYS_globalToggle = true Auto conditional
bool Property PYS_walkInTownsUnwalled = false Auto conditional

bool Property PYS_playerOverride = false Auto
bool Property PYS_simulatedKeyPress = false auto conditional
bool Property PYS_detailLog = true auto conditional
bool Property PYS_msgVerbose = true auto conditional

; Internal properties used to manage script behavior
int Property PYS_walkSpeedMod Auto conditional
int Property PYS_walkSpeedBase = 100 Auto conditional
int Property PYS_overrideToggleKey = -1 Auto conditional

int Property PYS_combatRun auto conditional ; 0 - Do nothing , 1 - Always run, 2 - Always walk
int Property PYS_shaderFX auto conditional ; 0 - None, 1 - red/green, 2 - orange/blue, 3 - yellow/purple
String[] PYS_combatStates
String[] PYS_shaderColorOpts 

float Property PYS_maxDist = 6000.0 Auto conditional
float Property PYS_waitTime = 0.1 Auto 
float Property PYS_timeout = 5.0 Auto
float Property PYS_refreshTime = 30.0 Auto
float Property speedModWaitStartTime = 0.0 Auto Hidden

int adjustedSpeed

; Internal variables for toggle options
int opt_walkInTowns
int opt_walkInDungeons 
int opt_walkInTownsUnwalled
int opt_globalToggle 
int opt_walkSpeed
int opt_DelayTime 
int opt_Timeout
int opt_RefreshTime
int opt_overrideToggleKey
int opt_maxDist
int opt_altFX
int opt_msgVerbose
int opt_combatRun
int opt_detailLog

; Flags - to control MCM option display
int flag_walkSpeed
int flag_globalToggle
int flag_maxDist
int flag_keyMap
int flag_walkInTownsExt
int flag_SMCMsg
int flag_disable

; Internal variables for functions
float Property timeout Auto
float Property waitTime Auto
float lastToggleTime = 0.0
float refreshTime 

int autorunKey
int runKey
int sneakKey
int forwardKey
int backKey
int leftKey
int rightKey

bool currentPlayerMode = true 
bool playerIsMoving
bool playerToggled = false


bool Property firstRun = true Auto

; --- MCM OPTIONS

int function GetVersion()
	;return 12  1.2 - Initial release with script versioning
	;return 121 ; 1.2.1 - Hotfix
	;return 122 ; 1.2.2 - Bugfix release
	return 200 ; 2.0.0 - SKSE rework
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
	
	flag_SMCMsg = OPTION_FLAG_HIDDEN
		
	if timeout != self.PYS_timeout
		timeout = self.PYS_timeout
	endif
	if waitTime != self.PYS_waitTime
		waitTime = self.PYS_waitTime
	endif	
	if refreshTime != self.PYS_refreshTime
		refreshTime = self.PYS_refreshTime
	endif

	PYS_walkSpeedBase = PlayerRef.GetBaseActorValue("SpeedMult") as Int
	RegisterForSingleUpdate(refreshTime/3)
	
endEvent

event OnVersionUpdate(int a_version)
	
	;/
	if (a_version >= 12 && CurrentVersion < 12 ) && !firstRun
		LogMsg("Pace Yourself RE: Updating script to version 1.2")
		CurrentVersion = 12
		OnConfigInit()
	endIf
	
	if (a_version >= 121 && CurrentVersion < 121 ) && !firstRun
		LogMsg("Pace Yourself RE: Updating script to version 1.2.1 - Hotfix")
		CurrentVersion = 12
		firstRun = true
		OnConfigInit()
	endIf
	
	if (a_version >= 122 && CurrentVersion < 122 ) && !firstRun
		LogMsg("Pace Yourself RE: Updating script to version 1.2.2")
		CurrentVersion = 12
		firstRun = true
		OnConfigInit()
	endIf
	/;
		
	if (a_version >= 200 && CurrentVersion < 200 ) && !firstRun
		LogMsg("Pace Yourself RE: Updating script to version 2.0.0")
		CurrentVersion = 200
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
			opt_overrideToggleKey = AddKeyMapOption("Manual Override:", PYS_overrideToggleKey, flag_keyMap)			
			AddEmptyOption()
			AddHeaderOption("Internal Functions")
			opt_DelayTime = AddSliderOption("Delay Time:", PYS_waitTime, "{1} seconds")
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
	elseif option == opt_walkSpeed
		PYS_walkSpeedMod = 0
		SetSliderOptionValue(option,PYS_walkSpeedMod,false)
	elseif option == opt_walkInTownsUnwalled
		PYS_walkInTownsUnwalled = false
		SetToggleOptionValue(option, PYS_walkInTownsUnwalled,false)
	elseif option == opt_DelayTime
		PYS_waitTime = 0.1
		waitTime = PYS_waitTime
		SetSliderOptionValue(option, PYS_waitTime, false)
	elseif option == opt_maxDist
		PYS_maxDist = 6000
		SetSliderOptionValue(option, PYS_maxDist,false)
	elseif option == opt_msgVerbose
		PYS_msgVerbose = true
		SetToggleOptionValue(option, PYS_msgVerbose,false)
	elseif option == opt_RefreshTime
		PYS_refreshTime = 30.0
		refreshTime = PYS_refreshTime
		SetToggleOptionValue(option,PYS_refreshTime,false)
	elseif option == opt_DelayTime
		PYS_waitTime = 0.1
		waitTime = 0.1
		SetToggleOptionValue(option, PYS_waitTime,false)
	elseif option == opt_Timeout
		PYS_timeout = 5.0
		timeout = PYS_timeout
		SetToggleOptionValue(option,PYS_timeout,false)
	elseif option == opt_overrideToggleKey
		PYS_overrideToggleKey = -10		
		SetKeymapOptionValue(option,PYS_overrideToggleKey,false)
	endIf
	
	if option == opt_overrideToggleKey
		Initialize()
	endif
	
	LogMsg("Set " + option + " value to default.",false)
	
	ForcePageReset()
	RegisterForSingleUpdate(PYS_timeout)

endEvent

event OnOptionKeyMapChange(int option, int keyCode, string conflictControl, string conflictName)
	
	if (option == opt_overrideToggleKey)
		bool continue = true
		if (conflictControl != "")
			string msg
			if (conflictName != "")
				msg = "This key is already mapped to:\n\"" + conflictControl + "\"\n(" + conflictName + ")\n\nAre you sure you want to continue?"
			else
				msg = "This key is already mapped to:\n\"" + conflictControl + "\"\n\nAre you sure you want to continue?"
			endIf

			continue = ShowMessage(msg, true, "$Yes", "$No")
		endIf

		if (continue)
			PYS_overrideToggleKey = keyCode
			SetKeymapOptionValue(option, keyCode)
			LogMsg("Set override key: " + keyCode)
		endIf
	endIf
	
	ForcePageReset()
	Initialize()
	
endEvent

string function GetCustomControl(int keyCode)
	if (keyCode == PYS_overrideToggleKey)
		return "Override Pace Yourself RE"
	else
		return ""
	endIf
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
	
	if option == opt_walkSpeed
		SetSliderDialogStartValue(PYS_walkSpeedMod as Float)
		SetSliderDialogRange(0.0,100.0)
		SetSliderDialogInterval(1.0)	
	elseif option == opt_DelayTime
		SetSliderDialogStartValue(PYS_waitTime)
		SetSliderDialogRange(0.1,5.0)
		SetSliderDialogInterval(0.1)	
	elseif option == opt_Timeout
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
	
	if option == opt_walkSpeed
		PYS_walkSpeedMod = value as Int		
		SetSliderOptionValue(option, value, "+{1}%")
		adjustedSpeed = PYS_walkSpeedBase + PYS_walkSpeedMod
	elseif option == opt_DelayTime
		PYS_waitTime = value
		SetSliderOptionValue(option, value, "{1} seconds")
	elseif option == opt_Timeout
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
	elseif option == opt_walkSpeed && flag_walkSpeed == OPTION_FLAG_NONE
		SetInfoText("Adjust walking speed for player.")
	elseif option == opt_DelayTime
		SetInfoText("Adjust time delay for keypress attempts.")
	elseif option == opt_Timeout
		SetInfoText("Adjust the timeout for toggle attempts.")
	elseif option == opt_RefreshTime
		SetInfoText("Adjust how often the system refreshes.")
	elseif option == opt_walkInTownsUnwalled
		SetInfoText("Disable to automatically switch to walking in non-walled towns in exterior worldspaces (e.g., Riverwood, Morthal, etc.)")
	elseif option == opt_maxDist
		SetInfoText("Maximum distance from center of town locations to continue walking, while Include Unwalled Towns is enabled.")
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
	endif
	
	if PYS_simulatedKeyPress
		PYS_simulatedKeyPress = false
		return
	endif
	
	; When a setting changes, call this instead of just UpdateNativeConfig
	DebugMCMSettings(PYS_Active, PYS_combatRun, PYS_walkInTowns, PYS_walkInTownsUnwalled, PYS_walkInDungeons, PYS_maxDist)
	CheckGamepad()
	Utility.Wait(0.5)
	SetRunState(PlayerRef)
	
endEvent

Event OnKeyDown(int akKey)
	
	if Utility.IsInMenuMode()
		;Discard input, player is in menu
		LogMsg("Ignoring input while menu is open.",false)
		return
	endif
		
	if akKey == runKey 
		PYS_playerOverride = true
	endif
		
endevent

Event OnKeyUp(int akKey, float holdTime)
	
	if Utility.IsInMenuMode()
		;Discard input, player is in menu
		LogMsg("Ignoring input while menu is open.",false)
		return
	endif
	
	if (akKey == PYS_overrideToggleKey)
		PYS_playerOverride = true ; Setting this true now until we can determine if it's valid, to avoid a race condition
		SetOverride(akKey)		
	endif	
		
	if akKey == autorunKey
		PYS_playerOverride = true
		SetOverride(akKey)	
	endIf
		
endEvent


Function SetRunState(Actor akActor)

	; Early validation
	if akActor != Game.GetPlayer()
		LogMsg("Invalid actor - skipping", false)
		return
	endif
	
	if self.PYS_playerOverride || Input.IsKeyPressed(runKey)
		LogMsg("Player override active - bypassing", false)
		self.RegisterForSingleUpdate(PYS_refreshTime)
		return
	endif
	
	; Cache frequently used values
	bool currentRunState = Game.GetPlayerMovementMode()
	bool targetRunState = ShouldRunHere()
	
	; Early exit if no change needed or player is sneaking
	if akActor.IsSneaking() || (targetRunState == currentRunState)
		self.RegisterForSingleUpdate(refreshTime)
		return
	endif
	
	; Cooldown check
	float currentTime = Utility.GetCurrentRealTime()
	float cooldownPeriod = PYS_timeout / 4
	if (currentTime - lastToggleTime) < cooldownPeriod  && !PlayerRef.GetCombatState() == 0 && !PlayerRef.IsWeaponDrawn() ; Adding checks for combat and weapon states, to skip cooldown.
		LogMsg("Cooldown active - deferring toggle", false)
		self.RegisterForSingleUpdate(PYS_timeout/2)
		return
	endif
	lastToggleTime = currentTime
	
	if PYS_shaderFX != 0
		MuffleFXShader_PYSChangeIndicator.Play(akActor,1)
	endif
	
	if PYS_msgVerbose
		if targetRunState
			PYS_RunMsg.Show()
		else
			PYS_WalkMsg.Show()
		endif
	endif
	
	; Perform the actual movement state change
	HandleMovementLogic()
	
	; Schedule next update
	self.RegisterForSingleUpdate(refreshTime)
	
endFunction

Function SetOverride(int aKey = -1)

	LogMsg("Setting player override in response to Keypress.",false)
	
	if aKey != -1 && aKey == PYS_overrideToggleKey		
		Input.HoldKey(autorunKey)
		Utility.Wait(waitTime)
		Input.ReleaseKey(autorunKey)		
	endIf

	; Player manually toggled autorun with override key - set override logic
	currentPlayerMode = Game.GetPlayerMovementMode()
	
	bool setRun = ShouldRunHere() 
	
	LogMsg("simulated key press: " + self.PYS_simulatedKeyPress, false)
	LogMsg("Player toggle matches script: " + (currentPlayerMode == setRun), false)
	bool isOverridden = currentPlayerMode != setRun
	
	
	if isOverridden ; Set override if player's choice conflicts with script preference
	
		self.PYS_playerOverride = isOverridden			
		if PYS_shaderFX == 1
			MuffleFXShader_PYSDisable.Play(PlayerRef,2)			
		elseif PYS_shaderFX == 2
			MuffleFXShader_PYSDisableAlt.Play(PlayerRef,2)			
		elseif PYS_shaderFX == 3
			MuffleFXShader_PYSDisableAlt2.Play(PlayerRef,2)			
		endif
		
		LogMsg("Player manually overrode script preference", false)
		if PYS_msgVerbose
			PYS_PauseMsg.Show()
		endif
		
	else ;Disable override if player's choice matches with script preference
		self.PYS_playerOverride = isOverridden
		
		if PYS_shaderFX == 1
			MuffleFXShader_PYSEnable.Play(PlayerRef,2)			
		elseif PYS_shaderFX == 2
			MuffleFXShader_PYSEnableAlt.Play(PlayerRef,2)			
		elseif PYS_shaderFX == 3
			MuffleFXShader_PYSEnableAlt2.Play(PlayerRef,2)			
		endif				
	
		LogMsg("Player choice aligns with script preference", false)
		if PYS_msgVerbose
			PYS_ResumeMsg.Show()
		endif
	endif

	
endfunction


Function ResetMarkerQuest()

	PYS_LocationMarkerQuest.Reset()
	PYS_LocationMarkerQuest.Stop()
	PYS_LocationMarkerQuest.Start()
	UpdateLocationMarker((PYS_LocationMarkerQuest.GetAlias(2) as ReferenceAlias).GetReference() as ObjectReference)

endFunction

Function Initialize()
	
	
	PYS_UtilScript.CheckGamepad()
		
	if PYS_globalToggle && PYS_Active.GetValueInt() == 0
		return
	endif

	if timeout != self.PYS_timeout
		timeout = self.PYS_timeout
	endif
	if waitTime != self.PYS_waitTime
		waitTime = self.PYS_waitTime
	endif	
	if refreshTime != self.PYS_refreshTime
		refreshTime = self.PYS_refreshTime
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
	
	autorunKey = Input.GetMappedKey("Toggle Always Run")
	runKey = Input.GetMappedKey("Run")
	sneakKey = Input.GetMappedKey("Sneak")
	forwardKey = Input.GetMappedKey("Forward")
	backKey = Input.GetMappedKey("Back")
	leftKey = Input.GetMappedKey("Strafe Left")
	rightKey = Input.GetMappedKey("Strafe Right")
	
	if runKey != -1
		self.RegisterForKey(runKey)
	endif
	if autorunKey != -1
		self.RegisterForKey(autorunKey)
	endif
	if sneakKey != -1
		self.RegisterForKey(sneakKey)
	endif
	if forwardKey != -1
		self.RegisterForKey(forwardKey)
	endif
	if backKey != -1
		self.RegisterForKey(backKey)
	endif
	if leftKey != -1
		self.RegisterForKey(leftKey)
	endif
	if rightKey != -1
		self.RegisterForKey(rightKey)
	endif
	if PYS_overrideToggleKey != -1
		self.RegisterForKey(PYS_overrideToggleKey)
	endif
		
	ResetMarkerQuest()
	
	if PYS_detailLog
		LogMsg("Initialization Debug Output:", false)
		int i = 0
		int count = (PYS_shaderColorOpts.Length - 1)
		while i <= count
			LogMsg("PYS_shaderColorOpts:" + i + " - " + PYS_shaderColorOpts[i],false)
			i += 1
		endwhile
		i = 0
		count = (PYS_combatStates.Length - 1)
		while i <= count
			LogMsg("PYS_combatStates:" + i + " - " + PYS_combatStates[i],false)
			i += 1
		endwhile	
	endif
	
	InitializeNativeSystem(PYS_Active, PYS_combatRun, PYS_walkInTowns, PYS_walkInTownsUnwalled, PYS_walkInDungeons, PYS_maxDist, PYS_InteriorWorldspacesFLST, PYS_WalledTownWorldspacesFLST, PYS_ExtraTownKeywordFLST, PYS_ExtraDunKeywordFLST)
	
	; Call this to see what's happening
	TestNativeFunctions()
	
	if firstRun || ModName == "" 
		LogMsg("Mod Initialized.")
		firstRun = false
	elseif !firstRun && ModName != ""
		LogMsg("Mod Reinitialized.")		
	endif
	
	if autorunKey == -1
		LogMsg("Toggle Always Run key is unmapped.", false)
		debug.MessageBox(self.ModName + ": Toggle Auto Run is not set.")
		return
	endif
	
		
	self.RegisterForSingleUpdate(timeout)
	
endfunction

Function LogMsg(string aMsg, bool bPrint = true, bool bLogging = true) 
				
	if bLogging && PYS_detailLog
		debug.TraceUser("PYSRE", Utility.GetCurrentRealTime() + ": " + Modname + ": " + aMsg)
	endif
	if bPrint && PYS_msgVerbose
		debug.notification(Modname + ": " + aMsg)
	endif

endfunction