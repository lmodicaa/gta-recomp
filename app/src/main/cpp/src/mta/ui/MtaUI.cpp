#include "MtaUI.h"
#include "MtaJNI.h"
#include "mta/net/MtaNetwork.h"
#include "vendor/log/log.h"

MtaUI& MtaUI::Get() {
    static MtaUI instance;
    return instance;
}

MtaUI::MtaUI() {
}

void MtaUI::Initialize() {
    Log("[MTA-UI] Initializing MtaUI...");

    // Register Chat packet handler
    MtaNetwork::Get().RegisterHandler(PACKET_ID_CHAT_ECHO, [this](eMtaPacketId, NetBitStream& bs) {
        std::string text;
        if (bs.ReadString(text)) {
            AddChatMessage("Server", text);
        }
    });

    Log("[MTA-UI] MtaUI initialized.");
}

void MtaUI::Render() {
    // Render loop overlay placeholder
}

void MtaUI::AddChatMessage(const std::string& sender, const std::string& text, uint32_t color) {
    m_chatHistory.push_back({sender, text, color});
    Log("[MTA-CHAT] %s: %s", sender.c_str(), text.c_str());

    // Send to Android Java/Kotlin UI
    MtaJNI::NotifyJavaChat(sender, text);
}
