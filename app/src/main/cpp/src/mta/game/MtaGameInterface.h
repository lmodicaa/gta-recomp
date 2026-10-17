#pragma once

#include "MtaTypes.h"
#include <cstdint>

class MtaGameInterface {
public:
    static MtaGameInterface& Get();

    bool Initialize();
    bool IsInitialized() const { return m_initialized; }

    // Player access
    void* GetLocalPlayerPed();
    bool GetLocalPlayerPosition(CVector& outPos);
    bool SetLocalPlayerPosition(const CVector& pos);
    void SpawnLocalPlayer(const CVector& pos);

    float GetLocalPlayerHealth();
    void SetLocalPlayerHealth(float health);

    float GetLocalPlayerArmour();
    void SetLocalPlayerArmour(float armour);

    // World & Entities
    void AddEntityToWorld(void* entity);
    void RemoveEntityFromWorld(void* entity);
    void ForceGameStart();
    void DismissMenu();
    void ResetGameState();

    // Vehicles & Skins
    void* SpawnVehicle(int modelId, const CVector& pos, float heading = 0.0f);
    void* SpawnVehicleForLocalPlayer(int modelId);
    bool SetPlayerSkin(int skinId);

    // Streaming & Model management
    bool HasModelLoaded(int modelId);
    void RequestModel(int modelId, int flags = 2);
    int  GetModelLoadState(int modelId);   // raw CStreaming m_nLoadState byte (-1 if unresolved)
    void FlushModelRequests();             // CStreaming::LoadAllRequestedModels(false)

    // Remote Players 3D Peds & Entities
    void* CreateRemotePed(unsigned int modelIndex, const CVector& pos);
    void DestroyRemotePed(void* ped);
    void SetPedPosition(void* ped, const CVector& pos);
    float GetLocalPlayerHeading();
    void SetEntityHeading(void* entity, float heading);
    void SetPedHeading(void* ped, float heading);

private:
    MtaGameInterface();

    bool m_initialized;

    // Resolved function pointers from libGTASA.so
    void* (*pfn_FindPlayerPed)(int);
    void (*pfn_CWorld_Add)(void*);
    void (*pfn_CWorld_Remove)(void*);
    void (*pfn_CEntity_Teleport)(void*, CVector, unsigned char);
};
