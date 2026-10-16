#include "MtaClient.h"
#include "mta/game/MtaGameInterface.h"
#include "mta/multiplayer/MtaPlayerManager.h"
#include "mta/ui/MtaUI.h"
#include "mta/ui/MtaJNI.h"
#include "vendor/log/log.h"
#include <chrono>
#include <sstream>
#include <cctype>
#include <cmath>

MtaClient& MtaClient::Get() {
    static MtaClient instance;
    return instance;
}

MtaClient::MtaClient()
    : m_initialized(false),
      m_playerSpawned(false),
      m_lastStartAttempt(0),
      m_startAttempts(0),
      m_pendingVehicleModel(-1),
      m_pendingVehicleTicks(0),
      m_pendingSkinId(-1),
      m_pendingSkinTicks(0) {
}

MtaClient::~MtaClient() {
    Shutdown();
}

static uint32_t GetCurrentTimeMs() {
    auto now = std::chrono::steady_clock::now();
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
}

void MtaClient::Initialize() {
    if (m_initialized) return;

    Log("[MTA-CLIENT] ========================================");
    Log("[MTA-CLIENT] Initializing Multi Theft Auto Android...");
    Log("[MTA-CLIENT] Protocol: MTA:SA Blue Core");
    Log("[MTA-CLIENT] ========================================");

    MtaGameInterface::Get().Initialize();
    MtaPlayerManager::Get().Initialize();
    MtaUI::Get().Initialize();

    m_initialized = true;
}

void MtaClient::TriggerPlayerSpawn() {
    m_playerSpawned = false;
    m_startAttempts = 0;
    m_lastStartAttempt = 0;
}

void MtaClient::RequestSpawnVehicle(int modelId) {
    if (modelId < 400 || modelId > 611) return;
    if (!m_initialized) Initialize();
    m_pendingVehicleModel = modelId;
    m_pendingVehicleTicks = 0;
    Log("[MTA-CLIENT] Vehicle spawn queued: Model %d", modelId);
}

void MtaClient::RequestSetSkin(int skinId) {
    if (skinId < 0 || skinId > 312) return;
    if (!m_initialized) Initialize();
    m_pendingSkinId = skinId;
    m_pendingSkinTicks = 0;
    Log("[MTA-CLIENT] Skin change queued: ID %d", skinId);
}

void MtaClient::ProcessPendingVehicleSpawn() {
    if (m_pendingVehicleModel < 400 || m_pendingVehicleModel > 611) return;

    int modelId = m_pendingVehicleModel;

    if (m_pendingVehicleTicks == 0 || (m_pendingVehicleTicks % 60 == 0)) {
        Log("[MTA-CLIENT] ProcessPendingVehicleSpawn: model=%d, tick=%d, loaded=%d",
            modelId, m_pendingVehicleTicks, MtaGameInterface::Get().HasModelLoaded(modelId));
    }

    if (!MtaGameInterface::Get().HasModelLoaded(modelId)) {
        MtaGameInterface::Get().RequestModel(modelId, 2);
        m_pendingVehicleTicks++;
        if (m_pendingVehicleTicks > 300) { // ~5 seconds timeout (at 60fps)
            Log("[MTA-CLIENT] Timeout waiting for vehicle model %d to load", modelId);
            MtaJNI::NotifyJavaChat("MTA", "Tiempo de espera agotado cargando vehiculo " + std::to_string(modelId));
            m_pendingVehicleModel = -1;
            m_pendingVehicleTicks = 0;
        }
        return;
    }

    // Model loaded! Spawn vehicle cleanly on the game thread
    m_pendingVehicleModel = -1;
    m_pendingVehicleTicks = 0;

    void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_NOW);
    if (!handle) handle = RTLD_DEFAULT;

    typedef void* (*CreateCar_t)(int, CVector, unsigned char);
    CreateCar_t pfn_CreateCar = reinterpret_cast<CreateCar_t>(dlsym(handle, "_ZN8CCarCtrl18CreateCarForScriptEi7CVectorh"));

    if (!pfn_CreateCar) {
        Log("[MTA-CLIENT] Cannot resolve CCarCtrl::CreateCarForScript");
        MtaJNI::NotifyJavaChat("MTA", "Error: No se pudo resolver CreateCarForScript.");
        return;
    }

    CVector playerPos;
    if (MtaGameInterface::Get().GetLocalPlayerPosition(playerPos)) {
        float heading = MtaGameInterface::Get().GetLocalPlayerHeading();

        // Spawn 5 meters directly ahead of the player based on their heading
        CVector spawnPos(
            playerPos.x - sinf(heading) * 5.0f,
            playerPos.y + cosf(heading) * 5.0f,
            playerPos.z + 0.5f
        );

        void* pVeh = pfn_CreateCar(modelId, spawnPos, 1);
        if (pVeh) {
            MtaGameInterface::Get().SetEntityHeading(pVeh, heading);
            Log("[MTA-CLIENT] Vehicle %d spawned at (%.2f, %.2f, %.2f) ptr=%p",
                modelId, spawnPos.x, spawnPos.y, spawnPos.z, pVeh);
            MtaJNI::NotifyJavaChat("MTA", "Vehiculo " + std::to_string(modelId) + " creado.");
        } else {
            Log("[MTA-CLIENT] CCarCtrl::CreateCarForScript returned null for model %d", modelId);
            MtaJNI::NotifyJavaChat("MTA", "Error al instanciar vehiculo " + std::to_string(modelId));
        }
    }
}

void MtaClient::ProcessPendingSkinChange() {
    if (m_pendingSkinId < 0 || m_pendingSkinId > 312) return;

    int skinId = m_pendingSkinId;

    if (m_pendingSkinTicks == 0 || (m_pendingSkinTicks % 60 == 0)) {
        Log("[MTA-CLIENT] ProcessPendingSkinChange: skin=%d, tick=%d, loaded=%d",
            skinId, m_pendingSkinTicks, MtaGameInterface::Get().HasModelLoaded(skinId));
    }

    if (!MtaGameInterface::Get().HasModelLoaded(skinId)) {
        MtaGameInterface::Get().RequestModel(skinId, 2);
        m_pendingSkinTicks++;
        if (m_pendingSkinTicks > 300) {
            Log("[MTA-CLIENT] Timeout waiting for skin model %d to load", skinId);
            MtaJNI::NotifyJavaChat("MTA", "Tiempo de espera agotado cargando skin " + std::to_string(skinId));
            m_pendingSkinId = -1;
            m_pendingSkinTicks = 0;
        }
        return;
    }

    m_pendingSkinId = -1;
    m_pendingSkinTicks = 0;

    void* pPed = MtaGameInterface::Get().GetLocalPlayerPed();
    if (!pPed) {
        Log("[MTA-CLIENT] Player ped is null when changing skin");
        return;
    }

    void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_NOW);
    if (!handle) handle = RTLD_DEFAULT;

    typedef void (*SetModelIndex_t)(void*, unsigned int);
    SetModelIndex_t pfn_SetModelIndex = reinterpret_cast<SetModelIndex_t>(dlsym(handle, "_ZN4CPed13SetModelIndexEj"));
    if (!pfn_SetModelIndex) {
        pfn_SetModelIndex = reinterpret_cast<SetModelIndex_t>(dlsym(handle, "_ZN7CEntity13SetModelIndexEj"));
    }

    if (pfn_SetModelIndex) {
        pfn_SetModelIndex(pPed, static_cast<unsigned int>(skinId));
        Log("[MTA-CLIENT] Skin changed to %d successfully!", skinId);
        MtaJNI::NotifyJavaChat("MTA", "Skin cambiada a " + std::to_string(skinId) + ".");
    } else {
        Log("[MTA-CLIENT] SetModelIndex symbol could not be resolved");
    }
}

void MtaClient::Update() {
    if (!m_initialized) return;

    // Process network packets and player sync
    MtaNetwork::Get().Process();
    MtaPlayerManager::Get().Update();

    // Process async game actions on game thread
    ProcessPendingVehicleSpawn();
    ProcessPendingSkinChange();

    // Check if player joined MTA multiplayer and needs to enter the world
    if (MtaNetwork::Get().GetState() == eMtaState::INGAME && !m_playerSpawned) {
        void* pPed = MtaGameInterface::Get().GetLocalPlayerPed();
        if (pPed != nullptr) {
            // Spawn local player at multiplayer hub (Los Santos City Hall / Conference Center)
            MtaGameInterface::Get().SpawnLocalPlayer(CVector(1481.0f, -1771.0f, 18.7f));
            m_playerSpawned = true;
            Log("[MTA-CLIENT] Multiplayer world active. Player spawned successfully.");

            // Dismiss menu to bring 3D world into view
            MtaGameInterface::Get().DismissMenu();
        }
    }
}

void MtaClient::Shutdown() {
    if (!m_initialized) return;

    Log("[MTA-CLIENT] Shutting down MTA runtime...");
    MtaPlayerManager::Get().Clear();
    MtaNetwork::Get().Disconnect();
    m_playerSpawned = false;
    MtaGameInterface::Get().ResetGameState();
    m_initialized = false;
}

void MtaClient::ConnectToServer(const std::string& host, uint16_t port) {
    if (!m_initialized) Initialize();
    m_playerSpawned = false;
    MtaPlayerManager::Get().Clear();
    MtaGameInterface::Get().ResetGameState();
    MtaNetwork::Get().Connect(host, port);
}

void MtaClient::Disconnect() {
    m_playerSpawned = false;
    m_startAttempts = 0;
    MtaPlayerManager::Get().Clear();
    MtaNetwork::Get().Disconnect();
}

bool MtaClient::ProcessCommand(const std::string& cmdText) {
    if (cmdText.empty()) return false;

    std::stringstream ss(cmdText);
    std::string cmd;
    ss >> cmd;

    for (auto& c : cmd) {
        c = static_cast<char>(std::tolower(c));
    }

    if (cmd == "/v" || cmd == "/car" || cmd == "/veh" || cmd == "/vehiculo") {
        int modelId = 411; // Default: Infernus
        if (ss >> modelId) {
            if (modelId < 400 || modelId > 611) {
                MtaJNI::NotifyJavaChat("MTA", "ID invalido. Los vehiculos van de 400 a 611.");
                return true;
            }
        }
        RequestSpawnVehicle(modelId);
        return true;
    }

    if (cmd == "/skin") {
        int skinId = 0;
        if (ss >> skinId) {
            if (skinId < 0 || skinId > 312) {
                MtaJNI::NotifyJavaChat("MTA", "ID invalido. Las skins van de 0 a 312.");
                return true;
            }
            RequestSetSkin(skinId);
        } else {
            MtaJNI::NotifyJavaChat("MTA", "Uso: /skin [0-312]");
        }
        return true;
    }

    if (cmd == "/kill" || cmd == "/suicidio") {
        MtaGameInterface::Get().SetLocalPlayerHealth(0.0f);
        MtaJNI::NotifyJavaChat("MTA", "Te has suicidado.");
        return true;
    }

    if (cmd == "/heal" || cmd == "/vida") {
        MtaGameInterface::Get().SetLocalPlayerHealth(100.0f);
        MtaGameInterface::Get().SetLocalPlayerArmour(100.0f);
        MtaJNI::NotifyJavaChat("MTA", "Vida y chaleco restaurados al 100%.");
        return true;
    }

    if (cmd == "/pos" || cmd == "/coords") {
        CVector pos;
        if (MtaGameInterface::Get().GetLocalPlayerPosition(pos)) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Posicion: X=%.2f, Y=%.2f, Z=%.2f", pos.x, pos.y, pos.z);
            MtaJNI::NotifyJavaChat("MTA", std::string(buf));
        } else {
            MtaJNI::NotifyJavaChat("MTA", "No se pudo obtener la posicion.");
        }
        return true;
    }

    if (cmd == "/tp" || cmd == "/teleport") {
        float x, y, z;
        if (ss >> x >> y >> z) {
            MtaGameInterface::Get().SetLocalPlayerPosition(CVector(x, y, z));
            char buf[128];
            snprintf(buf, sizeof(buf), "Teletransportado a (%.2f, %.2f, %.2f).", x, y, z);
            MtaJNI::NotifyJavaChat("MTA", std::string(buf));
        } else {
            MtaJNI::NotifyJavaChat("MTA", "Uso: /tp [X] [Y] [Z]");
        }
        return true;
    }

    if (cmd == "/help" || cmd == "/ayuda" || cmd == "/comandos") {
        MtaJNI::NotifyJavaChat("MTA", "--- Comandos MTA ---");
        MtaJNI::NotifyJavaChat("MTA", "/v [400-611] - Crear vehiculo");
        MtaJNI::NotifyJavaChat("MTA", "/skin [0-312] - Cambiar skin");
        MtaJNI::NotifyJavaChat("MTA", "/heal - Curar vida y chaleco");
        MtaJNI::NotifyJavaChat("MTA", "/kill - Suicidarse");
        MtaJNI::NotifyJavaChat("MTA", "/pos - Ver coordenadas actuales");
        MtaJNI::NotifyJavaChat("MTA", "/tp [X] [Y] [Z] - Teletransporte");
        return true;
    }

    MtaJNI::NotifyJavaChat("MTA", "Comando desconocido: " + cmd + ". Escribe /ayuda");
    return false;
}
