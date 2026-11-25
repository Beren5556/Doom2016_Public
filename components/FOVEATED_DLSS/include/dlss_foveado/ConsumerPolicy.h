#pragma once
#include "Experimental.h"

namespace dlss_foveado::consumer_policy {
// Opt-in DLSS-only policy, versioned separately from generic engine capabilities.
// Map the host's renderer to this enum; these are NOT persisted host/UI mode IDs.
inline constexpr uint32_t Version = 1;
enum class Renderer { Unavailable, SuperResolution, Neural, NativeAA };
inline bool Allows(experimental::Mode mode, Renderer renderer) noexcept {
    using experimental::Mode;
    if (mode == Mode::Disabled) return true;
    if (mode != Mode::Fixed && mode != Mode::Dynamic) return false;
    return renderer == Renderer::SuperResolution ||
        (renderer == Renderer::Neural && mode == Mode::Fixed);
}
// Session, revisions, initialization and command validity remain host checks.
// Recovery must not depend on a currently available renderer/projection.
inline bool CanApply(experimental::Mode mode, Renderer renderer,
                     bool renderReady, bool projectionReady) noexcept {
    return Allows(mode, renderer) &&
        (mode == experimental::Mode::Disabled || (renderReady && projectionReady));
}
struct Selection {
    Config config;
    experimental::Settings settings;
};
// Use on a validated live selection, at startup and again at the render safe point.
// Passing by value preserves the host's separately stored per-mode presets.
inline Selection Effective(Selection selected, Renderer renderer) {
    selected.settings.centralDlaa = false;
    if (!Allows(selected.settings.mode, renderer))
        selected.settings.mode = experimental::Mode::Disabled;
    selected.config.enabled = selected.settings.mode != experimental::Mode::Disabled;
    return selected;
}
} // namespace dlss_foveado::consumer_policy
