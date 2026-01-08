Scriptname LockBashing_Script Extends Quest

ObjectReference Property LockBashing_CrimeTriggerRef Auto
GlobalVariable[] Property ContainerCrimeGold Auto
GlobalVariable[] Property DoorCrimeGold Auto
Actor Property PlayerRef Auto
EffectShader Property ShaderFX Auto
{Effect played when unlocking the object through a projectile.}
String[] Property sXPMult Auto

Function UpdateCrosshairs() Global Native

ObjectReference Function GetLinkedDoor(ObjectReference akRef) Global Native

Bool Function GetAutoOpen() Global Native

String Function GetIniValue(String asSection, String asOptionName) Global Native

; received when bashing lock open with a weapon
Function BashOpen(ObjectReference akRef, Weapon akWeapon)
    MaybeGiveXP(akRef, akWeapon)
    UnlockRef(akRef)
EndFunction

; received when bashing lock open with a projectile
Function ProjectileOpen(ObjectReference akRef, Projectile akProjectile)
    ShaderFX.Play(akRef, 2)
    UnlockRef(akRef)
EndFunction

Function UnlockRef(ObjectReference akRef)
    MagicCrimeAlarm(akRef)
    akRef.Lock(False)
    UpdateCrosshairs()
    If akRef.GetBaseObject() as Container && GetAutoOpen()
        akRef.Activate(PlayerRef)
    EndIf
Endfunction

Faction Function GetOwner(ObjectReference akRef)
    Faction owner = akRef.GetFactionOwner()
    If ! owner && akRef.GetBaseObject() as Door
        ObjectReference linkedDoor = GetLinkedDoor(akRef)
        If linkedDoor
            owner = linkedDoor.GetFactionOwner()
        EndIf
    EndIf
    If ! owner
        owner = akRef.GetParentCell().GetFactionOwner()
    EndIf

    Return owner
Endfunction

Function MagicCrimeAlarm(ObjectReference akRef)
    Int crimeGold = GetCrimeGold(akRef)
    If (CrimeGold > 0)
        Faction owner = GetOwner(akRef)
        If owner
            LockBashing_CrimeTriggerRef.MoveTo(PlayerRef)
            LockBashing_CrimeTriggerRef.GetBaseObject().SetGoldValue(crimeGold)
            LockBashing_CrimeTriggerRef.SetFactionOwner(owner)
            LockBashing_CrimeTriggerRef.SendStealAlarm(PlayerRef)
        EndIf
    EndIf
    akRef.CreateDetectionEvent(PlayerRef, 100)
EndFunction

Int Function GetCrimeGold(ObjectReference akRef)
    Int lockLevel = SanitizeLockLevel(akRef)
    If akRef.GetBaseObject() as Container
        Return ContainerCrimeGold[lockLevel].GetValueInt()
    Else
        Return DoorCrimeGold[lockLevel].GetValueInt()
    EndIf
EndFunction

int function SanitizeLockLevel(ObjectReference lock)
	int level = lock.GetLockLevel()

	if !lock.IsLocked()
		return -1
	endif

	if level == 0 || level == 1 ; novice
		return 0
	elseif level >= 2 && level <= 25 ; Apprentice
		return 1
	elseif level >= 26 && level <= 50 ; Adept
		return 2
	elseif level >= 51 && level <= 75 ; Expert
		return 3
	elseif level >= 76 && level <= 254 ; Master
		return 4
	else
		return 5
	endif
endfunction

Function MaybeGiveXP(ObjectReference akRef, Weapon akWeapon)
    Float xp = GetIniValue("XP", "fXP") as Float
    If xp > 0
        Int level = SanitizeLockLevel(akRef)
        Game.AdvanceSkill(akWeapon.GetSkill(), xp * (GetIniValue("XP", sXPMult[level]) as Float))
    EndIf
    Float lockpickXP = GetIniValue("XP", "fLockpickingXP") as Float
    If lockpickXP > 0
        Int level = SanitizeLockLevel(akRef)
        Game.AdvanceSkill("Lockpicking", lockpickXP * (GetIniValue("XP", sXPMult[level]) as Float))
    EndIf
Endfunction