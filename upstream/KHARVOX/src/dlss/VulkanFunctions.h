#pragma once
#include <vulkan/vulkan.h>
#include <stdexcept>
#include <string>

namespace kharvox::dlss {
#define KH_DLSS_VK_FUNCTIONS(X) \
 X(vkCreateImage) X(vkDestroyImage) X(vkCreateImageView) X(vkDestroyImageView) \
 X(vkGetImageMemoryRequirements) X(vkAllocateMemory) X(vkFreeMemory) X(vkBindImageMemory) \
 X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkAllocateCommandBuffers) X(vkResetCommandPool) \
 X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkCmdPipelineBarrier) \
 X(vkCreateFence) X(vkDestroyFence) X(vkResetFences) X(vkGetFenceStatus) X(vkWaitForFences) X(vkQueueSubmit) X(vkDeviceWaitIdle)
struct Functions {
#define MEMBER(name) PFN_##name name{};
    KH_DLSS_VK_FUNCTIONS(MEMBER)
#undef MEMBER
    void Load(VkDevice d,PFN_vkGetDeviceProcAddr gdpa){
#define LOAD(name) name=reinterpret_cast<PFN_##name>(gdpa(d,#name));if(!name)throw std::runtime_error(#name " unavailable");
        KH_DLSS_VK_FUNCTIONS(LOAD)
#undef LOAD
    }
};
#undef KH_DLSS_VK_FUNCTIONS
inline void vkCheck(VkResult result,const char* action){if(result!=VK_SUCCESS)throw std::runtime_error(std::string(action)+": "+std::to_string(result));}
}
