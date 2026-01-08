Scriptname LockBashing_GripScript extends activemagiceffect  

GlobalVariable Property LockBashing_StrengthMod Auto
Bool Property bDetrimental Auto
Actor Property PlayerRef Auto

Float magnitude

Event OnEffectStart(Actor akTarget, Actor akCaster)
    If akTarget == PlayerRef
        magnitude = GetMagnitude()
        If bDetrimental
            magnitude *= -1
        EndIf
        LockBashing_StrengthMod.Mod(magnitude)
    EndIf
EndEvent

Event OnEffectFinish(Actor akTarget, Actor akCaster)
    If akTarget == PlayerRef
        LockBashing_StrengthMod.Mod(magnitude * -1)
    EndIf
EndEvent