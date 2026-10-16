#include "MtaGameInterface.h"
#include "mta/core/MtaClient.h"
#include "vendor/log/log.h"
#include <dlfcn.h>
#include <cstring>
#include <cmath>

MtaGameInterface& MtaGameInterface::Get() {
    static MtaGameInterface instance;
    return instance;
}

MtaGameInterface::MtaGameInterface()
    : m_initialized(false),
      pfn_FindPlayerPed(nullptr),
      pfn_CWorld_Add(nullptr),
      pfn_CWorld_Remove(nullptr),
      pfn_CEntity_Teleport(nullptr) {
}

bool MtaGameInterface::Initialize() {
    if (m_initialized) return true;

    // Resolve symbols from libGTASA.so
    void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_NOW);
    if (!handle) {
        handle = RTLD_DEFAULT;
    }

    pfn_FindPlayerPed = reinterpret_cast<void*(*)(int)>(dlsym(handle, "_Z13FindPlayerPedi"));
    pfn_CWorld_Add = reinterpret_cast<void(*)(void*)>(dlsym(handle, "_ZN6CWorld3AddEP7CEntity"));
    pfn_CWorld_Remove = reinterpret_cast<void(*)(void*)>(dlsym(handle, "_ZN6CWorld6RemoveEP7CEntity"));
    pfn_CEntity_Teleport = reinterpret_cast<void(*)(void*, CVector, unsigned char)>(dlsym(handle, "_ZN4CPed8TeleportE7CVectorh"));
    if (!pfn_CEntity_Teleport) {
        pfn_CEntity_Teleport = reinterpret_cast<void(*)(void*, CVector, unsigned char)>(dlsym(handle, "_ZN7CEntity8TeleportE7CVectorh"));
    }

    Log("[MTA-GAME] GameInterface initialized. FindPlayerPed: %p, CWorld::Add: %p, Teleport: %p",
        pfn_FindPlayerPed, pfn_CWorld_Add, pfn_CEntity_Teleport);

    m_initialized = true;
    return true;
}

void* MtaGameInterface::GetLocalPlayerPed() {
    if (!m_initialized) Initialize();
    if (pfn_FindPlayerPed) {
        return pfn_FindPlayerPed(-1); // -1 = local player in GTA SA
    }
    return nullptr;
}

bool MtaGameInterface::GetLocalPlayerPosition(CVector& outPos) {
    void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_NOW);
    if (!handle) handle = RTLD_DEFAULT;

    typedef CVector (*FindPlayerCoors_t)(int);
    static FindPlayerCoors_t pfn_FindPlayerCoors = nullptr;
    if (!pfn_FindPlayerCoors) {
        pfn_FindPlayerCoors = reinterpret_cast<FindPlayerCoors_t>(dlsym(handle, "_Z15FindPlayerCoorsi"));
    }

    if (pfn_FindPlayerCoors) {
        outPos = pfn_FindPlayerCoors(0);
        if (outPos.x != 0.0f || outPos.y != 0.0f || outPos.z != 0.0f) {
            return true;
        }
    }

    uint8_t* pPed = reinterpret_cast<uint8_t*>(GetLocalPlayerPed());
    if (!pPed) return false;

    // CEntity placement
#if defined(__aarch64__)
    // 64-bit placement offset: CMatrix* is at offset 0x18
    uint8_t* pMatrix = *reinterpret_cast<uint8_t**>(pPed + 0x18);
    if (pMatrix) {
        outPos = *reinterpret_cast<CVector*>(pMatrix + 0x30);
    } else {
        outPos = *reinterpret_cast<CVector*>(pPed + 0x08);
    }
#else
    // 32-bit placement offset
    uint8_t* pMatrix = *reinterpret_cast<uint8_t**>(pPed + 0x14);
    if (pMatrix) {
        outPos = *reinterpret_cast<CVector*>(pMatrix + 0x30);
    } else {
        outPos = *reinterpret_cast<CVector*>(pPed + 0x04);
    }
#endif
    return true;
}

bool MtaGameInterface::SetLocalPlayerPosition(const CVector& pos) {
    uint8_t* pPed = reinterpret_cast<uint8_t*>(GetLocalPlayerPed());
    if (!pPed) return false;

    if (pfn_CEntity_Teleport) {
        pfn_CEntity_Teleport(pPed, pos, true);
        Log("[MTA-GAME] CEntity::Teleport invoked to (%.2f, %.2f, %.2f)", pos.x, pos.y, pos.z);
        return true;
    }

#if defined(__aarch64__)
    uint8_t* pMatrix = *reinterpret_cast<uint8_t**>(pPed + 0x18);
    if (pMatrix) {
        *reinterpret_cast<CVector*>(pMatrix + 0x30) = pos;
    } else {
        *reinterpret_cast<CVector*>(pPed + 0x08) = pos;
    }
#else
    uint8_t* pMatrix = *reinterpret_cast<uint8_t**>(pPed + 0x14);
    if (pMatrix) {
        *reinterpret_cast<CVector*>(pMatrix + 0x30) = pos;
    } else {
        *reinterpret_cast<CVector*>(pPed + 0x04) = pos;
    }
#endif
    return true;
}

void MtaGameInterface::SpawnLocalPlayer(const CVector& pos) {
    SetLocalPlayerPosition(pos);
    SetLocalPlayerHealth(100.0f);
    SetLocalPlayerArmour(100.0f);

    void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_NOW);
    if (!handle) handle = RTLD_DEFAULT;

    // 1. Finish & delete any active cutscenes (intro sequence, etc.)
    typedef void (*CutsceneSkip_t)();
    CutsceneSkip_t pfn_SkipCutscene = reinterpret_cast<CutsceneSkip_t>(dlsym(handle, "_ZN12CCutsceneMgr12SkipCutsceneEv"));
    if (pfn_SkipCutscene) pfn_SkipCutscene();

    typedef void (*CutsceneFinish_t)();
    CutsceneFinish_t pfn_FinishCutscene = reinterpret_cast<CutsceneFinish_t>(dlsym(handle, "_ZN12CCutsceneMgr14FinishCutsceneEv"));
    if (pfn_FinishCutscene) pfn_FinishCutscene();

    typedef void (*CutsceneDelete_t)();
    CutsceneDelete_t pfn_DeleteCutscene = reinterpret_cast<CutsceneDelete_t>(dlsym(handle, "_ZN12CCutsceneMgr18DeleteCutsceneDataEv"));
    if (pfn_DeleteCutscene) pfn_DeleteCutscene();

    // 2. Restore camera to behind CJ, turn off letterbox black bars, clear fade
    void* pTheCamera = dlsym(handle, "TheCamera");
    if (pTheCamera) {
        typedef void (*CamRestore_t)(void*);
        CamRestore_t pfn_CamRestore = reinterpret_cast<CamRestore_t>(dlsym(handle, "_ZN7CCamera18RestoreWithJumpCutEv"));
        if (pfn_CamRestore) pfn_CamRestore(pTheCamera);

        typedef void (*CamWideOff_t)(void*);
        CamWideOff_t pfn_CamWideOff = reinterpret_cast<CamWideOff_t>(dlsym(handle, "_ZN7CCamera16SetWideScreenOffEv"));
        if (pfn_CamWideOff) pfn_CamWideOff(pTheCamera);

        typedef void (*CamBehind_t)(void*);
        CamBehind_t pfn_CamBehind = reinterpret_cast<CamBehind_t>(dlsym(handle, "_ZN7CCamera48SetCameraDirectlyBehindForFollowPed_CamOnAStringEv"));
        if (pfn_CamBehind) pfn_CamBehind(pTheCamera);

        typedef void (*CamFade_t)(void*, float, short);
        CamFade_t pfn_CamFade = reinterpret_cast<CamFade_t>(dlsym(handle, "_ZN7CCamera4FadeEfs"));
        if (pfn_CamFade) pfn_CamFade(pTheCamera, 0.0f, 1);
    }

    // 3. Restore player controls (unfreeze CJ movement, camera swipe, weapons)
    typedef void (*RestoreControls_t)();
    RestoreControls_t pfn_RestoreControls = reinterpret_cast<RestoreControls_t>(dlsym(handle, "_Z21RestorePlayerControlsv"));
    if (pfn_RestoreControls) pfn_RestoreControls();

    Log("[MTA-GAME] Local player spawned at (%.2f, %.2f, %.2f) in multiplayer mode (cutscenes cleared, controls restored).", pos.x, pos.y, pos.z);
}


float MtaGameInterface::GetLocalPlayerHealth() {
    uint8_t* pPed = reinterpret_cast<uint8_t*>(GetLocalPlayerPed());
    if (!pPed) return 0.0f;

#if defined(__aarch64__)
    return *reinterpret_cast<float*>(pPed + 0x5A0);
#else
    return *reinterpret_cast<float*>(pPed + 0x540);
#endif
}

void MtaGameInterface::SetLocalPlayerHealth(float health) {
    uint8_t* pPed = reinterpret_cast<uint8_t*>(GetLocalPlayerPed());
    if (!pPed) return;

#if defined(__aarch64__)
    *reinterpret_cast<float*>(pPed + 0x5A0) = health;
#else
    *reinterpret_cast<float*>(pPed + 0x540) = health;
#endif
}

float MtaGameInterface::GetLocalPlayerArmour() {
    uint8_t* pPed = reinterpret_cast<uint8_t*>(GetLocalPlayerPed());
    if (!pPed) return 0.0f;

#if defined(__aarch64__)
    return *reinterpret_cast<float*>(pPed + 0x5A4);
#else
    return *reinterpret_cast<float*>(pPed + 0x544);
#endif
}

void MtaGameInterface::SetLocalPlayerArmour(float armour) {
    uint8_t* pPed = reinterpret_cast<uint8_t*>(GetLocalPlayerPed());
    if (!pPed) return;

#if defined(__aarch64__)
    *reinterpret_cast<float*>(pPed + 0x5A4) = armour;
#else
    *reinterpret_cast<float*>(pPed + 0x544) = armour;
#endif
}

void MtaGameInterface::AddEntityToWorld(void* entity) {
    if (!m_initialized) Initialize();
    if (pfn_CWorld_Add && entity) {
        pfn_CWorld_Add(entity);
    }
}

void MtaGameInterface::RemoveEntityFromWorld(void* entity) {
    if (!m_initialized) Initialize();
    if (pfn_CWorld_Remove && entity) {
        pfn_CWorld_Remove(entity);
    }
}

static bool s_newGameTriggered = false;

void MtaGameInterface::ResetGameState() {
    s_newGameTriggered = false;
    void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_NOW);
    if (!handle) handle = RTLD_DEFAULT;
    bool* pSkipIntro = reinterpret_cast<bool*>(dlsym(handle, "SkipIntroCutscene"));
    if (pSkipIntro) {
        *pSkipIntro = false;
    }
}

void MtaGameInterface::ForceGameStart() {
    void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_NOW);
    if (!handle) handle = RTLD_DEFAULT;

    // Instruct engine to skip the story intro cutscene in MTA mode
    bool* pSkipIntro = reinterpret_cast<bool*>(dlsym(handle, "SkipIntroCutscene"));
    if (pSkipIntro) {
        *pSkipIntro = true;
        Log("[MTA-GAME] SkipIntroCutscene set to true for MTA session.");
    }

    typedef void (*Menu_NewGame_t)();
    Menu_NewGame_t pfn_Menu_NewGame = reinterpret_cast<Menu_NewGame_t>(dlsym(handle, "_Z12Menu_NewGamev"));

    typedef void (*Menu_SwitchOffToGame_t)();
    Menu_SwitchOffToGame_t pfn_Menu_SwitchOff = reinterpret_cast<Menu_SwitchOffToGame_t>(dlsym(handle, "_Z20Menu_SwitchOffToGamev"));

    Log("[MTA-GAME] ForceGameStart: Menu_NewGame=%p, Menu_SwitchOffToGame=%p", pfn_Menu_NewGame, pfn_Menu_SwitchOff);

    // If player already in game, just dismiss any open menu
    if (GetLocalPlayerPed() != nullptr) {
        if (pfn_Menu_SwitchOff) {
            pfn_Menu_SwitchOff();
            Log("[MTA-GAME] Player ped exists. Dismissed menu via Menu_SwitchOffToGame().");
        }
        return;
    }

    // If not in game and new game hasn't been triggered yet, initiate new game
    if (!s_newGameTriggered && pfn_Menu_NewGame) {
        s_newGameTriggered = true;
        pfn_Menu_NewGame();
        Log("[MTA-GAME] Menu_NewGame() triggered! Loading game assets (do not interrupt)...");
    }
}

void MtaGameInterface::DismissMenu() {
    void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_NOW);
    if (!handle) handle = RTLD_DEFAULT;

    typedef void (*Menu_SwitchOffToGame_t)();
    Menu_SwitchOffToGame_t pfn_Menu_SwitchOff = reinterpret_cast<Menu_SwitchOffToGame_t>(dlsym(handle, "_Z20Menu_SwitchOffToGamev"));
    if (pfn_Menu_SwitchOff) {
        pfn_Menu_SwitchOff();
        Log("[MTA-GAME] DismissMenu: Menu_SwitchOffToGame() executed.");
    }
}

bool MtaGameInterface::HasModelLoaded(int modelId) {
    if (modelId < 0) return false;
    static uint8_t* s_pStreamingInfo = nullptr;
    if (!s_pStreamingInfo) {
        void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_NOW);
        if (!handle) handle = RTLD_DEFAULT;
        s_pStreamingInfo = reinterpret_cast<uint8_t*>(dlsym(handle, "_ZN10CStreaming16ms_aInfoForModelE"));
    }
    if (!s_pStreamingInfo) return false;
    // Each entry in CStreaming::ms_aInfoForModel is 20 bytes (0x14)
    // Offset 0x10 is unsigned char m_nLoadState: 1 == LOADSTATE_LOADED
    return s_pStreamingInfo[modelId * 20 + 0x10] == 1;
}

void MtaGameInterface::RequestModel(int modelId, int flags) {
    if (modelId < 0) return;
    static void (*s_pfnRequestModel)(int, int) = nullptr;
    static void (*s_pfnLoadRequestedModels)() = nullptr;
    static bool s_resolved = false;
    if (!s_resolved) {
        void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_NOW);
        if (!handle) handle = RTLD_DEFAULT;
        s_pfnRequestModel = reinterpret_cast<void(*)(int, int)>(dlsym(handle, "_ZN10CStreaming12RequestModelEii"));
        s_pfnLoadRequestedModels = reinterpret_cast<void(*)()>(dlsym(handle, "_ZN10CStreaming19LoadRequestedModelsEv"));
        s_resolved = true;
    }
    if (s_pfnRequestModel) {
        s_pfnRequestModel(modelId, flags);
    }
    if (s_pfnLoadRequestedModels) {
        s_pfnLoadRequestedModels();
    }
}

void* MtaGameInterface::SpawnVehicle(int modelId, const CVector& pos, float heading) {
    if (!m_initialized) Initialize();

    if (!HasModelLoaded(modelId)) {
        RequestModel(modelId, 2); // 2 = priority stream
        return nullptr;
    }

    void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_NOW);
    if (!handle) handle = RTLD_DEFAULT;

    typedef void* (*CreateCar_t)(int, CVector, unsigned char);
    CreateCar_t pfn_CreateCar = reinterpret_cast<CreateCar_t>(dlsym(handle, "_ZN8CCarCtrl18CreateCarForScriptEi7CVectorh"));

    if (pfn_CreateCar) {
        void* pVeh = pfn_CreateCar(modelId, pos, 1);
        if (pVeh) {
            SetEntityHeading(pVeh, heading);
            Log("[MTA-GAME] SpawnVehicle: Model %d spawned at (%.2f, %.2f, %.2f) ptr=%p",
                modelId, pos.x, pos.y, pos.z, pVeh);
            return pVeh;
        }
    }
    return nullptr;
}

void* MtaGameInterface::SpawnVehicleForLocalPlayer(int modelId) {
    if (modelId < 400 || modelId > 611) return nullptr;
    MtaClient::Get().RequestSpawnVehicle(modelId);
    return nullptr;
}

bool MtaGameInterface::SetPlayerSkin(int skinId) {
    if (skinId < 0 || skinId > 312) return false;
    MtaClient::Get().RequestSetSkin(skinId);
    return true;
}

void* MtaGameInterface::CreateRemotePed(unsigned int modelIndex, const CVector& pos) {
    int mId = static_cast<int>(modelIndex);
    if (!HasModelLoaded(mId)) {
        RequestModel(mId, 2);
        return nullptr;
    }

    void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_NOW);
    if (!handle) handle = RTLD_DEFAULT;

    typedef void* (*AddPed_t)(int, unsigned int, const CVector&, bool);
    AddPed_t pfn_AddPed = reinterpret_cast<AddPed_t>(dlsym(handle, "_ZN11CPopulation6AddPedE8ePedTypejRK7CVectorb"));

    if (pfn_AddPed) {
        void* pPed = pfn_AddPed(4, modelIndex, pos, false); // 4 = PED_TYPE_CIVMALE
        if (pPed) {
            Log("[MTA-GAME] CreateRemotePed: Ped model %u created at (%.2f, %.2f, %.2f) ptr=%p",
                modelIndex, pos.x, pos.y, pos.z, pPed);
            return pPed;
        }
    }
    return nullptr;
}

void MtaGameInterface::DestroyRemotePed(void* ped) {
    if (!ped) return;
    RemoveEntityFromWorld(ped);
}

void MtaGameInterface::SetPedPosition(void* ped, const CVector& pos) {
    if (!ped) return;
    if (pfn_CEntity_Teleport) {
        pfn_CEntity_Teleport(ped, pos, false);
    }
}

float MtaGameInterface::GetLocalPlayerHeading() {
    void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_NOW);
    if (!handle) handle = RTLD_DEFAULT;

    typedef float (*FindPlayerHeading_t)(int);
    static FindPlayerHeading_t pfn_FindPlayerHeading = nullptr;
    if (!pfn_FindPlayerHeading) {
        pfn_FindPlayerHeading = reinterpret_cast<FindPlayerHeading_t>(dlsym(handle, "_Z17FindPlayerHeadingi"));
    }

    if (pfn_FindPlayerHeading) {
        return pfn_FindPlayerHeading(0);
    }

    void* pPed = GetLocalPlayerPed();
    if (pPed) {
#if defined(__aarch64__)
        uint8_t* pMatrix = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(pPed) + 0x18);
#else
        uint8_t* pMatrix = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(pPed) + 0x14);
#endif
        if (pMatrix) {
            float upX = *reinterpret_cast<float*>(pMatrix + 0x10);
            float upY = *reinterpret_cast<float*>(pMatrix + 0x14);
            return atan2f(-upX, upY);
        }
    }
    return 0.0f;
}

void MtaGameInterface::SetEntityHeading(void* entity, float heading) {
    if (!entity) return;
    uint8_t* pEntity = reinterpret_cast<uint8_t*>(entity);

#if defined(__aarch64__)
    uint8_t* pMatrix = *reinterpret_cast<uint8_t**>(pEntity + 0x18);
#else
    uint8_t* pMatrix = *reinterpret_cast<uint8_t**>(pEntity + 0x14);
#endif

    if (pMatrix) {
        float sinH = sinf(heading);
        float cosH = cosf(heading);

        // right vector = (cosH, sinH, 0)
        *reinterpret_cast<float*>(pMatrix + 0x00) = cosH;
        *reinterpret_cast<float*>(pMatrix + 0x04) = sinH;
        *reinterpret_cast<float*>(pMatrix + 0x08) = 0.0f;

        // up/forward vector = (-sinH, cosH, 0)
        *reinterpret_cast<float*>(pMatrix + 0x10) = -sinH;
        *reinterpret_cast<float*>(pMatrix + 0x14) = cosH;
        *reinterpret_cast<float*>(pMatrix + 0x18) = 0.0f;

        // at vector = (0, 0, 1)
        *reinterpret_cast<float*>(pMatrix + 0x20) = 0.0f;
        *reinterpret_cast<float*>(pMatrix + 0x24) = 0.0f;
        *reinterpret_cast<float*>(pMatrix + 0x28) = 1.0f;
    } else {
        *reinterpret_cast<float*>(pEntity + 0x14) = heading;
    }
}

void MtaGameInterface::SetPedHeading(void* ped, float heading) {
    SetEntityHeading(ped, heading);
}
