#pragma once
#include <cstdint>
#include <cstddef>
namespace external_overlay {
constexpr uint32_t ResolutionOffset=3968;
struct ResolutionView {uint32_t width{},height{},recommendedWidth{},recommendedHeight{},maxWidth{},maxHeight{};};
// Independent telemetry ABI v1 in the final reserved bytes of Surface v1.
// Flags: 1 session, 2 projection observed, 4 ambiguous layers, 8 truncated views.
struct XrResolution {
 uint32_t version=1,viewCount{};uint64_t tick{};
 uint32_t viewType{},flags{},systemMaxWidth{},systemMaxHeight{};
 ResolutionView views[4]{};
};
static_assert(sizeof(XrResolution)==128);
static_assert(offsetof(XrResolution,views)==32);
static_assert(ResolutionOffset+sizeof(XrResolution)==4096);
}
