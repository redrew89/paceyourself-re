Scriptname PYS_PlayerLoadScript extends ReferenceAlias



PYS_MCMScript Property MCM Auto

Actor Property PlayerRef Auto

event OnPlayerLoadGame()
	Utility.Wait(2)
	if MCM.firstRun
		return ; Let the main initialization thread run
	endif
	MCM.Initialize()
endevent

event OnInit()
	
	if !(MCM as Quest).IsRunning()
		(MCM as Quest).Start()
	endif
	
endevent

Function SystemReset()

		(MCM as Quest).Reset()
		(MCM as Quest).Stop()
		(MCM as Quest).Start()
		
endFunction




