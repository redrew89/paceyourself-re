Scriptname PYS_PlayerWeaponScript extends activemagiceffect  


PYS_MCMScript Property MCM Auto

Event OnEffectStart(actor aktarget, actor akcaster)

	if MCM.PYS_combatRun != 0
		MCM.SetRunState(aktarget)
	endif
	
endEvent


Event OnEffectFinish(actor aktarget, actor akcaster)

	if MCM.PYS_combatRun != 0
		MCM.SetRunState(aktarget)
	endif
	
endEvent