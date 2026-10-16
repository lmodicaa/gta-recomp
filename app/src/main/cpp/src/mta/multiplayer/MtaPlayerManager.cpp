#include "MtaPlayerManager.h"
#include "mta/game/MtaGameInterface.h"
#include "mta/net/MtaNetwork.h"
#include "mta/ui/MtaJNI.h"
#include "vendor/log/log.h"
#include <chrono>

static uint32_t GetCurrentTimeMs() {
    auto now = std::chrono::steady_clock::now();
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
}

MtaPlayerManager& MtaPlayerManager::Get() {
    static MtaPlayerManager instance;
    return instance;
}

MtaPlayerManager::MtaPlayerManager()
    : m_lastSyncTimeMs(0) {
}

void MtaPlayerManager::Initialize() {
    Log("[MTA-MP] Initializing MtaPlayerManager...");

    // Register Packet Handlers
    MtaNetwork::Get().RegisterHandler(PACKET_ID_PLAYER_JOIN, [this](eMtaPacketId, NetBitStream& bs) {
        uint16_t playerId = 0;
        std::string nick;
        if (bs.ReadShort(playerId) && bs.ReadString(nick)) {
            AddPlayer(playerId, nick);
        }
    });

    MtaNetwork::Get().RegisterHandler(PACKET_ID_PLAYER_QUIT, [this](eMtaPacketId, NetBitStream& bs) {
        uint16_t playerId = 0;
        if (bs.ReadShort(playerId)) {
            RemovePlayer(playerId);
        }
    });

    MtaNetwork::Get().RegisterHandler(PACKET_ID_PLAYER_PURESYNC, [this](eMtaPacketId, NetBitStream& bs) {
        uint16_t playerId = 0;
        CVector pos;
        float heading = 0.0f;
        float health = 100.0f;
        float armour = 0.0f;

        if (bs.ReadShort(playerId) &&
            bs.ReadFloat(pos.x) && bs.ReadFloat(pos.y) && bs.ReadFloat(pos.z) &&
            bs.ReadFloat(heading) && bs.ReadFloat(health) && bs.ReadFloat(armour)) {
            UpdatePlayerSync(playerId, pos, heading, health, armour);
        }
    });

    MtaNetwork::Get().RegisterHandler(PACKET_ID_CHAT_ECHO, [](eMtaPacketId, NetBitStream& bs) {
        std::string text;
        if (bs.ReadString(text)) {
            MtaJNI::NotifyJavaChat("CHAT", text);
        }
    });

    Log("[MTA-MP] MtaPlayerManager initialized.");
}

void MtaPlayerManager::Update() {
    // Send local player sync packet every 50ms (20 ticks/sec)
    uint32_t now = GetCurrentTimeMs();
    if (now - m_lastSyncTimeMs >= 50) {
        m_lastSyncTimeMs = now;
        SendLocalPlayerSync();
    }
}

void MtaPlayerManager::AddPlayer(uint16_t id, const std::string& nick) {
    MtaRemotePlayer player{};
    player.playerId = id;
    player.nickname = nick;
    player.health = 100.0f;
    player.armour = 0.0f;
    player.pGamePed = nullptr;

    m_players[id] = player;
    Log("[MTA-MP] Player joined: %s (ID: %u)", nick.c_str(), id);
    MtaJNI::NotifyJavaChat("INFO", nick + " se unió a la partida.");
}

void MtaPlayerManager::RemovePlayer(uint16_t id) {
    auto it = m_players.find(id);
    if (it != m_players.end()) {
        Log("[MTA-MP] Player left: %s (ID: %u)", it->second.nickname.c_str(), id);
        MtaJNI::NotifyJavaChat("INFO", it->second.nickname + " salió de la partida.");
        if (it->second.pGamePed) {
            MtaGameInterface::Get().DestroyRemotePed(it->second.pGamePed);
            it->second.pGamePed = nullptr;
        }
        m_players.erase(it);
    }
}

void MtaPlayerManager::Clear() {
    for (auto& [id, p] : m_players) {
        if (p.pGamePed) {
            MtaGameInterface::Get().DestroyRemotePed(p.pGamePed);
            p.pGamePed = nullptr;
        }
    }
    m_players.clear();
}

void MtaPlayerManager::UpdatePlayerSync(uint16_t id, const CVector& pos, float heading, float health, float armour) {
    auto it = m_players.find(id);
    if (it != m_players.end()) {
        it->second.position = pos;
        it->second.heading = heading;
        it->second.health = health;
        it->second.armour = armour;

        // Remote 3D Ped Sync
        if (!it->second.pGamePed) {
            it->second.pGamePed = MtaGameInterface::Get().CreateRemotePed(0, pos);
            Log("[MTA-MP] Created remote 3D ped for player %u (%s) at (%.2f, %.2f, %.2f)",
                id, it->second.nickname.c_str(), pos.x, pos.y, pos.z);
        } else {
            MtaGameInterface::Get().SetPedPosition(it->second.pGamePed, pos);
            MtaGameInterface::Get().SetPedHeading(it->second.pGamePed, heading);
        }
    }
}

void MtaPlayerManager::SendLocalPlayerSync() {
    if (!MtaNetwork::Get().IsConnected()) return;

    CVector localPos;
    if (!MtaGameInterface::Get().GetLocalPlayerPosition(localPos)) {
        return;
    }

    float heading = MtaGameInterface::Get().GetLocalPlayerHeading();
    float health = MtaGameInterface::Get().GetLocalPlayerHealth();
    float armour = MtaGameInterface::Get().GetLocalPlayerArmour();

    NetBitStream bs;
    bs.WriteFloat(localPos.x);
    bs.WriteFloat(localPos.y);
    bs.WriteFloat(localPos.z);
    bs.WriteFloat(heading);
    bs.WriteFloat(health);
    bs.WriteFloat(armour);

    MtaNetwork::Get().SendPacket(PACKET_ID_PLAYER_PURESYNC, bs);
}

std::string MtaPlayerManager::GetPlayerListJson() const {
    std::string json = "[";
    bool first = true;
    for (const auto& [id, p] : m_players) {
        if (!first) json += ",";
        first = false;
        json += "{\"id\":" + std::to_string(p.playerId) +
                ",\"nick\":\"" + p.nickname + "\"" +
                ",\"health\":" + std::to_string(static_cast<int>(p.health)) +
                ",\"armour\":" + std::to_string(static_cast<int>(p.armour)) +
                ",\"ping\":" + std::to_string(p.ping) + "}";
    }
    json += "]";
    return json;
}
