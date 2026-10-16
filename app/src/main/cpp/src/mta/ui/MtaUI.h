#pragma once

#include <string>
#include <vector>

struct MtaChatMessage {
    std::string sender;
    std::string text;
    uint32_t color;
};

class MtaUI {
public:
    static MtaUI& Get();

    void Initialize();
    void Render();
    void AddChatMessage(const std::string& sender, const std::string& text, uint32_t color = 0xFFFFFFFF);

    const std::vector<MtaChatMessage>& GetChatHistory() const { return m_chatHistory; }

private:
    MtaUI();

    std::vector<MtaChatMessage> m_chatHistory;
};
