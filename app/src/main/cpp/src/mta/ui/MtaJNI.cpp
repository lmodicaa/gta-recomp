#include "MtaJNI.h"
#include "mta/core/MtaClient.h"
#include "mta/game/MtaGameInterface.h"
#include "mta/multiplayer/MtaPlayerManager.h"
#include "mta/net/MtaNetwork.h"
#include "mta/server/MtaLocalServer.h"
#include "vendor/javawrapper/JavaWrapper.h"
#include "vendor/log/log.h"
#include <jni.h>

namespace MtaJNI {

void NotifyJavaChat(const std::string& sender, const std::string& text) {
    MTA_DEBUG("[JNI] NotifyJavaChat('%s', '%s')", sender.c_str(), text.c_str());

    if (!g_pJavaWrapper || !g_pJavaWrapper->activity) {
        MTA_DEBUG("[JNI] ERROR: g_pJavaWrapper o activity es null!");
        return;
    }

    JNIEnv* env = CJavaWrapper::GetEnv();
    if (!env) {
        MTA_DEBUG("[JNI] ERROR: GetEnv() devolvio null!");
        return;
    }

    jclass cls = env->GetObjectClass(g_pJavaWrapper->activity);
    if (!cls) {
        MTA_DEBUG("[JNI] ERROR: GetObjectClass devolvio null!");
        return;
    }

    jmethodID mid = env->GetMethodID(cls, "onMtaChatMessage", "(Ljava/lang/String;Ljava/lang/String;)V");
    if (mid) {
        jstring jSender = env->NewStringUTF(sender.c_str());
        jstring jText = env->NewStringUTF(text.c_str());
        env->CallVoidMethod(g_pJavaWrapper->activity, mid, jSender, jText);
        env->DeleteLocalRef(jSender);
        env->DeleteLocalRef(jText);
        MTA_DEBUG("[JNI] onMtaChatMessage invocado exitosamente");
    } else {
        MTA_DEBUG("[JNI] ERROR: No se encontro metodo onMtaChatMessage");
    }
    env->DeleteLocalRef(cls);
}

void NotifyJavaState(int state, const std::string& statusMsg) {
    MTA_DEBUG("[JNI] NotifyJavaState(state=%d, msg='%s')", state, statusMsg.c_str());

    if (!g_pJavaWrapper || !g_pJavaWrapper->activity) {
        MTA_DEBUG("[JNI] ERROR: g_pJavaWrapper o activity es null en NotifyJavaState!");
        return;
    }

    JNIEnv* env = CJavaWrapper::GetEnv();
    if (!env) {
        MTA_DEBUG("[JNI] ERROR: GetEnv() devolvio null en NotifyJavaState!");
        return;
    }

    jclass cls = env->GetObjectClass(g_pJavaWrapper->activity);
    if (!cls) {
        MTA_DEBUG("[JNI] ERROR: GetObjectClass devolvio null en NotifyJavaState!");
        return;
    }

    jmethodID mid = env->GetMethodID(cls, "onMtaStateChanged", "(ILjava/lang/String;)V");
    if (mid) {
        jstring jMsg = env->NewStringUTF(statusMsg.c_str());
        env->CallVoidMethod(g_pJavaWrapper->activity, mid, static_cast<jint>(state), jMsg);
        env->DeleteLocalRef(jMsg);
        MTA_DEBUG("[JNI] onMtaStateChanged invocado exitosamente");
    } else {
        MTA_DEBUG("[JNI] ERROR: No se encontro metodo onMtaStateChanged");
    }
    env->DeleteLocalRef(cls);
}

} // namespace MtaJNI

extern "C" {

JNIEXPORT void JNICALL
Java_com_gta_reversed_REGTA_nativeMtaConnect(JNIEnv *env, jobject thiz, jstring host, jint port, jstring nick) {
    const char* hostStr = env->GetStringUTFChars(host, nullptr);
    const char* nickStr = env->GetStringUTFChars(nick, nullptr);

    MTA_DEBUG("[JNI] nativeMtaConnect recibido desde Java: host=%s, port=%d, nick=%s", hostStr, port, nickStr);

    MtaClient::Get().ConnectToServer(hostStr, static_cast<uint16_t>(port));

    env->ReleaseStringUTFChars(host, hostStr);
    env->ReleaseStringUTFChars(nick, nickStr);
}

JNIEXPORT void JNICALL
Java_com_gta_reversed_REGTA_nativeMtaDisconnect(JNIEnv *env, jobject thiz) {
    MTA_DEBUG("[JNI] nativeMtaDisconnect recibido desde Java");
    MtaClient::Get().Disconnect();
}

JNIEXPORT void JNICALL
Java_com_gta_reversed_REGTA_nativeMtaSendChat(JNIEnv *env, jobject thiz, jstring text) {
    const char* textStr = env->GetStringUTFChars(text, nullptr);

    MTA_DEBUG("[JNI] nativeMtaSendChat recibido: %s", textStr);

    if (textStr && textStr[0] == '/') {
        MtaClient::Get().ProcessCommand(textStr);
    } else {
        NetBitStream bs;
        bs.WriteString(textStr);
        MtaNetwork::Get().SendPacket(PACKET_ID_CHAT_ECHO, bs);
    }

    env->ReleaseStringUTFChars(text, textStr);
}

JNIEXPORT jboolean JNICALL
Java_com_gta_reversed_REGTA_nativeMtaSpawnVehicle(JNIEnv *env, jobject thiz, jint modelId) {
    MtaClient::Get().RequestSpawnVehicle(modelId);
    return true;
}

JNIEXPORT jboolean JNICALL
Java_com_gta_reversed_REGTA_nativeMtaSetSkin(JNIEnv *env, jobject thiz, jint skinId) {
    MtaClient::Get().RequestSetSkin(skinId);
    return true;
}

JNIEXPORT jstring JNICALL
Java_com_gta_reversed_REGTA_nativeMtaGetPlayerList(JNIEnv *env, jobject thiz) {
    std::string listJson = MtaPlayerManager::Get().GetPlayerListJson();
    return env->NewStringUTF(listJson.c_str());
}

JNIEXPORT jint JNICALL
Java_com_gta_reversed_REGTA_nativeMtaGetState(JNIEnv *env, jobject thiz) {
    return static_cast<jint>(MtaNetwork::Get().GetState());
}

JNIEXPORT jboolean JNICALL
Java_com_gta_reversed_REGTA_nativeMtaStartLocalServer(JNIEnv *env, jobject thiz, jint port) {
    MTA_DEBUG("[JNI] nativeMtaStartLocalServer solicitado en puerto %d", port);
    return MtaLocalServer::Get().Start(static_cast<uint16_t>(port));
}

JNIEXPORT void JNICALL
Java_com_gta_reversed_REGTA_nativeMtaStopLocalServer(JNIEnv *env, jobject thiz) {
    MTA_DEBUG("[JNI] nativeMtaStopLocalServer solicitado");
    MtaLocalServer::Get().Stop();
}

JNIEXPORT jboolean JNICALL
Java_com_gta_reversed_REGTA_nativeMtaIsLocalServerRunning(JNIEnv *env, jobject thiz) {
    return MtaLocalServer::Get().IsRunning();
}

}
