#pragma once

#include "mta/net/MtaNetwork.h"
#include <string>

class MtaClient {
public:
    static MtaClient& Get();

    void Initialize();
    void Update();
    void Shutdown();

    void ConnectToServer(const std::string& host, uint16_t port);
    void Disconnect();

    bool IsRunning() const { return m_initialized; }
    void TriggerPlayerSpawn();
    void SetPlayerSpawned(bool val) { m_playerSpawned = val; }
    bool IsPlayerSpawned() const { return m_playerSpawned; }

    // In-Game MTA Commands (/v, /skin, /kill, /pos, /tp, /help)
    bool ProcessCommand(const std::string& cmdText);

    // Thread-safe game actions (called from JNI or chat, executed on game thread)
    void RequestSpawnVehicle(int modelId);
    void RequestSetSkin(int skinId);

private:
    MtaClient();
    ~MtaClient();

    void ProcessPendingVehicleSpawn();
    void ProcessPendingSkinChange();

    bool m_initialized;
    bool m_playerSpawned;
    uint32_t m_lastStartAttempt;
    uint32_t m_startAttempts;

    int m_pendingVehicleModel;
    int m_pendingVehicleTicks;
    int m_pendingSkinId;
    int m_pendingSkinTicks;
};
