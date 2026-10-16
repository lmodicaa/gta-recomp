#include "MtaLocalServer.h"
#include "vendor/log/log.h"
#include "mta/ui/MtaJNI.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cstring>
#include <chrono>

static uint32_t GetServerTimeMs() {
    auto now = std::chrono::steady_clock::now();
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
}

MtaLocalServer& MtaLocalServer::Get() {
    static MtaLocalServer instance;
    return instance;
}

MtaLocalServer::MtaLocalServer()
    : m_serverFd(-1),
      m_queryFd(-1),
      m_port(22003),
      m_running(false),
      m_nextPlayerId(100) {
}

MtaLocalServer::~MtaLocalServer() {
    Stop();
}

bool MtaLocalServer::Start(uint16_t port) {
    Stop();

    m_port = port;

    // 1. Create and bind main server game socket
    m_serverFd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_serverFd < 0) {
        MTA_DEBUG("[SERVER] ERROR: Fallo al crear socket servidor UDP: %s", strerror(errno));
        return false;
    }

    int reuse = 1;
    setsockopt(m_serverFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    int flags = fcntl(m_serverFd, F_GETFL, 0);
    fcntl(m_serverFd, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in bindAddr{};
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_port = htons(port);
    bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(m_serverFd, reinterpret_cast<struct sockaddr*>(&bindAddr), sizeof(bindAddr)) < 0) {
        MTA_DEBUG("[SERVER] ERROR: Fallo al bind en puerto %u: %s", port, strerror(errno));
        close(m_serverFd);
        m_serverFd = -1;
        return false;
    }

    // 2. Create and bind ASE query socket (port + 123)
    m_queryFd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_queryFd >= 0) {
        setsockopt(m_queryFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        flags = fcntl(m_queryFd, F_GETFL, 0);
        fcntl(m_queryFd, F_SETFL, flags | O_NONBLOCK);

        struct sockaddr_in queryBind{};
        queryBind.sin_family = AF_INET;
        queryBind.sin_port = htons(port + 123);
        queryBind.sin_addr.s_addr = htonl(INADDR_ANY);

        if (bind(m_queryFd, reinterpret_cast<struct sockaddr*>(&queryBind), sizeof(queryBind)) < 0) {
            MTA_DEBUG("[SERVER] AVISO: No se pudo enlazar puerto de consulta ASE %u: %s", port + 123, strerror(errno));
            close(m_queryFd);
            m_queryFd = -1;
        }
    }

    m_running = true;
    m_clients.clear();
    m_nextPlayerId = 100;

    MTA_DEBUG("[SERVER] Servidor local MTA INICIADO en 0.0.0.0:%u (ASE: %u)", port, port + 123);
    MtaJNI::NotifyJavaChat("HOST", "Servidor Local MTA iniciado en puerto " + std::to_string(port));

    m_thread = std::thread(&MtaLocalServer::ServerLoop, this);
    return true;
}

void MtaLocalServer::Stop() {
    if (!m_running) return;

    MTA_DEBUG("[SERVER] Deteniendo servidor local...");
    m_running = false;

    if (m_thread.joinable()) {
        m_thread.join();
    }

    if (m_serverFd >= 0) {
        close(m_serverFd);
        m_serverFd = -1;
    }
    if (m_queryFd >= 0) {
        close(m_queryFd);
        m_queryFd = -1;
    }

    m_clients.clear();
    MTA_DEBUG("[SERVER] Servidor local detenido.");
    MtaJNI::NotifyJavaChat("HOST", "Servidor Local MTA detenido.");
}

void MtaLocalServer::SendPacketTo(const struct sockaddr_in& dest, eMtaPacketId packetId, NetBitStream& bs) {
    if (m_serverFd < 0) return;

    std::vector<uint8_t> payload;
    payload.reserve(1 + bs.GetNumberOfBytesUsed());
    payload.push_back(static_cast<uint8_t>(packetId));

    if (bs.GetNumberOfBytesUsed() > 0) {
        payload.insert(payload.end(), bs.GetData(), bs.GetData() + bs.GetNumberOfBytesUsed());
    }

    sendto(m_serverFd, payload.data(), payload.size(), 0,
           reinterpret_cast<const struct sockaddr*>(&dest), sizeof(dest));
}

void MtaLocalServer::BroadcastPacket(eMtaPacketId packetId, NetBitStream& bs, uint16_t excludePlayerId) {
    for (const auto& [id, client] : m_clients) {
        if (id != excludePlayerId) {
            SendPacketTo(client.address, packetId, bs);
        }
    }
}

void MtaLocalServer::HandleAseQuery(const struct sockaddr_in& fromAddr) {
    if (m_queryFd < 0) return;

    // Build ASE response: EYE1 + fields
    std::vector<uint8_t> resp;
    const char* magic = "EYE1";
    resp.insert(resp.end(), magic, magic + 4);

    auto appendField = [&](const std::string& str) {
        resp.push_back(static_cast<uint8_t>(str.length()));
        resp.insert(resp.end(), str.begin(), str.end());
    };

    appendField("mta");
    appendField(std::to_string(m_port));
    appendField("reGTA Android Local Host");
    appendField("Freeroam");
    appendField("San Andreas");
    appendField("1.6");
    appendField("0"); // no password
    appendField(std::to_string(m_clients.size()));
    appendField("32");

    sendto(m_queryFd, resp.data(), resp.size(), 0,
           reinterpret_cast<const struct sockaddr*>(&fromAddr), sizeof(fromAddr));

    MTA_DEBUG("[SERVER] Consulta ASE respondida a %s:%u",
              inet_ntoa(fromAddr.sin_addr), ntohs(fromAddr.sin_port));
}

void MtaLocalServer::HandleJoinPacket(const uint8_t* data, size_t length, const struct sockaddr_in& fromAddr) {
    NetBitStream stream(data, length);
    std::string clientNick = "Player";
    stream.ReadString(clientNick);

    uint16_t newId = ++m_nextPlayerId;
    MtaServerClient client{};
    client.playerId = newId;
    client.address = fromAddr;
    client.nickname = clientNick;
    client.lastActiveTimeMs = GetServerTimeMs();
    m_clients[newId] = client;

    MTA_DEBUG("[SERVER] Jugador UNIDO: '%s' asignado ID=%u de %s:%u",
              clientNick.c_str(), newId, inet_ntoa(fromAddr.sin_addr), ntohs(fromAddr.sin_port));

    // 1. Send PACKET_ID_SERVER_JOINED to this client
    NetBitStream joinResp;
    joinResp.WriteShort(newId);
    joinResp.WriteString("reGTA Android Local Host");
    SendPacketTo(fromAddr, PACKET_ID_SERVER_JOINED, joinResp);

    // 2. Send PACKET_ID_SERVER_JOIN_DATA to this client (confirming in-game)
    NetBitStream joinData;
    joinData.WriteShort(newId);
    joinData.WriteByte(1); // ready
    SendPacketTo(fromAddr, PACKET_ID_SERVER_JOIN_DATA, joinData);

    // 3. Send welcome chat message
    NetBitStream chatResp;
    chatResp.WriteString("¡Bienvenido al servidor local MTA Android!");
    SendPacketTo(fromAddr, PACKET_ID_CHAT_ECHO, chatResp);

    // 4. Send all existing players to the new client
    for (const auto& [id, existing] : m_clients) {
        if (id != newId) {
            NetBitStream existJoin;
            existJoin.WriteShort(id);
            existJoin.WriteString(existing.nickname);
            SendPacketTo(fromAddr, PACKET_ID_PLAYER_JOIN, existJoin);
        }
    }

    // 5. Broadcast join to any other connected clients
    NetBitStream bcastJoin;
    bcastJoin.WriteShort(newId);
    bcastJoin.WriteString(clientNick);
    BroadcastPacket(PACKET_ID_PLAYER_JOIN, bcastJoin, newId);
}

void MtaLocalServer::HandleChatPacket(const uint8_t* data, size_t length, const struct sockaddr_in& fromAddr) {
    NetBitStream stream(data, length);
    std::string text;
    if (stream.ReadString(text)) {
        std::string sender = "Jugador";
        for (const auto& [id, client] : m_clients) {
            if (client.address.sin_addr.s_addr == fromAddr.sin_addr.s_addr &&
                client.address.sin_port == fromAddr.sin_port) {
                sender = client.nickname;
                break;
            }
        }

        std::string formattedChat = sender + ": " + text;
        MTA_DEBUG("[SERVER-CHAT] %s", formattedChat.c_str());

        // Broadcast to all clients
        NetBitStream outStream;
        outStream.WriteString(formattedChat);
        BroadcastPacket(PACKET_ID_CHAT_ECHO, outStream);
    }
}

void MtaLocalServer::HandlePureSyncPacket(const uint8_t* data, size_t length, const struct sockaddr_in& fromAddr) {
    // Find client ID
    uint16_t senderId = 0;
    for (auto& [id, client] : m_clients) {
        if (client.address.sin_addr.s_addr == fromAddr.sin_addr.s_addr &&
            client.address.sin_port == fromAddr.sin_port) {
            senderId = id;
            client.lastActiveTimeMs = GetServerTimeMs();
            break;
        }
    }

    if (senderId == 0) return;

    // Relay sync to all other players
    NetBitStream inStream(data, length);
    CVector pos;
    float heading, health, armour;
    if (inStream.ReadFloat(pos.x) && inStream.ReadFloat(pos.y) && inStream.ReadFloat(pos.z) &&
        inStream.ReadFloat(heading) && inStream.ReadFloat(health) && inStream.ReadFloat(armour)) {

        NetBitStream outStream;
        outStream.WriteShort(senderId);
        outStream.WriteFloat(pos.x);
        outStream.WriteFloat(pos.y);
        outStream.WriteFloat(pos.z);
        outStream.WriteFloat(heading);
        outStream.WriteFloat(health);
        outStream.WriteFloat(armour);

        BroadcastPacket(PACKET_ID_PLAYER_PURESYNC, outStream, senderId);
    }
}

void MtaLocalServer::ServerLoop() {
    MTA_DEBUG("[SERVER-THREAD] Bucle principal del servidor iniciado.");

    uint8_t buffer[4096];
    struct sockaddr_in fromAddr{};
    socklen_t fromLen = sizeof(fromAddr);

    while (m_running) {
        struct pollfd fds[2];
        int numFds = 0;

        if (m_serverFd >= 0) {
            fds[numFds].fd = m_serverFd;
            fds[numFds].events = POLLIN;
            fds[numFds].revents = 0;
            numFds++;
        }
        if (m_queryFd >= 0) {
            fds[numFds].fd = m_queryFd;
            fds[numFds].events = POLLIN;
            fds[numFds].revents = 0;
            numFds++;
        }

        int ret = poll(fds, numFds, 20); // 20ms tick
        if (ret <= 0) continue;

        // ASE Query socket
        if (m_queryFd >= 0 && (fds[1].revents & POLLIN)) {
            fromLen = sizeof(fromAddr);
            ssize_t received = recvfrom(m_queryFd, buffer, sizeof(buffer), 0,
                                        reinterpret_cast<struct sockaddr*>(&fromAddr), &fromLen);
            if (received > 0 && buffer[0] == 's') {
                HandleAseQuery(fromAddr);
            }
        }

        // Game socket
        if (m_serverFd >= 0 && (fds[0].revents & POLLIN)) {
            fromLen = sizeof(fromAddr);
            ssize_t received = recvfrom(m_serverFd, buffer, sizeof(buffer), 0,
                                        reinterpret_cast<struct sockaddr*>(&fromAddr), &fromLen);
            if (received > 0) {
                uint8_t packetIdRaw = buffer[0];
                eMtaPacketId packetId = static_cast<eMtaPacketId>(packetIdRaw);
                const uint8_t* packetPayload = buffer + 1;
                size_t payloadLen = static_cast<size_t>(received - 1);

                MTA_DEBUG("[SERVER-THREAD] Paquete recibido: ID=0x%02X (%zd bytes) de %s:%u",
                          packetIdRaw, received, inet_ntoa(fromAddr.sin_addr), ntohs(fromAddr.sin_port));

                switch (packetId) {
                    case PACKET_ID_PLAYER_JOIN:
                        HandleJoinPacket(packetPayload, payloadLen, fromAddr);
                        break;
                    case PACKET_ID_PLAYER_PURESYNC:
                        HandlePureSyncPacket(packetPayload, payloadLen, fromAddr);
                        break;
                    case PACKET_ID_CHAT_ECHO:
                        HandleChatPacket(packetPayload, payloadLen, fromAddr);
                        break;
                    case PACKET_ID_PLAYER_QUIT:
                        // Handle disconnect
                        for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
                            if (it->second.address.sin_addr.s_addr == fromAddr.sin_addr.s_addr &&
                                it->second.address.sin_port == fromAddr.sin_port) {
                                MTA_DEBUG("[SERVER] Jugador desconectado: ID=%u", it->first);
                                uint16_t quitId = it->first;
                                m_clients.erase(it);

                                NetBitStream qStream;
                                qStream.WriteShort(quitId);
                                BroadcastPacket(PACKET_ID_PLAYER_QUIT, qStream);
                                break;
                            }
                        }
                        break;
                    default:
                        break;
                }
            }
        }
    }

    MTA_DEBUG("[SERVER-THREAD] Bucle principal del servidor finalizado.");
}
