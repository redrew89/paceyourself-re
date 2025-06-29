Scriptname PYS_MarkerTracker extends ObjectReference

PYS_MCMScript Property MCM Auto

Event OnCellDetach()
	Utility.Wait(0.5) ;maybe not necessary
	MoveTo(MCM.PlayerRef)
	if MCM.firstrun
		return ; Do nothing on first run -- Let initialization flow run
	endif
	MCM.ResetMarkerQuest()
	Utility.Wait(MCM.PYS_waitTime)
	MCM.SetRunState(MCM.PlayerRef)	
EndEvent