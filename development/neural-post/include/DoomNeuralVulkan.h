#pragma once
#include <memory>
#include "NeuralPostFrame.h"
#include "OpenXRBootstrap.h"

namespace doomnp {
struct EyeImage { VkImage image{}; VkRect2D rect{}; VkExtent2D extent{}; };
// Caller owns acquired XR images and has retired their last GPU producer.
// Queue access must remain locked for the whole call. Never called after release.
class VulkanPost {
    struct Impl; std::unique_ptr<Impl> p;
public:
    explicit VulkanPost(unsigned diagnostic=0); ~VulkanPost();
    bool enabled();
    bool process(VkPhysicalDevice,VkDevice,VkQueue,uint32_t,const KharvoxVulkanDispatch&,
                 VkFormat,const EyeImage(&)[2],es2np::Frame);
    void suspend();
    bool reset(); // false means unresolved GPU ownership: retain until process exit
    bool gpuReady() const;
    const char* message() const;
};
}
