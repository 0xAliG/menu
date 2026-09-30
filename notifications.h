#pragma once
#include <string>

namespace Notifications {

enum class Type {
    Info,
    Success,
    Warning,
    Error
};

// Push a new toast onto the queue. The notification fades in, holds for `duration`
// seconds, then fades out and is removed automatically.
void Push(Type type, const std::string& text, float duration = 3.5f);

// Render all live toasts as an overlay anchored to the main viewport's bottom-right.
// Call once per frame after the main menu render.
void Render();

// Drop all active notifications.
void Clear();

} // namespace Notifications
