#include "MtaNetwork.h"
#include "mta/game/MtaGameInterface.h"
#include "mta/ui/MtaJNI.h"
#include "vendor/log/log.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cstring>
#include <chrono>

static uint32_t GetCurrentTimeMs() {
    auto now = std::chrono::steady_clock::now();
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
}

MtaNetwork& MtaNetwork::Get() {
    static MtaNetwork instance;
    return instance;
}

MtaNetwork::MtaNetwork()
    : m_socketFd(-1),
      m_querySocketFd(-1),
      m_state(eMtaState::DISCONNECTED),
      m_serverPort(0),
      m_running(false),
      m_connectStartTimeMs(0),
      m_retryCount(0) {
    std::memset(&m_serverAddr, 0, sizeof(m_serverAddr));
    std::memset(&m_queryAddr, 0, sizeof(m_queryAddr));
}

MtaNetwork::~MtaNetwork() {
    Disconnect();
}

bool MtaNetwork::Connect(const std::string& host, uint16_t port) {
    MTA_DEBUG("[NET] Connect() iniciando host='%s', port=%u", host.c_str(), port);
    Disconnect();

    m_serverHost = host;
    m_serverPort = port;

    // Check for localhost warning
    if (host == "127.0.0.1" || host == "localhost") {
        MTA_DEBUG("[NET] AVISO: Detectado localhost 127.0.0.1 (dispositivo móvil)");
        MtaJNI::NotifyJavaChat("AVISO", "127.0.0.1 es tu dispositivo móvil. Si tu servidor corre en PC, usa la IP de red local (ej. 192.168.x.x) o 10.0.2.2 en emulador.");
    }

    // 1. Create main game UDP socket
    m_socketFd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_socketFd < 0) {
        MTA_DEBUG("[NET] ERROR: Fallo al crear socket UDP: %s (errno %d)", strerror(errno), errno);
        MtaJNI::NotifyJavaState(static_cast<int>(eMtaState::DISCONNECTED), "Error de socket");
        return false;
    }

    // 2. Create ASE query UDP socket (port + 123)
    m_querySocketFd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    MTA_DEBUG("[NET] Sockets creados: GameFD=%d, QueryFD=%d", m_socketFd, m_querySocketFd);

    // Set non-blocking
    int flags = fcntl(m_socketFd, F_GETFL, 0);
    fcntl(m_socketFd, F_SETFL, flags | O_NONBLOCK);
    if (m_querySocketFd >= 0) {
        flags = fcntl(m_querySocketFd, F_GETFL, 0);
        fcntl(m_querySocketFd, F_SETFL, flags | O_NONBLOCK);
    }

    std::memset(&m_serverAddr, 0, sizeof(m_serverAddr));
    m_serverAddr.sin_family = AF_INET;
    m_serverAddr.sin_port = htons(port);

    std::memset(&m_queryAddr, 0, sizeof(m_queryAddr));
    m_queryAddr.sin_family = AF_INET;
    m_queryAddr.sin_port = htons(port + 123); // MTA ASE query port

    if (inet_pton(AF_INET, host.c_str(), &m_serverAddr.sin_addr) <= 0) {
        MTA_DEBUG("[NET] Host '%s' no es IP numerica, resolviendo por DNS...", host.c_str());
        struct hostent* he = gethostbyname(host.c_str());
        if (!he) {
            MTA_DEBUG("[NET] ERROR: DNS fallo para '%s': %s (h_errno %d)", host.c_str(), hstrerror(h_errno), h_errno);
            close(m_socketFd);
            m_socketFd = -1;
            if (m_querySocketFd >= 0) {
                close(m_querySocketFd);
                m_querySocketFd = -1;
            }
            MtaJNI::NotifyJavaState(static_cast<int>(eMtaState::DISCONNECTED), "Host no encontrado");
            MtaJNI::NotifyJavaChat("ERROR", "No se pudo resolver el host: " + host);
            return false;
        }
        std::memcpy(&m_serverAddr.sin_addr, he->h_addr_list[0], he->h_length);
        std::memcpy(&m_queryAddr.sin_addr, he->h_addr_list[0], he->h_length);
        MTA_DEBUG("[NET] DNS resuelto: %s -> %s", host.c_str(), inet_ntoa(m_serverAddr.sin_addr));
    } else {
        m_queryAddr.sin_addr = m_serverAddr.sin_addr;
        MTA_DEBUG("[NET] IP numerica configurada: %s", inet_ntoa(m_serverAddr.sin_addr));
    }

    m_state = eMtaState::CONNECTING;
    m_connectStartTimeMs = GetCurrentTimeMs();
    m_retryCount = 0;

    MTA_DEBUG("[NET] Iniciando conexion a %s:%u (ASE Query: %u)...", host.c_str(), port, port + 123);
    MtaJNI::NotifyJavaState(static_cast<int>(eMtaState::CONNECTING), "Conectando...");
    MtaJNI::NotifyJavaChat("MTA", "Enviando solicitud de conexion a " + host + ":" + std::to_string(port) + "...");

    // Send initial handshake and query
    SendHandshakePackets();

    // Start background network thread
    m_running = true;
    m_workerThread = std::thread(&MtaNetwork::NetworkWorker, this);
    MTA_DEBUG("[NET] Hilo de red NetworkWorker lanzado exitosamente.");

    return true;
}

void MtaNetwork::SendHandshakePackets() {
    if (m_socketFd < 0) return;

    MTA_DEBUG("[NET] Enviando paquetes de handshake a %s:%u...", inet_ntoa(m_serverAddr.sin_addr), ntohs(m_serverAddr.sin_port));

    // 1. Send MTA Join Handshake
    NetBitStream joinStream;
    joinStream.WriteString("MTA:SA Android");
    SendPacket(PACKET_ID_PLAYER_JOIN, joinStream);

    // 2. Send MTA ASE server info query ('s')
    if (m_querySocketFd >= 0) {
        const char queryByte = 's';
        ssize_t qSent = sendto(m_querySocketFd, &queryByte, 1, 0,
                               reinterpret_cast<struct sockaddr*>(&m_queryAddr), sizeof(m_queryAddr));
        MTA_DEBUG("[NET] ASE query byte 's' enviado a %s:%u (resultado: %zd)",
                  inet_ntoa(m_queryAddr.sin_addr), ntohs(m_queryAddr.sin_port), qSent);
    }
}

void MtaNetwork::Disconnect() {
    MTA_DEBUG("[NET] Disconnect() invocado.");
    m_running = false;
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    if (m_socketFd >= 0) {
        if (m_state != eMtaState::DISCONNECTED) {
            NetBitStream quitStream;
            quitStream.WriteByte(0);
            SendPacket(PACKET_ID_PLAYER_QUIT, quitStream);
        }
        close(m_socketFd);
        m_socketFd = -1;
    }

    if (m_querySocketFd >= 0) {
        close(m_querySocketFd);
        m_querySocketFd = -1;
    }

    m_state = eMtaState::DISCONNECTED;
    MTA_DEBUG("[NET] Desconexion completa.");
    MtaJNI::NotifyJavaState(static_cast<int>(eMtaState::DISCONNECTED), "Desconectado");
}

bool MtaNetwork::SendPacket(eMtaPacketId packetId, NetBitStream& bitStream) {
    if (m_socketFd < 0) {
        MTA_DEBUG("[NET] ERROR en SendPacket: m_socketFd es negativo!");
        return false;
    }

    std::vector<uint8_t> payload;
    payload.reserve(1 + bitStream.GetNumberOfBytesUsed());
    payload.push_back(static_cast<uint8_t>(packetId));

    if (bitStream.GetNumberOfBytesUsed() > 0) {
        payload.insert(payload.end(), bitStream.GetData(), bitStream.GetData() + bitStream.GetNumberOfBytesUsed());
    }

    ssize_t sent = sendto(m_socketFd, payload.data(), payload.size(), 0,
                          reinterpret_cast<struct sockaddr*>(&m_serverAddr), sizeof(m_serverAddr));

    MTA_DEBUG("[NET] SendPacket ID=0x%02X (%zu bytes) enviado=%zd a %s:%u",
              (unsigned)packetId, payload.size(), sent, inet_ntoa(m_serverAddr.sin_addr), ntohs(m_serverAddr.sin_port));

    return sent == static_cast<ssize_t>(payload.size());
}

void MtaNetwork::RegisterHandler(eMtaPacketId packetId, PacketHandler handler) {
    m_handlers[static_cast<uint8_t>(packetId)] = std::move(handler);
}

void MtaNetwork::HandleAseResponse(const uint8_t* data, size_t length) {
    // ASE Response format: EYE1 + length-prefixed strings
    if (length < 4 || std::memcmp(data, "EYE1", 4) != 0) {
        return;
    }

    size_t offset = 4;
    auto readString = [&](std::string& out) -> bool {
        if (offset >= length) return false;
        uint8_t len = data[offset++];
        if (offset + len > length) return false;
        out.assign(reinterpret_cast<const char*>(data + offset), len);
        offset += len;
        return true;
    };

    std::string game, portStr, serverName, gameType, mapName, version, password, players, maxPlayers;
    readString(game);
    readString(portStr);
    if (readString(serverName)) {
        readString(gameType);
        readString(mapName);
        readString(version);
        readString(password);
        readString(players);
        readString(maxPlayers);

        MTA_DEBUG("[NET-ASE] Servidor ASE respondio: '%s' [%s/%s players]",
                  serverName.c_str(), players.c_str(), maxPlayers.c_str());

        MtaJNI::NotifyJavaChat("MTA", "Servidor encontrado: " + serverName + " [" + players + "/" + maxPlayers + "]");
        MtaJNI::NotifyJavaState(static_cast<int>(eMtaState::JOINING), "Servidor: " + serverName);
    }
}

void MtaNetwork::NetworkWorker() {
    MTA_DEBUG("[NET-THREAD] Background network worker thread iniciado.");

    uint8_t buffer[4096];
    struct sockaddr_in fromAddr;
    socklen_t fromLen = sizeof(fromAddr);

    while (m_running) {
        uint32_t now = GetCurrentTimeMs();

        // 1. Connection retry / timeout logic
        if (m_state == eMtaState::CONNECTING) {
            uint32_t elapsed = now - m_connectStartTimeMs;

            if (elapsed > 1500 * (m_retryCount + 1) && m_retryCount < 3) {
                m_retryCount++;
                MTA_DEBUG("[NET-THREAD] Reintentando handshake (intento %d/3) tras %u ms...", m_retryCount + 1, elapsed);
                MtaJNI::NotifyJavaChat("MTA", "Reintentando conectar (intento " + std::to_string(m_retryCount + 1) + "/3)...");
                SendHandshakePackets();
            } else if (elapsed > 6000) {
                MTA_DEBUG("[NET-THREAD] TIMEOUT: Pasaron %u ms sin respuesta de %s:%u", elapsed, m_serverHost.c_str(), m_serverPort);
                MtaJNI::NotifyJavaChat("ERROR", "Tiempo de espera agotado. El servidor en " + m_serverHost + ":" + std::to_string(m_serverPort) + " no responde.");
                MtaJNI::NotifyJavaState(static_cast<int>(eMtaState::DISCONNECTED), "Sin respuesta (Timeout)");
                m_state = eMtaState::DISCONNECTED;
                break;
            }
        }

        // 2. Poll sockets
        struct pollfd fds[2];
        int numFds = 0;

        if (m_socketFd >= 0) {
            fds[numFds].fd = m_socketFd;
            fds[numFds].events = POLLIN;
            fds[numFds].revents = 0;
            numFds++;
        }
        if (m_querySocketFd >= 0) {
            fds[numFds].fd = m_querySocketFd;
            fds[numFds].events = POLLIN;
            fds[numFds].revents = 0;
            numFds++;
        }

        int ret = poll(fds, numFds, 50); // 50ms wait
        if (ret <= 0) {
            continue;
        }

        // 3. Check ASE query socket
        if (m_querySocketFd >= 0 && (fds[1].revents & POLLIN)) {
            fromLen = sizeof(fromAddr);
            ssize_t received = recvfrom(m_querySocketFd, buffer, sizeof(buffer), 0,
                                        reinterpret_cast<struct sockaddr*>(&fromAddr), &fromLen);
            if (received > 0) {
                MTA_DEBUG("[NET-THREAD] Paquete ASE recibido: %zd bytes de %s:%u",
                          received, inet_ntoa(fromAddr.sin_addr), ntohs(fromAddr.sin_port));
                HandleAseResponse(buffer, static_cast<size_t>(received));
            }
        }

        // 4. Check main game socket
        if (m_socketFd >= 0 && (fds[0].revents & POLLIN)) {
            fromLen = sizeof(fromAddr);
            ssize_t received = recvfrom(m_socketFd, buffer, sizeof(buffer), 0,
                                        reinterpret_cast<struct sockaddr*>(&fromAddr), &fromLen);
            if (received > 0) {
                uint8_t packetIdRaw = buffer[0];
                eMtaPacketId packetId = static_cast<eMtaPacketId>(packetIdRaw);

                MTA_DEBUG("[NET-THREAD] Paquete de juego recibido: %zd bytes de %s:%u, PacketID=0x%02X",
                          received, inet_ntoa(fromAddr.sin_addr), ntohs(fromAddr.sin_port), packetIdRaw);

                NetBitStream stream(buffer + 1, static_cast<size_t>(received - 1));

                auto it = m_handlers.find(packetIdRaw);
                if (it != m_handlers.end()) {
                    it->second(packetId, stream);
                } else {
                    switch (packetId) {
                        case PACKET_ID_SERVER_JOINED:
                            m_state = eMtaState::JOINING;
                            MTA_DEBUG("[NET-THREAD] PACKET_ID_SERVER_JOINED recibido");
                            MtaJNI::NotifyJavaState(static_cast<int>(eMtaState::JOINING), "Unido al servidor");
                            MtaJNI::NotifyJavaChat("MTA", "¡Conectado exitosamente al servidor!");
                            break;
                        case PACKET_ID_SERVER_JOIN_DATA:
                            m_state = eMtaState::INGAME;
                            MTA_DEBUG("[NET-THREAD] PACKET_ID_SERVER_JOIN_DATA recibido (INGAME)");
                            MtaJNI::NotifyJavaState(static_cast<int>(eMtaState::INGAME), "En juego (MTA)");
                            // Trigger game startup and menu dismissal immediately
                            MtaGameInterface::Get().ForceGameStart();
                            break;
                        case PACKET_ID_SERVER_QUIT:
                            MTA_DEBUG("[NET-THREAD] PACKET_ID_SERVER_QUIT recibido");
                            MtaJNI::NotifyJavaChat("MTA", "El servidor cerró la conexión.");
                            Disconnect();
                            break;
                        default:
                            MTA_DEBUG("[NET-THREAD] Paquete ID 0x%02X sin handler registrado", packetIdRaw);
                            break;
                    }
                }
            }
        }
    }

    MTA_DEBUG("[NET-THREAD] Background network worker thread finalizado.");
}

void MtaNetwork::Process() {
    // Handled by worker thread
}
