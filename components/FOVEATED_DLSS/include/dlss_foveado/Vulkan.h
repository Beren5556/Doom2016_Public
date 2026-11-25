#pragma once
#include "Foveation.h"
#include <vulkan/vulkan.h>
#include <memory>
#include <vector>

namespace dlss_foveado::vulkan {
// Experimental backend extension 1. Base API4/schema3/policies are unchanged.
// A view represents one mip/layer on the borrowed device, with a 2D view.
struct Image {
    VkDevice device{};VkImage image{};VkImageView view{};VkFormat format{};
    Extent extent{};VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    VkImageLayout layout{VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkImageUsageFlags usage{};
    VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT};
};
struct Context {
    VkDevice device{};PFN_vkGetDeviceProcAddr resolve{};VkPhysicalDeviceMemoryProperties memory{};
    // Must describe the feature actually enabled at vkCreateDevice, not merely
    // supported by the GPU. Required for the RG32F storage output shader.
    bool storageImageExtendedFormatsEnabled{};
};
// Retain each recorded operation and its input/output images until the command
// buffer completes. After successful submission call Submitted with its fence;
// keep that fence alive and unreset until this object is destroyed. If recording
// is abandoned, destroy the unsubmitted operation before recycling the command.
class Recorded {
public:
    struct State;
    explicit Recorded(std::unique_ptr<State>);
    ~Recorded();
    Recorded(const Recorded&)=delete;Recorded& operator=(const Recorded&)=delete;
    void Submitted(VkFence completion);
    // Alternative for hosts that already waited a fence/device-idle boundary
    // covering this operation AND its final reader. No wait/submit is issued.
    // The caller must not use this for work still recorded but never submitted.
    void RetiredAfterHostCompletion();
    bool Complete() const;
    bool DirectCopy() const;
private:std::unique_ptr<State> state;
};
class Operations {
public:
    struct State;
    explicit Operations(Context);
    ~Operations();
    // Outside a render pass, on a graphics/compute-capable queue. Inputs must be
    // available on that queue. Restores input layouts, leaves destination GENERAL.
    // Recording binds compute state; the host must rebind its own compute state.
    std::unique_ptr<Recorded> Resample(VkCommandBuffer,const Image&,Rect,const Image& destination,
        bool nearest=false,Point componentScale={1,1});
    std::unique_ptr<Recorded> Composite(VkCommandBuffer,const Image& peripheral,const Image& central,
        const RegionPlan&,const Image& destination,bool srgb=false,Point regionScale={1,1});
    std::unique_ptr<Recorded> NeuralResolve(VkCommandBuffer,const Image& original,const Image& smallInput,const Image& smallOutput,const Image& destination,float detail=1,float color=1,float sharpness=0);
    std::unique_ptr<Recorded> Sharpen(VkCommandBuffer,const Image&,const Image& destination,float strength);
    std::unique_ptr<Recorded> Indicator(VkCommandBuffer,const Image&,const RegionPlan&,const Image& destination,bool neural=false);
private:std::shared_ptr<State> state;
};
}
