#pragma once

#include <string>

namespace MtaJNI {
    void NotifyJavaChat(const std::string& sender, const std::string& text);
    void NotifyJavaState(int state, const std::string& statusMsg);
}
