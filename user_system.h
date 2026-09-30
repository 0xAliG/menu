#pragma once
#include "imgui.h"
#include <string>

namespace UserSystem {

struct UserInfo {
    bool authenticated = false;
    std::string username;
    int subscriptionDaysLeft = 0; // -1 = lifetime, 0 = expired
    ImTextureID avatarTexture = ImTextureID();
    float avatarW = 0, avatarH = 0;
};

const UserInfo& GetUser();
void SetUser(const UserInfo& info);
void ClearUser();
void SetUsername(const char* name);
void SetSubscription(int daysLeft);
void SetAvatar(ImTextureID texture, float w, float h);

} // namespace UserSystem
