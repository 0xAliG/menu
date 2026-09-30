#pragma once

// ============================================================================
// Top-right status panel — "paradox • <fps>fps • <client> • active". Styled to
// match the rest of the menu chrome: low rounding, 1 px border, accent stripe
// underneath (not on top). Call Render() once per frame after the menu draw;
// the panel paints itself as a borderless ImGui window so it can sit above
// everything without intercepting input.
//
// The client name string can be overridden per build — apex passes "apex",
// rust passes "rust", the standalone demo passes "new menu".
// ============================================================================

namespace Watermark {

// Render one frame of the watermark panel. `clientName` is the third body
// segment (after identity + fps, before status); pass nullptr to skip it.
// `statusText` is the trailing segment ("active" / "inactive" / "loading" /
// whatever the caller wants — usually driven by attach state).
void Render(const char* clientName, const char* statusText);

} // namespace Watermark
