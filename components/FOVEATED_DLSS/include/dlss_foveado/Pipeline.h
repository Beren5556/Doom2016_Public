#pragma once
#include "Reconstruction.h"
#include "Experimental.h"

namespace dlss_foveado::d3d11 {
struct NeuralRegionSpec { uint64_t viewId{}; Extent extent{}; bool before{}; bool operator==(const NeuralRegionSpec&) const = default; };
struct NeuralRegionInput {
    NeuralRegionSpec region{};
    uint64_t epoch{}, frame{};
    ID3D11Texture2D *color{}, *depth{}, *motion{}, *destination{};
    Point jitter{0,0}; float frameTimeMs{}; bool reset{};
};
struct NeuralRegionReceipt { uint64_t epoch{}, frame{}, viewId{}, ticket{}; Extent extent{}; };
class NeuralRegionBackend {
public:
    virtual ~NeuralRegionBackend()=default;
    // Called by host preparation outside Evaluate. Production adapters may be asynchronous.
    virtual bool Prepare(const std::array<NeuralRegionSpec,2>& regions)=0;
    virtual NeuralRegionReceipt Evaluate(ID3D11DeviceContext*,const NeuralRegionInput&)=0;
};
struct PipelineSettings {
    Config config{};
    uint32_t inheritedPreset{11}; Quality centralQuality{Quality::Balanced};
    bool neuralEnabled{}, neuralBefore{};
    float sharpness{}; // Optional full-output spatial pass, outside temporal histories.
    // Opt-in extension v1: native scene input; DLAA center, reduced input SR periphery.
    bool centralDlaa{};
    bool dynamic{};
    experimental::Cuts inner{3000,3000,3000,3000},outer{1000,1000,1000,1000};
    bool operator==(const PipelineSettings&) const = default;
};
struct EyeInput {
    ID3D11Texture2D *color{}, *depth{}, *motion{};
    // Source color may still be in a host atlas. Depth/MV must be eye-local.
    uint32_t colorX{}, colorY{};
    Point jitter{0,0}; float frameTimeMs{}; bool reset{};
};
struct EyeOutput {
    Image image;
    uint64_t epoch{}, frame{}, configRevision{}, ticket{};
    unsigned eye{};
    std::array<ReconstructionReceipt,2> reconstruction{};
    NeuralRegionReceipt neural{};
};
class FixedPipeline {
    struct State; std::unique_ptr<State> state;
public:
    FixedPipeline(ID3D11Device*,ReconstructionBackend&);
    ~FixedPipeline();
    FixedPipeline(const FixedPipeline&)=delete;
    FixedPipeline& operator=(const FixedPipeline&)=delete;
    static std::array<NeuralRegionSpec,2> NeuralRegions(const FramePlan&,bool before);
    void Begin(FramePlan,PipelineSettings,NeuralRegionBackend* neural=nullptr);
    EyeOutput Evaluate(ID3D11DeviceContext*,unsigned eye,const EyeInput&);
    bool CanPublish(const EyeOutput&) const noexcept;
    bool Complete() const noexcept;
    void Abandon() noexcept;
    const FramePlan& CurrentPlan() const;
    bool DynamicPrepared() const noexcept;
    void DrawIndicators(ID3D11DeviceContext*,ID3D11Texture2D* scene,const std::array<Rect,2>& outputViewports);
};
} // namespace dlss_foveado::d3d11
