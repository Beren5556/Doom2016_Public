#pragma once
#include "D3D11.h"
#include <filesystem>

namespace dlss_foveado::d3d11 {
enum class Branch : uint32_t { Central, Peripheral };
enum class Quality : uint32_t { Native, Quality, Balanced, Performance, UltraPerformance };
struct FeatureSpec {
    uint64_t viewId{};
    Branch branch{};
    Extent input{}, output{};
    uint32_t preset{};
    Quality quality{Quality::Balanced};
    bool encodedLdr{true}, depthInverted{true}, motionJittered{};
    bool operator==(const FeatureSpec&) const = default;
};
struct ReconstructionInput {
    FeatureSpec feature{};
    uint64_t epoch{}, frame{};
    ID3D11Texture2D* color{};
    ID3D11Texture2D* depth{};
    ID3D11Texture2D* motion{};
    ID3D11Texture2D* destination{};
    Point jitter{0,0};
    float frameTimeMs{}, preExposure{1}, exposureScale{1};
    bool reset{};
    // Experimental bounded central cache: 0..2; peripheral always uses 0.
    uint32_t preparedSlot{};
};
struct ReconstructionReceipt {
    uint64_t ticket{}, epoch{}, frame{}, viewId{};
    Branch branch{};
    Extent input{}, output{};
    uint32_t providerResult{};
};
struct BackendCapabilities {
    uint32_t contractVersion{ContractVersion};
    bool ldrSuperResolution{}, ldrNativeAA{}, hdrNativeAA{};
    uint32_t maximumContexts{}, minimumExtent{32}, maximumExtent{16384};
};
struct ReconstructionStatistics {uint64_t created{},evaluated{},released{};};
class ReconstructionBackend {
public:
    virtual ~ReconstructionBackend() = default;
    virtual BackendCapabilities Capabilities() const noexcept = 0;
    virtual ReconstructionReceipt Evaluate(ID3D11DeviceContext* context, const ReconstructionInput& input) = 0;
    // Nonblocking ordinary GPU retirement. Host keeps the backend alive until true.
    virtual bool Stop(ID3D11DeviceContext* context) = 0;
    virtual ReconstructionStatistics Statistics() const noexcept {return {};}
};
struct NgxOptions {
    std::filesystem::path runtimeDirectory, cacheDirectory;
    std::string projectId;
    uint32_t budgetReservePercent{}; // Legacy opt-in flag; actual budget/paging guard, zero disables it.
};
// The host must retire any other NGX session it owns before activating this
// backend. No SDK or game types are exposed by the public reconstruction API.
std::unique_ptr<ReconstructionBackend> CreateNgxBackend(ID3D11Device* device, NgxOptions options);
} // namespace dlss_foveado::d3d11
