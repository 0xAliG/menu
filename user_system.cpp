#include "user_system.h"

namespace UserSystem {

static UserInfo s_user;

const UserInfo& GetUser() { return s_user; }

void SetUser(const UserInfo& info) { s_user = info; }

void ClearUser() { s_user = UserInfo{}; }

void SetUsername(const char* name) {
    s_user.username = name;
    s_user.authenticated = true;
}

void SetSubscription(int daysLeft) {
    s_user.subscriptionDaysLeft = daysLeft;
}

void SetAvatar(ImTextureID texture, float w, float h) {
    s_user.avatarTexture = texture;
    s_user.avatarW = w;
    s_user.avatarH = h;
}

} // namespace UserSystem
