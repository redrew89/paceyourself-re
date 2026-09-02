Function SetGamepadWalkRunState(bool shouldRun) Global
    ;/
    Handles toggling a keypress with retry logic to ensure SMC picks up the input.
    Uses multiple taps with feedback validation to overcome event queue timing issues.
    /;

    PYS_MCMScript MCM = Game.GetFormFromFile(2817, "PaceYourself.esp") as PYS_MCMScript

    if !MCM
        return
    endif

    if !MCM.bHasSMC
        MCM.LogMsg("SetGamepadWalkRunState: SMC not available", false)
        return
    endif

    ; Get current gamepad state
    bool currentState = GetPlayerWalkRunState()

    ; Only proceed if state needs to change
    if currentState == shouldRun
        MCM.LogMsg("SetGamepadWalkRunState: State already " + shouldRun + ", no toggle needed", false)
        return
    endif

    ; Attempt to toggle with retry logic
    int maxAttempts = 3
    int attempt = 0
    float retryDelay = 0.05

    MCM.LogMsg("SetGamepadWalkRunState: Attempting to toggle to " + shouldRun, false)

    int i = 0
    while i < maxAttempts
        Input.Tap(MCM.PYS_overrideToggle)
        Utility.Wait(retryDelay)

        ; Check if state changed
        bool newState = GetPlayerWalkRunState()
        if newState == shouldRun
            MCM.LogMsg("SetGamepadWalkRunState: Success on attempt " + (i + 1), false)
            return
        endif

        i += 1
    endwhile

    ; Log failure after all retries exhausted
    MCM.LogMsg("SetGamepadWalkRunState: FAILED after " + maxAttempts + " attempts. Target: " + shouldRun + ", Actual: " + GetPlayerWalkRunState(), false)
EndFunction