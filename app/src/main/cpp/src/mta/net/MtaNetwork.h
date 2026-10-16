#pragma once

#include "MtaPackets.h"
#include "NetBitStream.h"
#include <string>
#include <functional>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <netinet/in.h>

using PacketHandler = std::function<void(eMtaPacketId, NetBitStream&)>;

class MtaNetwork {
public:
    static MtaNetwork& Get();

    bool Connect(const std::string& host, uint16_t port);
    void Disconnect();
    void Process();

    bool SendPacket(eMtaPacketId packetId, NetBitStream& bitStream);
    void RegisterHandler(eMtaPacketId packetId, PacketHandler handler);

    bool IsConnected() const { return m_socketFd >= 0 && m_state != eMtaState::DISCONNECTED; }
    eMtaState GetState() const { return m_state; }

private:
    MtaNetwork();
    ~MtaNetwork();

    void NetworkWorker();
    void SendHandshakePackets();
    void HandleAseResponse(const uint8_t* data, size_t length);

    int m_socketFd;
    int m_querySocketFd;
    struct sockaddr_in m_serverAddr;
    struct sockaddr_in m_queryAddr;
    std::atomic<eMtaState> m_state;
    std::string m_serverHost;
    uint16_t m_serverPort;

    std::thread m_workerThread;
    std::atomic<bool> m_running;
    uint32_t m_connectStartTimeMs;
    int m_retryCount;

    std::unordered_map<uint8_t, PacketHandler> m_handlers;
};
