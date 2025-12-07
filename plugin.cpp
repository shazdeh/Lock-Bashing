#include <SimpleIni.h>

using namespace SKSE;
using namespace RE;

BGSListForm* breakableDoors;
BGSListForm* breakableContainers;
BGSListForm* excludedWeapons;
PlayerCharacter* player;
TESForm* questForm;
TESForm* requiredPerk;
BGSListForm* validProjectiles[5];
using IniData = std::unordered_map<std::string, std::unordered_map<std::string, std::string>>;
IniData config;
static GPtr<IMenu> hudMenu;
std::string containerName;
TESGlobal* strengthMod;

// config
int requiredPower[5];
bool allowBashAttack;
float fPowerMult;
float fPowerHealthMult;
float fPowerStaminaMult;
bool bAllLocks = false;
static float weaponMults[] = {
    1.0f,
    1.0f,
    1.0f,
    1.0f,
    1.0f,
    1.0f,
    1.0f,
    1.0f,
    0.0f, // staves, not used
    1.0f,
    1.0f // warhammer
};
bool bAutoOpen = true; /* kept for backward compatibility */

void UpdateWidget(int state) {
    GFxValue rootMenu;
    if (hudMenu->uiMovie->GetVariable(&rootMenu,
                                      ("_root.HUDMovieBaseInstance." + containerName + ".Menu_mc").c_str())) {
        GFxValue args[1];
        args[0] = GFxValue(state);
        rootMenu.Invoke("setState", nullptr, args, 1);
    }
}

void LoadIni(const char* filename, IniData& outData) {
    CSimpleIniA ini;
    ini.SetUnicode();
    ini.SetMultiKey(false);  // assuming you don't want duplicate keys

    SI_Error rc = ini.LoadFile(filename);
    if (rc < 0) {
        throw std::runtime_error("Failed to load INI file");
    }

    CSimpleIniA::TNamesDepend sections;
    ini.GetAllSections(sections);

    for (auto& sec : sections) {
        // lowercased section name
        std::string section = sec.pItem;
        std::transform(section.begin(), section.end(), section.begin(), ::tolower);

        CSimpleIniA::TNamesDepend keys;
        ini.GetAllKeys(sec.pItem, keys);

        for (auto& key : keys) {
            std::string keyName = key.pItem;
            std::transform(keyName.begin(), keyName.end(), keyName.begin(), ::tolower);

            const char* value = ini.GetValue(sec.pItem, key.pItem, "");
            outData[section][keyName] = value ? value : "";
        }
    }
}

std::string GetIni(const std::string& section, const std::string& key) {
    // normalize to lowercase, because when we interface with Papyrus the casing is unreliable
    std::string sec = section;
    std::string opt = key;

    std::transform(sec.begin(), sec.end(), sec.begin(), ::tolower);
    std::transform(opt.begin(), opt.end(), opt.begin(), ::tolower);

    auto sit = config.find(sec);
    if (sit == config.end()) return "";

    const auto& inner = sit->second;
    auto kit = inner.find(opt);
    if (kit == inner.end()) return "";

    return kit->second;
}

template <class... Args>
void CallQuestScript(TESForm* questForm, std::string_view scriptName, std::string_view methodName, Args&&... args) {
    static auto* vm = BSScript::Internal::VirtualMachine::GetSingleton();
    static auto* policy = vm->GetObjectHandlePolicy();

    VMHandle handle = policy->GetHandleForObject(questForm->GetFormType(), questForm);
    if (handle == policy->EmptyHandle()) {
        return;
    }

    BSTSmartPointer<BSScript::Object> papyrusObject;
    BSTSmartPointer<BSScript::IStackCallbackFunctor> callback;

    if (!vm->FindBoundObject(handle, scriptName.data(), papyrusObject)) {
        return;
    }

    auto packed = MakeFunctionArguments(std::forward<Args>(args)...);

    vm->DispatchMethodCall1(papyrusObject, methodName, packed, callback);
}

bool HasPerks() {
    if (!requiredPerk) {
        return true;
    }
    if (requiredPerk->Is(FormType::Perk)) {
        if (player->HasPerk(static_cast<BGSPerk*>(requiredPerk))) {
            return true;
        }
    } else if (requiredPerk->Is(FormType::FormList)) {
        bool result = false;
        requiredPerk->As<BGSListForm>()->ForEachForm([&](TESForm& listItem) {
            BGSPerk* perk = listItem.As<BGSPerk>();
            if (perk && player->HasPerk(perk)) {
                result = true;
                return BSContainer::ForEachResult::kStop;
            }
            return BSContainer::ForEachResult::kContinue;
        });

        return result;
    }

    return false;
}

float GetPlayerStrength() {
    ActorValueOwner* avo = player->AsActorValueOwner();
    float strength = 0.0f;
    if (avo) {
        strength = fPowerMult * ((avo->GetActorValue(ActorValue::kHealth) * fPowerHealthMult) +
                                    (fPowerStaminaMult * avo->GetActorValue(ActorValue::kStamina)));
    }
    return strength;
}

bool IsValidWeapon(TESObjectWEAP* weapon) {
    if (weapon && !excludedWeapons->HasForm(weapon)) {
        return true;
    }
    return false;
}

bool IsObjectBreakable(TESBoundObject* form) {
    if (bAllLocks) {
        return true;
    } else if ((form->Is(FormType::Door) && breakableDoors->HasForm(form)) ||
        (form->Is(FormType::Container) && breakableContainers->HasForm(form))) {
        return true;
    }
    return false;
}

float GetWeaponPowerMult(RE::TESObjectWEAP* weapon) {
    auto type = weapon->GetWeaponType();
    switch (type) {
        case WeaponTypes::kTwoHandAxe:
            // I think GetWeaponType() uses weapon's AnimType, since warhammers use the same animation as battleaxes?
            if (weapon->HasKeywordString("WeapTypeWarhammer")) {
                return weaponMults[10];
            } else {
                return weaponMults[type];
            }
        default:
            return weaponMults[type];
    }
}

float GetEquippedWeaponMult(bool a_leftHand) {
    auto* equipment = player->GetEquippedObject(a_leftHand);
    if (auto* weaponForm = equipment ? equipment->As<RE::TESObjectWEAP>() : nullptr;
        IsValidWeapon(weaponForm)) {
        return GetWeaponPowerMult(weaponForm);
    }
    return 0.0f;
}

bool CanBreak(int lockLevel, TESBoundObject* target, FormID source) {
    if (requiredPower[lockLevel] && HasPerks() && IsObjectBreakable(target)) {
        TESObjectWEAP* weapon;
        float weaponMult = 0.0f;
        if (source == 0) {
            // get the weapon equipped in either hand, find which one player is more likely to succeed (=> mult is lower)
            weaponMult = GetEquippedWeaponMult(false);
            float leftHandMult = GetEquippedWeaponMult(true);
            if (leftHandMult > 0 && (leftHandMult < weaponMult || weaponMult == 0)) {
                weaponMult = leftHandMult;
            }
        } else {
            weapon = TESForm::LookupByID<TESObjectWEAP>(source);
            if (IsValidWeapon(weapon)) {
                weaponMult = GetWeaponPowerMult(weapon);
            }
        }
        float strength = GetPlayerStrength() * weaponMult;
        strength += strengthMod->value;
        if (strength != 0 && strength >= requiredPower[lockLevel]) {
            return true;
        }
    }

    return false;
}

struct theSink : public BSTEventSink<TESHitEvent>, public BSTEventSink<SKSE::CrosshairRefEvent> {
    BSEventNotifyControl ProcessEvent(const TESHitEvent* event, BSTEventSource<TESHitEvent>*) {
        if (!event->cause || !event->cause->IsPlayerRef() || !event->target ||
            (!allowBashAttack && event->flags.any(TESHitEvent::Flag::kBashAttack))) {
            return BSEventNotifyControl::kContinue;
        }

        auto base = event->target->GetBaseObject();
        if (base && (base->Is(FormType::Door) || base->Is(FormType::Container))) {
            int lockLevel = static_cast<int>(event->target->GetLockLevel());
            if (lockLevel >= 0 && lockLevel <= 4) {
                if (event->projectile) {
                    if (validProjectiles[lockLevel]->HasForm(event->projectile)) {
                        CallQuestScript(questForm, "Lockbashing_Script", "ProjectileOpen", event->target.get(),
                                        TESForm::LookupByID<BGSProjectile>(event->projectile));
                    }
                } else if (CanBreak(lockLevel, base, event->source)) {
                    CallQuestScript(questForm, "Lockbashing_Script", "BashOpen", event->target.get(), TESForm::LookupByID<TESObjectWEAP>(event->source));
                }
            }
        }

        return BSEventNotifyControl::kContinue;
    }

    BSEventNotifyControl ProcessEvent(const SKSE::CrosshairRefEvent* event,
                                             BSTEventSource<SKSE::CrosshairRefEvent>*) {
        static int lastState = 0;
        int newState = 0;
        auto ref = event->crosshairRef;
        if (ref) {
            int lockLevel = static_cast<int>(ref->GetLockLevel());
            if (lockLevel >= 0 && lockLevel <= 4) {
                if (auto base = ref ? ref->GetBaseObject() : nullptr) {
                    if (CanBreak(lockLevel, base, 0)) {
                        newState = 1;
                    }
                }
            }
        }

        if (newState != lastState) {
            UpdateWidget(newState);
            lastState = newState;
        }

        return BSEventNotifyControl::kContinue;
    }
};

bool SetConfig() {
    try {
        LoadIni("Data/SKSE/Plugins/LockBashing.ini", config);
        requiredPower[0] = stoi(GetIni("Main", "iRequiredPowerNovice"));
        requiredPower[1] = stoi(GetIni("Main", "iRequiredPowerApprentice"));
        requiredPower[2] = stoi(GetIni("Main", "iRequiredPowerAdept"));
        requiredPower[3] = stoi(GetIni("Main", "iRequiredPowerExpert"));
        requiredPower[4] = stoi(GetIni("Main", "iRequiredPowerMaster"));
        bAllLocks = GetIni("Main", "bAllLocks") == "1";
        fPowerMult = stof(GetIni("Main", "fPowerMult"));
        fPowerHealthMult = stof(GetIni("Main", "fPowerHealthMult"));
        fPowerStaminaMult = stof(GetIni("Main", "fPowerStaminaMult"));
        if (GetIni("Main", "sRequiredPerk") != "") {
            requiredPerk = TESForm::LookupByEditorID(GetIni("Main", "sRequiredPerk"));
        }
        if (GetIni("Widget", "bEnableHudWidget") == "1") {
            containerName = fmt::format("LockBashing_{}_{}_{}", GetIni("Widget", "iHudWidgetScale"),
                                        GetIni("Widget", "iHudWidgetXOffset"), GetIni("Widget", "iHudWidgetYOffset"));
        }
        if (GetIni("Main", "bAutoOpenContainers") == "0") {
            bAutoOpen = false;
        }
        std::size_t i = 0;
        for (const auto& value : {"fUnarmedMult", "fSwordMult", "fDaggerMult", "fWarAxeMult", "fMaceMult", "fGreatswordMult",
              "fBattleaxeMult", "fBowMult", "", "fCrossbowMult", "fWarhammerMult"}) {
            if (GetIni("Weapons", value) != "") {
                weaponMults[i] = stof(GetIni("Weapons", value));
            }
            i++;
        }
        return true;
    } catch (const std::exception& e) {
        ConsoleLog::GetSingleton()->Print(fmt::format("Lockbashing error: {}", e.what()).c_str());
        return false;
    }
}

void GrabForms() {
    breakableDoors = TESForm::LookupByEditorID<BGSListForm>("LockBashing_BreakableDoors");
    breakableContainers = TESForm::LookupByEditorID<BGSListForm>("LockBashing_BreakableContainers");
    excludedWeapons = TESForm::LookupByEditorID<BGSListForm>("LockBashing_ExcludedWeapons");
    validProjectiles[0] = TESForm::LookupByEditorID<BGSListForm>("LockBashing_ProjectilesNovice");
    validProjectiles[1] = TESForm::LookupByEditorID<BGSListForm>("LockBashing_ProjectilesApprentice");
    validProjectiles[2] = TESForm::LookupByEditorID<BGSListForm>("LockBashing_ProjectilesAdept");
    validProjectiles[3] = TESForm::LookupByEditorID<BGSListForm>("LockBashing_ProjectilesExpert");
    validProjectiles[4] = TESForm::LookupByEditorID<BGSListForm>("LockBashing_ProjectilesMaster");
    strengthMod = TESForm::LookupByEditorID<TESGlobal>("LockBashing_StrengthMod");
}

static theSink* g_eventSink;

void InjectWidget() {
    if (containerName == "") { /* bEnableHudWidget is false */
        return;
    }

    auto ui = UI::GetSingleton();
    if (!ui) return;

    hudMenu = ui->GetMenu(HUDMenu::MENU_NAME);
    if (!hudMenu || !hudMenu->uiMovie) {
        return;
    }

    GFxValue rootMenu;
    if (hudMenu->uiMovie->GetVariable(&rootMenu, "_root.HUDMovieBaseInstance")) {
        GFxValue args[2];
        args[0] = GFxValue(containerName);
        args[1] = GFxValue(8541);
        rootMenu.Invoke("createEmptyMovieClip", nullptr, args, 2);
    }
    if (hudMenu->uiMovie->GetVariable(&rootMenu, ("_root.HUDMovieBaseInstance." + containerName).c_str())) {
        GFxValue args[1];
        args[0] = GFxValue("lockbashing_inject.swf");
        rootMenu.Invoke("loadMovie", nullptr, args, 1);
    }

    SKSE::GetCrosshairRefEventSource()->AddEventSink(g_eventSink);
}

void UpdateCrosshairs(StaticFunctionTag*) { player->UpdateCrosshairs(); }

TESObjectREFR* GetLinkedDoor(StaticFunctionTag*, TESObjectREFR* door) {
    if (!door) {
        return nullptr;
    }
    return door->extraList.GetTeleportLinkedDoor().get().get();
}

bool GetAutoOpen(StaticFunctionTag*) { return bAutoOpen; }

BSFixedString GetIniValue(StaticFunctionTag*, BSFixedString section, BSFixedString key) {
    std::string sec(section.c_str());
    std::string opt(key.c_str());

    std::string value = GetIni(sec, opt);

    return BSFixedString(value.c_str());
}

bool PapyrusBinder(BSScript::IVirtualMachine* vm) {
    vm->RegisterFunction("UpdateCrosshairs", "LockBashing_Script", UpdateCrosshairs);
    vm->RegisterFunction("GetLinkedDoor", "LockBashing_Script", GetLinkedDoor);
    vm->RegisterFunction("GetAutoOpen", "LockBashing_Script", GetAutoOpen);
    vm->RegisterFunction("GetIniValue", "LockBashing_Script", GetIniValue);

    return false;
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);

    SKSE::GetPapyrusInterface()->Register(PapyrusBinder);

    SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* message) {
        if (message->type == SKSE::MessagingInterface::kPostLoadGame ||
            message->type == SKSE::MessagingInterface::kNewGame) {
            static bool initialized = false;
            if (initialized) return;
            initialized = true;

            questForm = TESForm::LookupByEditorID("Lockbashing_Quest");
            player = PlayerCharacter::GetSingleton();
            if (!questForm || !player) return;
            if (!SetConfig()) return;
            GrabForms();
            g_eventSink = new theSink();
            auto* eventSourceHolder = ScriptEventSourceHolder::GetSingleton();
            eventSourceHolder->AddEventSink<TESHitEvent>(g_eventSink);
            
            SKSE::GetTaskInterface()->AddUITask([]() {
                InjectWidget();
            });
        }
    });

    return true;
}