#pragma once

#include "mta/net/MtaPackets.h"
#include "mta/net/NetBitStream.h"
#include "mta/game/MtaTypes.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <netinet/in.h>

struct MtaServerClient {
    uint16_t playerId;
    struct sockaddr_in address;
    std::string nickname;
    uint32_t lastActiveTimeMs;
};

class MtaLocalServer {
public:
    static MtaLocalServer& Get();

    bool Start(uint16_t port = 22003);
    void Stop();

    bool IsRunning() const { return m_running; }
    uint16_t GetPort() const { return m_port; }
    size_t GetClientCount() const { return m_clients.size(); }

private:
    MtaLocalServer();
    ~MtaLocalServer();

    void ServerLoop();
    void HandleJoinPacket(const uint8_t* data, size_t length, const struct sockaddr_in& fromAddr);
    void HandlePureSyncPacket(const uint8_t* data, size_t length, const struct sockaddr_in& fromAddr);
    void HandleChatPacket(const uint8_t* data, size_t length, const struct sockaddr_in& fromAddr);
    void HandleAseQuery(const struct sockaddr_in& fromAddr);

    void SendPacketTo(const struct sockaddr_in& dest, eMtaPacketId packetId, NetBitStream& bs);
    void BroadcastPacket(eMtaPacketId packetId, NetBitStream& bs, uint16_t excludePlayerId = 0);

    int m_serverFd;
    int m_queryFd;
    uint16_t m_port;
    std::atomic<bool> m_running;
    std::thread m_thread;

    uint16_t m_nextPlayerId;
    std::unordered_map<uint16_t, MtaServerClient> m_clients;
};
