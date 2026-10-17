#pragma once

#include "mta/net/MtaNetwork.h"
#include <string>
#include <mutex>
#include <deque>

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

    void Set3dWorldReady(bool ready) { m_3dWorldReady = ready; }
    bool Is3dWorldReady() const { return m_3dWorldReady; }
    void EnsureGameInterface();

    // In-Game MTA Commands (/v, /skin, /kill, /pos, /tp, /help)
    bool ProcessCommand(const std::string& cmdText);

    // Thread-safe game actions (called from JNI or chat, executed on game thread)
    void RequestSpawnVehicle(int modelId);
    void RequestSetSkin(int skinId);

    // [MTA-VEH] Vehicle spawn queue consumer. Must be drained once per frame by the
    // game-loop hooks (hook_Idle / hook_CGame_Process) — NEVER via MtaClient::Update()
    // or from the UI thread: streaming requests, CreateCarForScript and CVehicle field
    // writes are only safe on the game thread.  One active request is processed across
    // frames (retries while the model streams in); remaining requests stay in the queue.
    void ProcessPendingVehicleSpawn();

private:
    MtaClient();
    ~MtaClient();

    void ProcessPendingSkinChange();

    bool m_initialized;
    bool m_3dWorldReady;
    bool m_playerSpawned;
    uint32_t m_lastStartAttempt;
    uint32_t m_startAttempts;

    // [MTA-VEH] Vehicle spawn queue: producers (JNI/ui thread) push under mutex,
    // game thread pops. Only the game thread touches the two "active" fields below.
    std::deque<int> m_vehicleQueue;
    std::mutex m_vehicleQueueMutex;
    int m_activeVehicleModel;     // request being processed across frames; -1 = idle
    int m_activeVehicleAttempts;  // frames spent on the active request phase

    int m_pendingSkinId;
    int m_pendingSkinTicks;
};
