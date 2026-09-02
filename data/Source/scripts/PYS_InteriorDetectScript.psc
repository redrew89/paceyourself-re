Scriptname PYS_InteriorDetectScript extends activemagiceffect  

PYS_MCMScript Property MCM Auto

Event OnMagicEffectStart(Actor akTarget, Actor akCaster)

	if akTarget == Game.GetPlayer()
		MCM.SetRunState(akTarget)
	endif

EndEvent

Event OnMagicEffectEnd()

	
	MCM.SetRunState(GetTargetActor())
	

EndEvent