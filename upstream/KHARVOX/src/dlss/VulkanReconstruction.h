#pragma once
#include "VulkanDlss.h"
#include <dlss_foveado/Vulkan.h>

namespace kharvox::dlss {
struct ReconstructionSettings {
    dlss_foveado::Config foveation;
    std::array<dlss_foveado::ViewGeometry,2> geometry;
    uint64_t revision{1};
    bool neural{},neuralBefore{};
    unsigned neuralModel{},neuralPercent{85};
    PairInput::Appearance appearance;
    float detail{1},color{1},neuralSharpness{},sharpen{};
    bool dynamic{};unsigned processingLevel{};
    std::array<unsigned,4> processingCuts{};
};
// Host-level Vulkan counterpart of the accepted B3 route. The portable module
// owns geometry/crops/composition; this adapter supplies real NGX/Neural work.
// All work records in the host buffer. One ticket covers all branches and their
// final consumers. Rebind host compute state after Record, including failures.
class VulkanReconstruction {
    struct State;std::unique_ptr<State> state;
public:
    VulkanReconstruction(Options, bool storageImageExtendedFormatsEnabled);
    ~VulkanReconstruction();
    Status Record(const PairInput&,const ReconstructionSettings&,VkCommandBuffer,Output&);
    // Same caller proof/ownership rules as VulkanDlss recorded work.
    bool RetireRecordedAfterCompletion(uint64_t);
    bool CancelRecorded(uint64_t);
    // Same retirement requirement; release images and histories for an inactive
    // host route without tearing down the shared NGX device session.
    bool ReleaseResources();
    const std::string& Error()const;
    uint64_t Evaluations()const;
    uint64_t NeuralEvaluations()const;
    uint64_t ResetEvaluations()const;
};
}
