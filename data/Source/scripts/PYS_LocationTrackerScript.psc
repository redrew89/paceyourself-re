Scriptname PYS_LocationTrackerScript extends activemagiceffect  

PYS_MCMScript Property MCM Auto
ObjectReference Property PYS_trackerMarker Auto

Event OnEffectStart(actor aktarget, actor akcaster)
	Utility.Wait(0.1) ; Required.
	PYS_trackerMarker.MoveTo(akTarget)
	if MCM.firstRun
		return ; Do nothing. Let normal initialization flow occur	
	endif
	MCM.ResetMarkerQuest()
	Utility.Wait(MCM.PYS_waitTime)
	MCM.SetRunState(akTarget)
endevent