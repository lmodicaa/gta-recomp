#include "MtaClient.h"
#include "mta/game/MtaGameInterface.h"
#include "mta/multiplayer/MtaPlayerManager.h"
#include "mta/ui/MtaUI.h"
#include "mta/ui/MtaJNI.h"
#include "vendor/log/log.h"
#include <chrono>
#include <cstdint>
#include <cinttypes>
#include <sstream>
#include <cctype>
#include <cmath>
#include <sys/syscall.h>
#include <unistd.h>

static inline pid_t CurrentTid() {
    return static_cast<pid_t>(syscall(SYS_gettid));
}

MtaClient& MtaClient::Get() {
    static MtaClient instance;
    return instance;
}

MtaClient::MtaClient()
    : m_initialized(false),
      m_3dWorldReady(false),
      m_playerSpawned(false),
      m_lastStartAttempt(0),
      m_startAttempts(0),
      m_activeVehicleModel(-1),
      m_activeVehicleAttempts(0),
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
    Log("[MTA-CLIENT] Initialize() called (this=%p, tid=%d, already_init=%d)",
        this, CurrentTid(), m_initialized);
    if (m_initialized) return;

    Log("[MTA-CLIENT] ========================================");
    Log("[MTA-CLIENT] Initializing Multi Theft Auto Android (this=%p)...", this);
    Log("[MTA-CLIENT] Protocol: MTA:SA Blue Core");
    Log("[MTA-CLIENT] ========================================");

    EnsureGameInterface();
    MtaPlayerManager::Get().Initialize();
    MtaUI::Get().Initialize();

    m_initialized = true;
}

void MtaClient::EnsureGameInterface() {
    if (!MtaGameInterface::Get().IsInitialized()) {
        MtaGameInterface::Get().Initialize();
    }
    if (!m_3dWorldReady && MtaGameInterface::Get().GetLocalPlayerPed() != nullptr) {
        m_3dWorldReady = true;
        Log("[MTA-CLIENT] 3D world detected ready via active player ped (this=%p).", this);
    }
}

void MtaClient::TriggerPlayerSpawn() {
    m_playerSpawned = false;
    m_startAttempts = 0;
    m_lastStartAttempt = 0;
}

void MtaClient::RequestSpawnVehicle(int modelId) {
    if (modelId < 400 || modelId > 611) {
        Log("[MTA-VEH] request rejected: invalid model %d (expected 400..611)", modelId);
        return;
    }
    if (!m_initialized) Initialize();

    // [MTA-VEH] Producer: push into the mutex-protected queue (callable from any thread).
    size_t queueSize = 0;
    {
        std::lock_guard<std::mutex> lock(m_vehicleQueueMutex);
        if (m_vehicleQueue.size() >= 8) {
            Log("[MTA-VEH] ER: spawn queue full (%zu), dropping model %d", m_vehicleQueue.size(), modelId);
            MtaJNI::NotifyJavaChat("MTA", "Cola de spawn llena, ignorado.");
            return;
        }
        m_vehicleQueue.push_back(modelId);
        queueSize = m_vehicleQueue.size();
    }
    Log("[MTA-CLIENT] Vehicle spawn queued: Model %d", modelId);
    Log("[MTA-VEH] request queued (model=%d, queueSize=%zu, this=%p, &queue=%p, &mutex=%p, tid=%d)",
        modelId, queueSize, this, &m_vehicleQueue, &m_vehicleQueueMutex, CurrentTid());
}

void MtaClient::RequestSetSkin(int skinId) {
    if (skinId < 0 || skinId > 312) return;
    if (!m_initialized) Initialize();
    m_pendingSkinId = skinId;
    m_pendingSkinTicks = 0;
    Log("[MTA-CLIENT] Skin change queued: ID %d", skinId);
}

// [MTA-VEH] Vehicle spawn queue consumer — drained once per frame from the game-loop
// hooks (hook_Idle / hook_CGame_Process), never from the UI thread.
//
// Binary facts (libGTASA.so ARM64 — llvm-objdump + llvm-readelf, verified):
//  - CStreaming::RequestModel(int,int) @0x3949e0: bit 0x2 = GAME_REQUIRED.
//  - CStreaming::LoadAllRequestedModels(bool) @0x396b28: loops FlushChannels/
//    ProcessLoadingChannel/RequestModelStream and clears its re-entrancy flag at the
//    end (0x396cf4), so calling it every frame is safe.
//  - CStreaming::ms_aInfoForModel @0x8f2184: stride 20, m_nLoadState at +0x10,
//    LOADSTATE_LOADED == 1.
//  - CCarCtrl::CreateCarForScript(int, CVector, unsigned char) @0x3afa70: returns the
//    CVehicle*, sets the world position itself (z clamped with ground query + collision
//    radius), joins the road system and calls CWorld::Add INTERNALLY via
//    PLT 0x22b1e0 -> GOT 0x8469a0 -> _ZN6CWorld3AddEP7CEntity @0x507518 (readelf).
//    => do NOT call CWorld::Add again, or the entity is inserted twice.
//  - CCarCtrl::PossiblyRemoveVehicle @0x3b0410 (sole caller: CCarCtrl::RemoveDistantCars)
//    early-outs while the byte at CVehicle+0x5E9 is non-zero — the script-owned counter
//    that SA script handlers and CTaskSimpleCarSetPedInAsPassenger maintain.
void MtaClient::ProcessPendingVehicleSpawn() {
    pid_t tid = CurrentTid();

    // Skip check 1: null instance (repeated every 120 calls)
    if (this == nullptr) {
        static uint64_t s_nullThisCount = 0;
        if (++s_nullThisCount % 120 == 1) {
            Log("[MTA-VEH] drain skipped: this pointer is null (n=%" PRIu64 ")", s_nullThisCount);
        }
        return;
    }

    EnsureGameInterface();

    // Skip check 2: GameInterface symbol resolution (repeated every 120 calls)
    if (!MtaGameInterface::Get().IsInitialized()) {
        static uint64_t s_giNotInitCount = 0;
        if (++s_giNotInitCount % 120 == 1) {
            Log("[MTA-VEH] drain skipped: MtaGameInterface resolution failed (n=%" PRIu64 ", this=%p, tid=%d)",
                s_giNotInitCount, this, tid);
        }
        return;
    }

    // Skip check 3: 3D world readiness (repeated every 120 calls)
    if (!m_3dWorldReady) {
        static uint64_t s_worldNotReadyCount = 0;
        if (++s_worldNotReadyCount % 120 == 1) {
            Log("[MTA-VEH] drain skipped: 3D world not ready yet (waiting for CGame::Initialise, n=%" PRIu64 ", this=%p, tid=%d)",
                s_worldNotReadyCount, this, tid);
        }
        return;
    }

    // One-time log: state of m_initialized and GameInterface at first drain after 3D world ready
    static bool s_firstDrainAfterWorldReady = false;
    if (!s_firstDrainAfterWorldReady) {
        s_firstDrainAfterWorldReady = true;
        Log("[MTA-VEH] first drain after 3D world ready: m_initialized=%d, gameInterfaceReady=%d (this=%p, tid=%d)",
            m_initialized, MtaGameInterface::Get().IsInitialized(), this, tid);
    }

    // Heartbeat every 120 calls
    static uint64_t s_drainTick = 0;
    s_drainTick++;
    size_t curQueueSize = 0;
    {
        std::lock_guard<std::mutex> lock(m_vehicleQueueMutex);
        curQueueSize = m_vehicleQueue.size();
    }
    if (s_drainTick % 120 == 0) {
        Log("[MTA-VEH] drain tick n=%" PRIu64 " queueSize=%zu tid=%d this=%p &queue=%p &mutex=%p activeModel=%d",
            s_drainTick, curQueueSize, tid, this, &m_vehicleQueue, &m_vehicleQueueMutex, m_activeVehicleModel);
    }

    // Skip check 4: active request in flight (repeated every 120 calls)
    if (m_activeVehicleModel >= 0) {
        static uint64_t s_activeCount = 0;
        if (++s_activeCount % 120 == 1) {
            Log("[MTA-VEH] drain skipped: active request in progress (model=%d, attempts=%d, n=%" PRIu64 ")",
                m_activeVehicleModel, m_activeVehicleAttempts, s_activeCount);
        }
    } else {
        std::lock_guard<std::mutex> lock(m_vehicleQueueMutex);
        if (m_vehicleQueue.empty()) {
            return; // silent by design when idle
        }

        int modelId = m_vehicleQueue.front();
        m_vehicleQueue.pop_front();
        if (modelId < 400 || modelId > 611) {
            Log("[MTA-VEH] ER: invalid model %d dequeued, skipped", modelId);
            return;
        }
        m_activeVehicleModel = modelId;
        m_activeVehicleAttempts = 0;
        Log("[MTA-VEH] dequeued model %d (queueSize=%zu, this=%p, &queue=%p, &mutex=%p, tid=%d)",
            modelId, m_vehicleQueue.size(), this, &m_vehicleQueue, &m_vehicleQueueMutex, tid);
    }

    const int modelId = m_activeVehicleModel;

    // Phase A: wait for the model to stream in (retry every frame, budget 300 frames ~5 s).
    int loadState = MtaGameInterface::Get().GetModelLoadState(modelId);
    if (loadState != 1 /* LOADSTATE_LOADED */) {
        if (m_activeVehicleAttempts == 0 || (m_activeVehicleAttempts % 60 == 0)) {
            Log("[MTA-VEH] phase=load model=%d frame=%d loadState=%d (1=LOADSTATE_LOADED)",
                modelId, m_activeVehicleAttempts, loadState);
        }
        MtaGameInterface::Get().RequestModel(modelId, 2); // 2 = GAME_REQUIRED
        MtaGameInterface::Get().FlushModelRequests();     // LoadAllRequestedModels(false)
        m_activeVehicleAttempts++;
        if (m_activeVehicleAttempts > 300) {
            Log("[MTA-CLIENT] Timeout waiting for vehicle model %d to load (300 frames, loadState=%d)",
                modelId, loadState);
            MtaJNI::NotifyJavaChat("MTA", "Tiempo de espera agotado cargando vehiculo " + std::to_string(modelId));
            m_activeVehicleModel = -1;
        }
        return; // request stays active, retried next frame
    }

    if (m_activeVehicleAttempts != 0) {
        Log("[MTA-VEH] model %d loaded after %d frame(s) (loadState=1)", modelId, m_activeVehicleAttempts);
        m_activeVehicleAttempts = 0; // restart budget for the spawn phase below
    }

    // Phase B: wait for a valid player reference position (budget 120 frames), then spawn.
    CVector playerPos;
    if (!MtaGameInterface::Get().GetLocalPlayerPosition(playerPos)) {
        if (m_activeVehicleAttempts == 0 || (m_activeVehicleAttempts % 60 == 0)) {
            Log("[MTA-VEH] phase=spawn model=%d frame=%d ER: player position unavailable",
                modelId, m_activeVehicleAttempts);
        }
        m_activeVehicleAttempts++;
        if (m_activeVehicleAttempts > 120) {
            Log("[MTA-VEH] ER: dropped model %d (no player position after 120 frames)", modelId);
            MtaJNI::NotifyJavaChat("MTA", "No hay jugador para colocar el vehiculo " + std::to_string(modelId));
            m_activeVehicleModel = -1;
        }
        return; // retried next frame while the budget lasts
    }

    // Spawn 5 m in front of the player using the SA heading convention
    // (forward = (-sin(h), cos(h))), 1 m above the player's z.
    // CreateCarForScript re-clamps z itself (ground fallback + collision radius).
    float heading = MtaGameInterface::Get().GetLocalPlayerHeading();
    CVector spawnPos(
        playerPos.x - sinf(heading) * 5.0f,
        playerPos.y + cosf(heading) * 5.0f,
        playerPos.z + 1.0f
    );

    typedef void* (*CreateCar_t)(int, CVector, unsigned char);
    static CreateCar_t pfn_CreateCar = nullptr;
    static bool s_createResolvAttempted = false;
    if (!s_createResolvAttempted) {
        s_createResolvAttempted = true;
        void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_NOW);
        if (!handle) handle = RTLD_DEFAULT;
        pfn_CreateCar = reinterpret_cast<CreateCar_t>(dlsym(handle, "_ZN8CCarCtrl18CreateCarForScriptEi7CVectorh"));
        if (!pfn_CreateCar) Log("[MTA-VEH] ER: dlsym(_ZN8CCarCtrl18CreateCarForScriptEi7CVectorh) failed");
    }
    if (!pfn_CreateCar) {
        Log("[MTA-CLIENT] Cannot resolve CCarCtrl::CreateCarForScript");
        MtaJNI::NotifyJavaChat("MTA", "Error: No se pudo resolver CreateCarForScript.");
        m_activeVehicleModel = -1;
        return;
    }

    void* pVeh = pfn_CreateCar(modelId, spawnPos, 1);
    if (!pVeh) {
        Log("[MTA-VEH] ER: CreateCarForScript returned null for model %d", modelId);
        MtaJNI::NotifyJavaChat("MTA", "Error al instanciar vehiculo " + std::to_string(modelId));
        m_activeVehicleModel = -1;
        return;
    }

    // Align the fresh vehicle with the player's heading (matrix at CEntity+0x18, ARM64).
    MtaGameInterface::Get().SetEntityHeading(pVeh, heading);

    // Mark script-owned so the population/traffic cleanup never deletes it:
    // byte at CVehicle+0x5E9 == 1 -> CCarCtrl::PossiblyRemoveVehicle early-outs forever
    // (provided flag only decremented by script-ped-in-car tasks, which we never create).
    *(reinterpret_cast<uint8_t*>(pVeh) + 0x5E9) = 1;

    Log("[MTA-VEH] spawned model %d ptr=%p pos=(%.2f, %.2f, %.2f) heading=%.2f "
        "(world-add done inside CreateCarForScript; script-owned flag CVehicle+0x5E9=1)",
        modelId, pVeh, spawnPos.x, spawnPos.y, spawnPos.z, heading);
    MtaJNI::NotifyJavaChat("MTA", "Vehiculo " + std::to_string(modelId) + " creado.");

    m_activeVehicleModel = -1; // done; next request (if any) starts next frame
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

    // Pending skin change still drains here (plain-int slot, same thread as Update()).
    // The vehicle queue must NOT drain here: with the 4 per-frame hooks this path proved
    // unreliable (tick-0 logs never appeared), so ProcessPendingVehicleSpawn() is now
    // invoked directly by the game-loop hooks in MtaHooks.cpp — see MtaHooks.cpp.
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
    m_3dWorldReady = false;
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
