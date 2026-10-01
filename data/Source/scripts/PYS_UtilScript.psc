Scriptname PYS_UtilScript Hidden

; =======================
; Native Function Declarations
; =======================

string Function GetPluginVersion() Global Native

; Configuration functions
Function SetMovementConfig(bool modActive, int combatRun, bool walkInTowns, bool walkInTownsUnwalled, bool walkInDungeons, float maxDist) Global Native

; Populates native config and form caches from the MCM's FormLists, and enables the native reaction pipeline
Function NativeMCM_Initialize(Actor PlayerRef, GlobalVariable PYS_Active, int combatRunSetting, bool walkInTowns, bool walkInTownsUnwalled, bool walkInDungeons, float maxDistance, FormList interiorWorldspaces = None, FormList walledTownWorldspaces = None, FormList extraTownKeywords = None, FormList extraDunKeywords = None) Global Native

; Keys polled by the native input thread
Function SetNativeOverrideKey(int keyCode) Global Native
Function SetNativeRunKey(int keyCode) Global Native

; Native reaction pipeline - shaders, messages, cooldown and refresh scheduling
; shaderFX: 0=disabled, 1=red/green, 2=orange/blue, 3=yellow/purple
Function SetFeedbackConfig(int shaderFX, bool msgVerbose, float refreshTime, float timeout, bool detailLog) Global Native
Function RequestRunStateEvaluation() Global Native

; =======================
; Helper Functions
; =======================

; Update configuration at runtime (for MCM changes)
Function UpdateNativeConfig(GlobalVariable PYS_Active, int combatRunSetting, bool walkInTowns, bool walkInTownsUnwalled, bool walkInDungeons, float maxDistance) Global
	SetMovementConfig(PYS_Active.GetValueInt() != 0, combatRunSetting, walkInTowns, walkInTownsUnwalled, walkInDungeons, maxDistance)
EndFunction

Function CheckGamepad(PYS_MCMScript MCM) Global

	if Game.UsingGamepad()
		MCM.LogMsg("Gamepad detected.")
		if SKSE.GetPluginVersion("SkyrimMotionControl") != -1
			MCM.LogMsg("SMC detected. Set gamepad walkstate in SMC Beta SKSE Menu Framework.")
			MCM.PYS_hasSMC = true
		else
			MCM.LogMsg("Shutting down. Restart without gamepad to resume.")
			MCM.PYS_Active.SetValueInt(0)
			MCM.PYS_globalToggle = false
			MCM.ToggleModFlag(false)
		endif
	else
		MCM.PYS_Active.SetValueInt(1)
		MCM.PYS_globalToggle = true
		MCM.ToggleModFlag(true)
	endif

	UpdateNativeConfig(MCM.PYS_Active, MCM.PYS_combatRun, MCM.PYS_walkInTowns, MCM.PYS_walkInTownsUnwalled, MCM.PYS_walkInDungeons, MCM.PYS_maxDist)

endFunction
