#pragma once

#include "mta/game/MtaTypes.h"
#include <string>
#include <unordered_map>
#include <cstdint>

struct MtaRemotePlayer {
    uint16_t playerId;
    std::string nickname;
    CVector position;
    float heading;
    float health;
    float armour;
    uint32_t ping;
    void* pGamePed; // Pointer to spawned in-game CPed dummy
};

class MtaPlayerManager {
public:
    static MtaPlayerManager& Get();

    void Initialize();
    void Update();

    // Remote player events
    void AddPlayer(uint16_t id, const std::string& nick);
    void RemovePlayer(uint16_t id);
    void UpdatePlayerSync(uint16_t id, const CVector& pos, float heading, float health, float armour);

    void Clear();

    // Local player sync
    void SendLocalPlayerSync();

    const std::unordered_map<uint16_t, MtaRemotePlayer>& GetPlayers() const { return m_players; }
    size_t GetPlayerCount() const { return m_players.size(); }
    std::string GetPlayerListJson() const;

private:
    MtaPlayerManager();

    std::unordered_map<uint16_t, MtaRemotePlayer> m_players;
    uint32_t m_lastSyncTimeMs;
};
