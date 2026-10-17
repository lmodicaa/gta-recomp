# Fix de spawn vehicular (/v 522, /v 444) — MTA client sobre gta-recomp (libGTASA.so ARM64)

Estado: **compilado OK** (`gradlew :app:externalNativeBuildDebug`, ABIs arm64-v8a y armeabi-v7a, 2026-10-16).

## 1) Diagnóstico: por qué no aparecía el vehículo

Evidencia verificada en \pp/src/main/jniLibs/arm64-v8a/libGTASA.so\ (llvm-objdump + llvm-readelf, sin decompilador):

| # | Hecho | Evidencia |
|---|-------|-----------|
| 1 | El consumidor viejo (\ProcessPendingVehicleSpawn\) solo corría vía \MtaClient::Update()\ | Código fuente; el tick-0 nunca apareció en tus logs |
| 2 | \Idle(void*,bool)\ @0x4d8e60 es el game loop: llama a \CGame::Process()\ vía PLT 0x21e1d0 en 0x4d8f80 (**único llamador de CGame::Process** en todo el .text) y a \RenderScene(bool)\ en 0x4d9064 (**único llamador**) | disasm .text completo |
| 3 | \Idle\ se dispara por frame desde \AppEventHandler\ (evento RW rsIDLE=26) y \Pump_SwapBuffers\ | disasm 0x4d9438/0x4d926c |
| 4 | \CCarCtrl::CreateCarForScript\ @0x3afa70 **ya llama él mismo a CWorld::Add** internamente: \l 0x22b1e0\ → GOT 0x8469a0 → \_ZN6CWorld3AddEP7CEntity\ @0x507518 (readelf JUMP_SLOT, ordinal 4659). En AMBAS ramas (ped car / no-ped) | disasm + readelf |
| 5 | \CreateCarForScript\ **no comprueba el estado de streaming**; el llamador (/scripts) pide el modelo antes | disasm (queda constatado) |
| 6 | \CreateCarForScript\ reposiciona la entidad él mismo: z<=límite → re-consulta suelo; luego z += radio de la colmodel | disasm |
| 7 | \CStreaming::LoadAllRequestedModels(bool)\ @0x396b28: \ool\ en w0, semi-síncrono (usa FlushChannels/ProcessLoadingChannel/RequestModelStream) y **limpia su bandera de reentrada al final** (0x396cf4) → seguro por frame | disasm |
| 8 | \CStreaming::ms_aInfoForModel\ @0x8f2184: stride 20; \m_nLoadState\ en +0x10; 1 = LOADSTATE_LOADED; 0x2 = GAME_REQUIRED | disasm de RequestModel |
| 9 | Anti-borrado: \CCarCtrl::PossiblyRemoveVehicle\ @0x3b0410 early-return si el byte en *"CVehicle+0x5E9"* \!= 0; el **único** llamador es \CCarCtrl::RemoveDistantCars\ (0x3b0364) | disasm |
| 10 | El byte +0x5E9 es el contador "script-owned": los handlers de SA y \CTaskSimpleCarSetPedInAsPassenger::ProcessPed\ lo incrementan/decrementan | disasm |

**Conclusión:** la cola nunca se drenaba en el hilo del juego (el drenaje dependía de `Update()` + `m_initialized` + red), y aun si se drenara, el flujo creaba el coche sin marcarlo script-owned y sin garantía de modelo cargado. El fix mueve el drenaje al hilo del juego (hooks \hook_Idle\/\hook_CGame_Process\), con cola \std::deque<int>\ + \std::mutex\, flush síncrono del streaming y marca anti-borrado. NOTA: **no** se añade \CWorld::Add\ manual — es doble inserción (hecho #4).

## 2) Diff completo

\\\diff
diff --git a/app/src/main/cpp/src/mta/core/MtaClient.cpp b/app/src/main/cpp/src/mta/core/MtaClient.cpp
index d909dd1..8d87462 100644
--- a/app/src/main/cpp/src/mta/core/MtaClient.cpp
+++ b/app/src/main/cpp/src/mta/core/MtaClient.cpp
@@ -5,6 +5,7 @@
 #include "mta/ui/MtaJNI.h"
 #include "vendor/log/log.h"
 #include <chrono>
+#include <cstdint>
 #include <sstream>
 #include <cctype>
 #include <cmath>
@@ -19,8 +20,8 @@ MtaClient::MtaClient()
       m_playerSpawned(false),
       m_lastStartAttempt(0),
       m_startAttempts(0),
-      m_pendingVehicleModel(-1),
-      m_pendingVehicleTicks(0),
+      m_activeVehicleModel(-1),
+      m_activeVehicleAttempts(0),
       m_pendingSkinId(-1),
       m_pendingSkinTicks(0) {
 }
@@ -58,9 +59,21 @@ void MtaClient::TriggerPlayerSpawn() {
 void MtaClient::RequestSpawnVehicle(int modelId) {
     if (modelId < 400 || modelId > 611) return;
     if (!m_initialized) Initialize();
-    m_pendingVehicleModel = modelId;
-    m_pendingVehicleTicks = 0;
+
+    // [MTA-VEH] Producer: push into the mutex-protected queue (callable from any thread).
+    size_t queueSize = 0;
+    {
+        std::lock_guard<std::mutex> lock(m_vehicleQueueMutex);
+        if (m_vehicleQueue.size() >= 8) {
+            Log("[MTA-VEH] ER: spawn queue full (%zu), dropping model %d", m_vehicleQueue.size(), modelId);
+            MtaJNI::NotifyJavaChat("MTA", "Cola de spawn llena, ignorado.");
+            return;
+        }
+        m_vehicleQueue.push_back(modelId);
+        queueSize = m_vehicleQueue.size();
+    }
     Log("[MTA-CLIENT] Vehicle spawn queued: Model %d", modelId);
+    Log("[MTA-VEH] request queued (model=%d, queueSize=%zu)", modelId, queueSize);
 }
 
 void MtaClient::RequestSetSkin(int skinId) {
@@ -71,66 +84,131 @@ void MtaClient::RequestSetSkin(int skinId) {
     Log("[MTA-CLIENT] Skin change queued: ID %d", skinId);
 }
 
+// [MTA-VEH] Vehicle spawn queue consumer ΓÇö drained once per frame from the game-loop
+// hooks (hook_Idle / hook_CGame_Process), never from the UI thread.
+//
+// Binary facts (libGTASA.so ARM64 ΓÇö llvm-objdump + llvm-readelf, verified):
+//  - CStreaming::RequestModel(int,int) @0x3949e0: bit 0x2 = GAME_REQUIRED.
+//  - CStreaming::LoadAllRequestedModels(bool) @0x396b28: loops FlushChannels/
+//    ProcessLoadingChannel/RequestModelStream and clears its re-entrancy flag at the
+//    end (0x396cf4), so calling it every frame is safe.
+//  - CStreaming::ms_aInfoForModel @0x8f2184: stride 20, m_nLoadState at +0x10,
+//    LOADSTATE_LOADED == 1.
+//  - CCarCtrl::CreateCarForScript(int, CVector, unsigned char) @0x3afa70: returns the
+//    CVehicle*, sets the world position itself (z clamped with ground query + collision
+//    radius), joins the road system and calls CWorld::Add INTERNALLY via
+//    PLT 0x22b1e0 -> GOT 0x8469a0 -> _ZN6CWorld3AddEP7CEntity @0x507518 (readelf).
+//    => do NOT call CWorld::Add again, or the entity is inserted twice.
+//  - CCarCtrl::PossiblyRemoveVehicle @0x3b0410 (sole caller: CCarCtrl::RemoveDistantCars)
+//    early-outs while the byte at CVehicle+0x5E9 is non-zero ΓÇö the script-owned counter
+//    that SA script handlers and CTaskSimpleCarSetPedInAsPassenger maintain.
 void MtaClient::ProcessPendingVehicleSpawn() {
-    if (m_pendingVehicleModel < 400 || m_pendingVehicleModel > 611) return;
-
-    int modelId = m_pendingVehicleModel;
-
-    if (m_pendingVehicleTicks == 0 || (m_pendingVehicleTicks % 60 == 0)) {
-        Log("[MTA-CLIENT] ProcessPendingVehicleSpawn: model=%d, tick=%d, loaded=%d",
-            modelId, m_pendingVehicleTicks, MtaGameInterface::Get().HasModelLoaded(modelId));
+    if (m_activeVehicleModel < 0) {
+        std::lock_guard<std::mutex> lock(m_vehicleQueueMutex);
+        if (m_vehicleQueue.empty()) return; // idle per-frame path: silent by design
+
+        int modelId = m_vehicleQueue.front();
+        m_vehicleQueue.pop_front();
+        if (modelId < 400 || modelId > 611) {
+            Log("[MTA-VEH] ER: invalid model %d dequeued, skipped", modelId);
+            return;
+        }
+        m_activeVehicleModel = modelId;
+        m_activeVehicleAttempts = 0;
+        Log("[MTA-VEH] dequeued model %d (queueSize=%zu)", modelId, m_vehicleQueue.size());
     }
 
-    if (!MtaGameInterface::Get().HasModelLoaded(modelId)) {
-        MtaGameInterface::Get().RequestModel(modelId, 2);
-        m_pendingVehicleTicks++;
-        if (m_pendingVehicleTicks > 300) { // ~5 seconds timeout (at 60fps)
-            Log("[MTA-CLIENT] Timeout waiting for vehicle model %d to load", modelId);
+    const int modelId = m_activeVehicleModel;
+
+    // Phase A: wait for the model to stream in (retry every frame, budget 300 frames ~5 s).
+    int loadState = MtaGameInterface::Get().GetModelLoadState(modelId);
+    if (loadState != 1 /* LOADSTATE_LOADED */) {
+        if (m_activeVehicleAttempts == 0 || (m_activeVehicleAttempts % 60 == 0)) {
+            Log("[MTA-VEH] phase=load model=%d frame=%d loadState=%d (1=LOADSTATE_LOADED)",
+                modelId, m_activeVehicleAttempts, loadState);
+        }
+        MtaGameInterface::Get().RequestModel(modelId, 2); // 2 = GAME_REQUIRED
+        MtaGameInterface::Get().FlushModelRequests();     // LoadAllRequestedModels(false)
+        m_activeVehicleAttempts++;
+        if (m_activeVehicleAttempts > 300) {
+            Log("[MTA-CLIENT] Timeout waiting for vehicle model %d to load (300 frames, loadState=%d)",
+                modelId, loadState);
             MtaJNI::NotifyJavaChat("MTA", "Tiempo de espera agotado cargando vehiculo " + std::to_string(modelId));
-            m_pendingVehicleModel = -1;
-            m_pendingVehicleTicks = 0;
+            m_activeVehicleModel = -1;
         }
-        return;
+        return; // request stays active, retried next frame
     }
 
-    // Model loaded! Spawn vehicle cleanly on the game thread
-    m_pendingVehicleModel = -1;
-    m_pendingVehicleTicks = 0;
+    if (m_activeVehicleAttempts != 0) {
+        Log("[MTA-VEH] model %d loaded after %d frame(s) (loadState=1)", modelId, m_activeVehicleAttempts);
+        m_activeVehicleAttempts = 0; // restart budget for the spawn phase below
+    }
 
-    void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_NOW);
-    if (!handle) handle = RTLD_DEFAULT;
+    // Phase B: wait for a valid player reference position (budget 120 frames), then spawn.
+    CVector playerPos;
+    if (!MtaGameInterface::Get().GetLocalPlayerPosition(playerPos)) {
+        if (m_activeVehicleAttempts == 0 || (m_activeVehicleAttempts % 60 == 0)) {
+            Log("[MTA-VEH] phase=spawn model=%d frame=%d ER: player position unavailable",
+                modelId, m_activeVehicleAttempts);
+        }
+        m_activeVehicleAttempts++;
+        if (m_activeVehicleAttempts > 120) {
+            Log("[MTA-VEH] ER: dropped model %d (no player position after 120 frames)", modelId);
+            MtaJNI::NotifyJavaChat("MTA", "No hay jugador para colocar el vehiculo " + std::to_string(modelId));
+            m_activeVehicleModel = -1;
+        }
+        return; // retried next frame while the budget lasts
+    }
 
-    typedef void* (*CreateCar_t)(int, CVector, unsigned char);
-    CreateCar_t pfn_CreateCar = reinterpret_cast<CreateCar_t>(dlsym(handle, "_ZN8CCarCtrl18CreateCarForScriptEi7CVectorh"));
+    // Spawn 5 m in front of the player using the SA heading convention
+    // (forward = (-sin(h), cos(h))), 1 m above the player's z.
+    // CreateCarForScript re-clamps z itself (ground fallback + collision radius).
+    float heading = MtaGameInterface::Get().GetLocalPlayerHeading();
+    CVector spawnPos(
+        playerPos.x - sinf(heading) * 5.0f,
+        playerPos.y + cosf(heading) * 5.0f,
+        playerPos.z + 1.0f
+    );
 
+    typedef void* (*CreateCar_t)(int, CVector, unsigned char);
+    static CreateCar_t pfn_CreateCar = nullptr;
+    static bool s_createResolvAttempted = false;
+    if (!s_createResolvAttempted) {
+        s_createResolvAttempted = true;
+        void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_NOW);
+        if (!handle) handle = RTLD_DEFAULT;
+        pfn_CreateCar = reinterpret_cast<CreateCar_t>(dlsym(handle, "_ZN8CCarCtrl18CreateCarForScriptEi7CVectorh"));
+        if (!pfn_CreateCar) Log("[MTA-VEH] ER: dlsym(_ZN8CCarCtrl18CreateCarForScriptEi7CVectorh) failed");
+    }
     if (!pfn_CreateCar) {
         Log("[MTA-CLIENT] Cannot resolve CCarCtrl::CreateCarForScript");
         MtaJNI::NotifyJavaChat("MTA", "Error: No se pudo resolver CreateCarForScript.");
+        m_activeVehicleModel = -1;
         return;
     }
 
-    CVector playerPos;
-    if (MtaGameInterface::Get().GetLocalPlayerPosition(playerPos)) {
-        float heading = MtaGameInterface::Get().GetLocalPlayerHeading();
-
-        // Spawn 5 meters directly ahead of the player based on their heading
-        CVector spawnPos(
-            playerPos.x - sinf(heading) * 5.0f,
-            playerPos.y + cosf(heading) * 5.0f,
-            playerPos.z + 0.5f
-        );
-
-        void* pVeh = pfn_CreateCar(modelId, spawnPos, 1);
-        if (pVeh) {
-            MtaGameInterface::Get().SetEntityHeading(pVeh, heading);
-            Log("[MTA-CLIENT] Vehicle %d spawned at (%.2f, %.2f, %.2f) ptr=%p",
-                modelId, spawnPos.x, spawnPos.y, spawnPos.z, pVeh);
-            MtaJNI::NotifyJavaChat("MTA", "Vehiculo " + std::to_string(modelId) + " creado.");
-        } else {
-            Log("[MTA-CLIENT] CCarCtrl::CreateCarForScript returned null for model %d", modelId);
-            MtaJNI::NotifyJavaChat("MTA", "Error al instanciar vehiculo " + std::to_string(modelId));
-        }
+    void* pVeh = pfn_CreateCar(modelId, spawnPos, 1);
+    if (!pVeh) {
+        Log("[MTA-VEH] ER: CreateCarForScript returned null for model %d", modelId);
+        MtaJNI::NotifyJavaChat("MTA", "Error al instanciar vehiculo " + std::to_string(modelId));
+        m_activeVehicleModel = -1;
+        return;
     }
+
+    // Align the fresh vehicle with the player's heading (matrix at CEntity+0x18, ARM64).
+    MtaGameInterface::Get().SetEntityHeading(pVeh, heading);
+
+    // Mark script-owned so the population/traffic cleanup never deletes it:
+    // byte at CVehicle+0x5E9 == 1 -> CCarCtrl::PossiblyRemoveVehicle early-outs forever
+    // (provided flag only decremented by script-ped-in-car tasks, which we never create).
+    *(reinterpret_cast<uint8_t*>(pVeh) + 0x5E9) = 1;
+
+    Log("[MTA-VEH] spawned model %d ptr=%p pos=(%.2f, %.2f, %.2f) heading=%.2f "
+        "(world-add done inside CreateCarForScript; script-owned flag CVehicle+0x5E9=1)",
+        modelId, pVeh, spawnPos.x, spawnPos.y, spawnPos.z, heading);
+    MtaJNI::NotifyJavaChat("MTA", "Vehiculo " + std::to_string(modelId) + " creado.");
+
+    m_activeVehicleModel = -1; // done; next request (if any) starts next frame
 }
 
 void MtaClient::ProcessPendingSkinChange() {
@@ -189,8 +267,10 @@ void MtaClient::Update() {
     MtaNetwork::Get().Process();
     MtaPlayerManager::Get().Update();
 
-    // Process async game actions on game thread
-    ProcessPendingVehicleSpawn();
+    // Pending skin change still drains here (plain-int slot, same thread as Update()).
+    // The vehicle queue must NOT drain here: with the 4 per-frame hooks this path proved
+    // unreliable (tick-0 logs never appeared), so ProcessPendingVehicleSpawn() is now
+    // invoked directly by the game-loop hooks in MtaHooks.cpp ΓÇö see MtaHooks.cpp.
     ProcessPendingSkinChange();
 
     // Check if player joined MTA multiplayer and needs to enter the world
diff --git a/app/src/main/cpp/src/mta/core/MtaClient.h b/app/src/main/cpp/src/mta/core/MtaClient.h
index 967f7a0..28df2d2 100644
--- a/app/src/main/cpp/src/mta/core/MtaClient.h
+++ b/app/src/main/cpp/src/mta/core/MtaClient.h
@@ -2,6 +2,8 @@
 
 #include "mta/net/MtaNetwork.h"
 #include <string>
+#include <mutex>
+#include <deque>
 
 class MtaClient {
 public:
@@ -26,11 +28,17 @@ public:
     void RequestSpawnVehicle(int modelId);
     void RequestSetSkin(int skinId);
 
+    // [MTA-VEH] Vehicle spawn queue consumer. Must be drained once per frame by the
+    // game-loop hooks (hook_Idle / hook_CGame_Process) ΓÇö NEVER via MtaClient::Update()
+    // or from the UI thread: streaming requests, CreateCarForScript and CVehicle field
+    // writes are only safe on the game thread.  One active request is processed across
+    // frames (retries while the model streams in); remaining requests stay in the queue.
+    void ProcessPendingVehicleSpawn();
+
 private:
     MtaClient();
     ~MtaClient();
 
-    void ProcessPendingVehicleSpawn();
     void ProcessPendingSkinChange();
 
     bool m_initialized;
@@ -38,8 +46,13 @@ private:
     uint32_t m_lastStartAttempt;
     uint32_t m_startAttempts;
 
-    int m_pendingVehicleModel;
-    int m_pendingVehicleTicks;
+    // [MTA-VEH] Vehicle spawn queue: producers (JNI/ui thread) push under mutex,
+    // game thread pops. Only the game thread touches the two "active" fields below.
+    std::deque<int> m_vehicleQueue;
+    std::mutex m_vehicleQueueMutex;
+    int m_activeVehicleModel;     // request being processed across frames; -1 = idle
+    int m_activeVehicleAttempts;  // frames spent on the active request phase
+
     int m_pendingSkinId;
     int m_pendingSkinTicks;
 };
diff --git a/app/src/main/cpp/src/mta/core/MtaHooks.cpp b/app/src/main/cpp/src/mta/core/MtaHooks.cpp
index 5ba86f3..e707513 100644
--- a/app/src/main/cpp/src/mta/core/MtaHooks.cpp
+++ b/app/src/main/cpp/src/mta/core/MtaHooks.cpp
@@ -6,12 +6,59 @@
 #include "vendor/log/log.h"
 #include "vendor/javawrapper/JavaWrapper.h"
 #include <jni.h>
+#include <atomic>
 #include <cstring>
 #include <cstdlib>
+#include <sys/syscall.h>
+#include <unistd.h>
+
+static inline pid_t CurrentTid() {
+    return static_cast<pid_t>(syscall(SYS_gettid));
+}
+
+// [MTA-HOOK] One-shot first-fire logs so a single run of logcat answers definitively
+// which per-frame hook(s) actually execute on this device build.
+static void LogFirstFire(const char* hookName, std::atomic<int>& flag) {
+    int expected = 0;
+    if (flag.compare_exchange_strong(expected, 1)) {
+        Log("[MTA-HOOK] %s fired for the first time (tid=%d)", hookName, CurrentTid());
+    }
+}
+
+// [MTA-VEH] Game-thread drain latch.
+//
+// Binary evidence (libGTASA.so ARM64): this port runs the game and the render loop in
+// the same thread chain ΓÇö CGame::Process (via PLT 0x21e1d0) and RenderScene are both
+// called from inside Idle() per frame, and AppEventHandler(rsIDLE) invokes Idle from
+// the RenderWare core thread. Entity creation (CreateCarForScript) and streaming calls
+// are therefore only safe from that thread chain. The latch pins the drain to the
+// first per-frame hook thread it sees and refuses foreign threads.
+static std::atomic<pid_t> s_gameTid{0};
+static std::atomic<int>   s_gameTidWarned{0};
+
+static void DrainGameThreadQueue(const char* hookName) {
+    pid_t tid = CurrentTid();
+    pid_t expected = 0;
+    if (s_gameTid.compare_exchange_strong(expected, tid)) {
+        Log("[MTA-VEH] game-thread drain claimed by %s (tid=%d)", hookName, tid);
+    } else if (s_gameTid.load() != tid) {
+        if (s_gameTidWarned.fetch_add(1) == 0) {
+            Log("[MTA-VEH] ER: drain skipped in %s: owned by tid=%d, caller tid=%d", hookName, s_gameTid.load(), tid);
+        }
+        return;
+    }
+    // Drains the [MTA-VEH] queue once per frame (only on the latched game thread).
+    MtaClient::Get().ProcessPendingVehicleSpawn();
+}
+
+static std::atomic<int> s_fired_CGame_Process{0};
+static std::atomic<int> s_fired_NVEventEGLSwapBuffers{0};
+static std::atomic<int> s_fired_Idle{0};
+static std::atomic<int> s_fired_FrontendIdle{0};
 
 static void (*orig_CGame_Initialise)(const char*) = nullptr;
 static void hook_CGame_Initialise(const char* dataPath) {
-    Log("[MTA-HOOK] CGame::Initialise called with path: %s", dataPath ? dataPath : "null");
+    Log("[MTA-HOOK] CGame::Initialise called with path: %s (tid=%d)", dataPath ? dataPath : "null", CurrentTid());
     if (orig_CGame_Initialise) {
         orig_CGame_Initialise(dataPath);
     }
@@ -37,8 +84,12 @@ static void hook_CGame_Process() {
     if (orig_CGame_Process) {
         orig_CGame_Process();
     }
-    // MTA Client tick update during active gameplay
+    // MTA Client tick update during active gameplay (kept: network/INGAME-spawn logic)
     MtaClient::Get().Update();
+    // [MTA-VEH] Per-frame drain ΓÇö CGame::Process runs inside Idle() on the game thread
+    // (verified: its only caller is Idle+0x1c20 via PLT 0x21e1d0).
+    LogFirstFire("CGame::Process", s_fired_CGame_Process);
+    DrainGameThreadQueue("CGame::Process");
 }
 
 static void (*orig_CGame_ShutDown)() = nullptr;
@@ -57,6 +108,10 @@ static void (*orig_NVEventEGLSwapBuffers)() = nullptr;
 static void hook_NVEventEGLSwapBuffers() {
     // MTA Client tick update on every single render frame (menu, loading, ingame)
     MtaClient::Get().Update();
+    // [MTA-HOOK] diagnostics only: this hook must NOT drain the vehicle queue because
+    // its thread is not proven to be the game thread (the latch in DrainGameThreadQueue
+    // exists precisely to keep entity creation off foreign threads).
+    LogFirstFire("NVEventEGLSwapBuffers", s_fired_NVEventEGLSwapBuffers);
 
     if (orig_NVEventEGLSwapBuffers) {
         orig_NVEventEGLSwapBuffers();
@@ -69,8 +124,13 @@ static void hook_Idle(void* pData, bool bParam) {
     if (orig_Idle) {
         orig_Idle(pData, bParam);
     }
-    // MTA Client tick update during active 3D gameplay
+    // MTA Client tick update during active 3D gameplay (kept: network/INGAME-spawn logic)
     MtaClient::Get().Update();
+    // [MTA-VEH] Primary per-frame drain: Idle() is the game-loop function of this port
+    // (its body calls CGame::Process via PLT and RenderScene; AppEventHandler rsIDLE=26
+    // and Pump_SwapBuffers call it every frame on the RenderWare/NV thread).
+    LogFirstFire("Idle", s_fired_Idle);
+    DrainGameThreadQueue("Idle");
 }
 
 // Hook FrontendIdle - menu per-frame heartbeat on GTA SA Android
@@ -81,6 +141,10 @@ static void hook_FrontendIdle() {
     }
     // MTA Client tick update during frontend menu
     MtaClient::Get().Update();
+    // [MTA-HOOK] diagnostics only (see hook_NVEventEGLSwapBuffers): menu-thread drain
+    // of the vehicle queue is not required for the /v flow (vehicle spawns happen in
+    // 3D gameplay, driven by hook_Idle / hook_CGame_Process).
+    LogFirstFire("FrontendIdle", s_fired_FrontendIdle);
 }
 
 // Hook CText::Get so "FEP_MTA" displays "MULTI THEFT AUTO"
diff --git a/app/src/main/cpp/src/mta/game/MtaGameInterface.cpp b/app/src/main/cpp/src/mta/game/MtaGameInterface.cpp
index 241edca..806e90a 100644
--- a/app/src/main/cpp/src/mta/game/MtaGameInterface.cpp
+++ b/app/src/main/cpp/src/mta/game/MtaGameInterface.cpp
@@ -321,6 +321,40 @@ void MtaGameInterface::RequestModel(int modelId, int flags) {
     }
 }
 
+int MtaGameInterface::GetModelLoadState(int modelId) {
+    if (modelId < 0) return -1;
+    static uint8_t* s_pStreamingInfo = nullptr;
+    if (!s_pStreamingInfo) {
+        void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_NOW);
+        if (!handle) handle = RTLD_DEFAULT;
+        s_pStreamingInfo = reinterpret_cast<uint8_t*>(dlsym(handle, "_ZN10CStreaming16ms_aInfoForModelE"));
+    }
+    if (!s_pStreamingInfo) return -1;
+    // ms_aInfoForModel: stride 20 bytes, m_nLoadState at +0x10, LOADSTATE_LOADED == 1
+    // (verified against CStreaming::RequestModel disassembly, libGTASA.so ARM64).
+    return s_pStreamingInfo[modelId * 20 + 0x10];
+}
+
+void MtaGameInterface::FlushModelRequests() {
+    static void (*s_pfnLoadAllRequestedModels)(bool) = nullptr;
+    static bool s_resolved = false;
+    if (!s_resolved) {
+        s_resolved = true;
+        void* handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_NOW);
+        if (!handle) handle = RTLD_DEFAULT;
+        // CStreaming::LoadAllRequestedModels(bool) @0x396B28 (ARM64):
+        // void, bool in w0; loops FlushChannels/ProcessLoadingChannel/RequestModelStream;
+        // clears its re-entrancy flag at 0x396CF4, safe to call every frame.
+        s_pfnLoadAllRequestedModels = reinterpret_cast<void(*)(bool)>(dlsym(handle, "_ZN10CStreaming22LoadAllRequestedModelsEb"));
+        if (!s_pfnLoadAllRequestedModels) {
+            Log("[MTA-VEH] ER: dlsym(_ZN10CStreaming22LoadAllRequestedModelsEb) failed");
+        }
+    }
+    if (s_pfnLoadAllRequestedModels) {
+        s_pfnLoadAllRequestedModels(false);
+    }
+}
+
 void* MtaGameInterface::SpawnVehicle(int modelId, const CVector& pos, float heading) {
     if (!m_initialized) Initialize();
 
diff --git a/app/src/main/cpp/src/mta/game/MtaGameInterface.h b/app/src/main/cpp/src/mta/game/MtaGameInterface.h
index 42e8e2c..abdf845 100644
--- a/app/src/main/cpp/src/mta/game/MtaGameInterface.h
+++ b/app/src/main/cpp/src/mta/game/MtaGameInterface.h
@@ -36,6 +36,8 @@ public:
     // Streaming & Model management
     bool HasModelLoaded(int modelId);
     void RequestModel(int modelId, int flags = 2);
+    int  GetModelLoadState(int modelId);   // raw CStreaming m_nLoadState byte (-1 if unresolved)
+    void FlushModelRequests();             // CStreaming::LoadAllRequestedModels(false)
 
     // Remote Players 3D Peds & Entities
     void* CreateRemotePed(unsigned int modelIndex, const CVector& pos);
\\\

## 3) Cómo probar

1. Build instalado en el dispositivo: `.\gradlew.bat :app:assembleDebug` (JAVA_HOME=JDK 17 — el Java 25 default rompe el plugin de Kotlin).
2. Entra al juego (mundo 3D cargado), abre el chat y ejecuta:
   - `/v 522` → motocicleta NRG-500 (2 ruedas)
   - `/v 444` → monster truck (4 ruedas)
3. Recomendado: hazlo **moviéndote/rotando** (el spawn sale 5m adelante del heading del jugador).
4. Repite ambos con el jugador andando y dentro del agua/colinas para probar el re-clamp de z de \CreateCarForScript\.

### Logs esperados (logcat, tag del mod)
\\\
[MTA-CLIENT] Vehicle spawn queued: Model 522
[MTA-VEH] request queued (model=522, queueSize=1)
[MTA-HOOK] <algo de: Idle | CGame::Process | NVEventEGLSwapBuffers | FrontendIdle> fired for the first time (tid=N)
[MTA-VEH] game-thread drain claimed by Idle (tid=N)        <- dueño del drenaje (hilo del juego)
[MTA-VEH] dequeued model 522 (queueSize=0)
[MTA-VEH] phase=load model=522 frame=0 loadState=0 (1=LOADSTATE_LOADED)   (cada 60 frames)
[MTA-VEH] model 522 loaded after N frame(s) (loadState=1)
[MTA-VEH] spawned model 522 ptr=0x... pos=(x, y, z) heading=h ... script-owned flag CVehicle+0x5E9=1
[UI] appendChatMessage: [MTA] Vehiculo 522 creado.
\\\

### Qué observar
- **522 (NRG-500)** con dos ruedas y **444 (Monster)** con 4: valida que el constructor por tipo (CBike vs CAutomobile) del \GetNewVehicleDependingOnCarModel\ internamente funciona con el flujo script.
- Si un modelo tarda >300 frames (~5s): aparece \[MTA-CLIENT] Timeout ...\ y el chat lo avisa; **no** descarta el resto de la cola.
- Si NO aparece **ninguna** línea \[MTA-HOOK] ... fired for the first time\, la instalación de hooks falló (verías \Failed to hook symbol ... errno\ con tag "reGTA"). Ese \"falló\" del shadowhook es el siguiente paso a diagnosticar.
- `[MTA-VEH] ER: drain skipped...` significa que el drenaje está anclado a otro tid (dueño del hilo de juego); reporta cuál tid ganó.

## 4) Cambios (5 ficheros, todo bajo app/src/main/cpp/src/mta/)
- \core/MtaClient.h/.cpp\: productor/cola (mutex+deque), consumidor por fases (streaming; luego spawn) con logs \\[MTA-VEH]\\ en cada paso/early-return; \\Update()\\ ya no drena vehículos (solo skins).
- \game/MtaGameInterface.h/.cpp\: \GetModelLoadState()\ (byte raw de CStreaming) y \FlushModelRequests()\ = \CStreaming::LoadAllRequestedModels(false)\ (dlsym + comentario binario).
- \core/MtaHooks.cpp\: drenaje por frame desde \hook_Idle\/\hook_CGame_Process\ con candado de tid (\s_gameTid\ atomico) y logs one-shot \[MTA-HOOK] ... first fire (tid=...)\ para los 4 hooks per-frame.
