#include "MtaHooks.h"
#include "MtaClient.h"
#include "mta/net/MtaNetwork.h"
#include "mta/game/MtaGameInterface.h"
#include "vendor/patch/patch.h"
#include "vendor/log/log.h"
#include "vendor/javawrapper/JavaWrapper.h"
#include <jni.h>
#include <cstring>
#include <cstdlib>

static void (*orig_CGame_Initialise)(const char*) = nullptr;
static void hook_CGame_Initialise(const char* dataPath) {
    Log("[MTA-HOOK] CGame::Initialise called with path: %s", dataPath ? dataPath : "null");
    if (orig_CGame_Initialise) {
        orig_CGame_Initialise(dataPath);
    }
    Log("[MTA-HOOK] CGame::Initialise completed successfully! 3D world ready.");
    MtaClient::Get().Initialize();

    // If already in MTA multiplayer, trigger spawn immediately now that world exists
    if (MtaNetwork::Get().GetState() == eMtaState::INGAME && !MtaClient::Get().IsPlayerSpawned()) {
        void* pPed = MtaGameInterface::Get().GetLocalPlayerPed();
        if (pPed) {
            MtaGameInterface::Get().SpawnLocalPlayer(CVector(1481.0f, -1771.0f, 18.7f));
            MtaClient::Get().SetPlayerSpawned(true);
            MtaGameInterface::Get().DismissMenu();
            Log("[MTA-HOOK] Local player spawned into MTA world upon load completion.");
        }
    }
}



static void (*orig_CGame_Process)() = nullptr;
static void hook_CGame_Process() {
    if (orig_CGame_Process) {
        orig_CGame_Process();
    }
    // MTA Client tick update during active gameplay
    MtaClient::Get().Update();
}

static void (*orig_CGame_ShutDown)() = nullptr;
static void hook_CGame_ShutDown() {
    Log("[MTA-HOOK] CGame::ShutDown called.");
    MtaClient::Get().Shutdown();
    if (orig_CGame_ShutDown) {
        orig_CGame_ShutDown();
    }
}



// Hook NVEventEGLSwapBuffers - Android per-frame render heartbeat
static void (*orig_NVEventEGLSwapBuffers)() = nullptr;
static void hook_NVEventEGLSwapBuffers() {
    // MTA Client tick update on every single render frame (menu, loading, ingame)
    MtaClient::Get().Update();

    if (orig_NVEventEGLSwapBuffers) {
        orig_NVEventEGLSwapBuffers();
    }
}

// Hook Idle - in-game per-frame heartbeat on GTA SA Android
static void (*orig_Idle)(void*, bool) = nullptr;
static void hook_Idle(void* pData, bool bParam) {
    if (orig_Idle) {
        orig_Idle(pData, bParam);
    }
    // MTA Client tick update during active 3D gameplay
    MtaClient::Get().Update();
}

// Hook FrontendIdle - menu per-frame heartbeat on GTA SA Android
static void (*orig_FrontendIdle)() = nullptr;
static void hook_FrontendIdle() {
    if (orig_FrontendIdle) {
        orig_FrontendIdle();
    }
    // MTA Client tick update during frontend menu
    MtaClient::Get().Update();
}

// Hook CText::Get so "FEP_MTA" displays "MULTI THEFT AUTO"
static unsigned short* (*orig_CText_Get)(void*, const char*) = nullptr;
static unsigned short s_mtaTitleGxt[] = {
    'M', 'U', 'L', 'T', 'I', ' ', 'T', 'H', 'E', 'F', 'T', ' ', 'A', 'U', 'T', 'O', 0
};

static unsigned short* hook_CText_Get(void* self, const char* key) {
    if (key) {
        if (std::strcmp(key, "FEP_MTA") == 0 || std::strcmp(key, "FEH_BRI") == 0) {
            return s_mtaTitleGxt;
        }
    }
    if (orig_CText_Get) {
        return orig_CText_Get(self, key);
    }
    return nullptr;
}

// Menu item structure for MainMenuScreen in GTA SA Android (ARM64: 24 bytes)
struct MenuItem {
    void* texture;
    const char* gxtKey;
    void (*callback)(void*);
};

// Callback when player taps "MULTI THEFT AUTO" in native GTA menu carousel
static void hook_MainMenuScreen_OnMta(void* self) {
    Log("[MTA-HOOK] Native GTA Menu: 'MULTI THEFT AUTO' clicked by player!");

    if (g_pJavaWrapper && g_pJavaWrapper->activity) {
        JNIEnv* env = CJavaWrapper::GetEnv();
        if (env) {
            jclass cls = env->GetObjectClass(g_pJavaWrapper->activity);
            if (cls) {
                jmethodID mid = env->GetMethodID(cls, "showConnectDialogFromNative", "()V");
                if (mid) {
                    env->CallVoidMethod(g_pJavaWrapper->activity, mid);
                    Log("[MTA-HOOK] showConnectDialogFromNative called successfully!");
                }
                env->DeleteLocalRef(cls);
            }
        }
    }
}

// Hook MainMenuScreen::AddAllItems to add native "MULTI THEFT AUTO" button
static void (*orig_MainMenuScreen_AddAllItems)(void*) = nullptr;
static void hook_MainMenuScreen_AddAllItems(void* self) {
    if (orig_MainMenuScreen_AddAllItems) {
        orig_MainMenuScreen_AddAllItems(self);
    }

    if (!self) return;

    uint8_t* pScreen = reinterpret_cast<uint8_t*>(self);
    int capacity = *reinterpret_cast<int*>(pScreen + 0x58);
    int count = *reinterpret_cast<int*>(pScreen + 0x5c);
    MenuItem* items = *reinterpret_cast<MenuItem**>(pScreen + 0x60);

    Log("[MTA-HOOK] hook_MainMenuScreen_AddAllItems: count=%d, capacity=%d, items=%p",
        count, capacity, items);

    if (items && count > 0) {
        // Reuse texture from existing items
        void* tex = nullptr;
        for (int i = 0; i < count; i++) {
            if (items[i].texture) {
                tex = items[i].texture;
                break;
            }
        }

        // Expand capacity if needed
        if (count >= capacity) {
            int newCap = capacity + 4;
            MenuItem* newItems = reinterpret_cast<MenuItem*>(malloc(newCap * sizeof(MenuItem)));
            if (newItems) {
                std::memcpy(newItems, items, count * sizeof(MenuItem));
                *reinterpret_cast<MenuItem**>(pScreen + 0x60) = newItems;
                *reinterpret_cast<int*>(pScreen + 0x58) = newCap;
                items = newItems;
                capacity = newCap;
            }
        }

        if (count < capacity) {
            items[count].texture = tex;
            items[count].gxtKey = "FEP_MTA";
            items[count].callback = hook_MainMenuScreen_OnMta;
            *reinterpret_cast<int*>(pScreen + 0x5c) = count + 1;
            Log("[MTA-HOOK] Appended native 'MULTI THEFT AUTO' button at index %d! Total: %d",
                count, count + 1);
        }
    }
}

void MtaHooks::Install() {
    Log("[MTA-HOOK] Installing MTA game loop hooks...");

    // Hook CGame::Initialise
    Patch::Redirect("_ZN5CGame10InitialiseEPKc", reinterpret_cast<void*>(hook_CGame_Initialise),
                    reinterpret_cast<void**>(&orig_CGame_Initialise));

    // Hook CGame::Process (per-tick gameplay heartbeat)
    Patch::Redirect("_ZN5CGame7ProcessEv", reinterpret_cast<void*>(hook_CGame_Process),
                    reinterpret_cast<void**>(&orig_CGame_Process));

    // Hook CGame::ShutDown
    Patch::Redirect("_ZN5CGame8ShutDownEv", reinterpret_cast<void*>(hook_CGame_ShutDown),
                    reinterpret_cast<void**>(&orig_CGame_ShutDown));

    // Hook NVEventEGLSwapBuffers (per-frame render loop on Android)
    Patch::Redirect("_Z21NVEventEGLSwapBuffersv", reinterpret_cast<void*>(hook_NVEventEGLSwapBuffers),
                    reinterpret_cast<void**>(&orig_NVEventEGLSwapBuffers));

    // Hook CText::Get so "FEP_MTA" displays "MULTI THEFT AUTO"
    Patch::Redirect("_ZN5CText3GetEPKc", reinterpret_cast<void*>(hook_CText_Get),
                    reinterpret_cast<void**>(&orig_CText_Get));

    // Hook Idle (per-frame 3D world heartbeat on Android)
    Patch::Redirect("_Z4IdlePvb", reinterpret_cast<void*>(hook_Idle),
                    reinterpret_cast<void**>(&orig_Idle));

    // Hook FrontendIdle (per-frame menu heartbeat on Android)
    Patch::Redirect("_Z12FrontendIdlev", reinterpret_cast<void*>(hook_FrontendIdle),
                    reinterpret_cast<void**>(&orig_FrontendIdle));

    // Hook MainMenuScreen::AddAllItems to insert native "MULTI THEFT AUTO" button
    Patch::Redirect("_ZN14MainMenuScreen11AddAllItemsEv", reinterpret_cast<void*>(hook_MainMenuScreen_AddAllItems),
                    reinterpret_cast<void**>(&orig_MainMenuScreen_AddAllItems));



    Log("[MTA-HOOK] MTA game loop and native menu hooks installed successfully.");
}
