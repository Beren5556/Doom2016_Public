#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <cstring>
#include <vector>

namespace external_overlay {
// Borrow the session device/queue. The consumer must externally synchronize
// xrEndFrame against other host access to that queue, as required by Vulkan.
// One private staging allocation and command buffer; never rewrite while busy.
class VulkanUpload {
    HMODULE loader_{};
    VkDevice device_{};
    VkQueue queue_{};
    VkCommandPool pool_{};
    VkCommandBuffer command_{};
    VkBuffer staging_{};
    VkDeviceMemory memory_{};
    VkFence fence_{};
    void* mapped_{};
    bool busy_{};
    bool resetOnDestruction_{true};
    VkResult failure_{VK_SUCCESS};
    uint32_t width_{},height_{};
#define VKFN(name) PFN_##name name##_{ };
    VKFN(vkCreateCommandPool) VKFN(vkDestroyCommandPool)
    VKFN(vkAllocateCommandBuffers) VKFN(vkResetCommandPool)
    VKFN(vkBeginCommandBuffer) VKFN(vkEndCommandBuffer)
    VKFN(vkCreateBuffer) VKFN(vkDestroyBuffer) VKFN(vkGetBufferMemoryRequirements)
    VKFN(vkAllocateMemory) VKFN(vkFreeMemory) VKFN(vkBindBufferMemory)
    VKFN(vkMapMemory) VKFN(vkUnmapMemory)
    VKFN(vkCreateFence) VKFN(vkDestroyFence) VKFN(vkGetFenceStatus)
    VKFN(vkResetFences) VKFN(vkWaitForFences) VKFN(vkQueueSubmit)
    VKFN(vkDeviceWaitIdle)
    VKFN(vkCmdPipelineBarrier) VKFN(vkCmdCopyBufferToImage)
#undef VKFN
public:
    explicit VulkanUpload(bool resetOnDestruction=true):resetOnDestruction_(resetOnDestruction){}
    VulkanUpload(const VulkanUpload&)=delete;
    VulkanUpload& operator=(const VulkanUpload&)=delete;
    ~VulkanUpload(){if(resetOnDestruction_)Reset();}
    bool Active() const {return device_!=VK_NULL_HANDLE&&mapped_!=nullptr;}
    bool Busy() const {return busy_;}
    VkResult Poll() {
        if(failure_!=VK_SUCCESS)return failure_;
        if(!busy_)return VK_SUCCESS;
        const auto result=vkGetFenceStatus_(device_,fence_);
        if(result==VK_SUCCESS)busy_=false;else if(result!=VK_NOT_READY)failure_=result;
        return result;
    }
    void Reset() {
        if(failure_!=VK_SUCCESS&&device_&&vkDeviceWaitIdle_)vkDeviceWaitIdle_(device_);
        if(busy_&&vkWaitForFences_)vkWaitForFences_(device_,1,&fence_,VK_TRUE,UINT64_MAX);
        if(mapped_)vkUnmapMemory_(device_,memory_);
        if(fence_)vkDestroyFence_(device_,fence_,nullptr);
        if(staging_)vkDestroyBuffer_(device_,staging_,nullptr);
        if(memory_)vkFreeMemory_(device_,memory_,nullptr);
        if(pool_)vkDestroyCommandPool_(device_,pool_,nullptr);
        mapped_=nullptr;fence_=VK_NULL_HANDLE;staging_=VK_NULL_HANDLE;
        memory_=VK_NULL_HANDLE;pool_=VK_NULL_HANDLE;command_=VK_NULL_HANDLE;
        queue_=VK_NULL_HANDLE;device_=VK_NULL_HANDLE;busy_=false;
        failure_=VK_SUCCESS;
        if(loader_)FreeLibrary(loader_);loader_=nullptr;
    }
    bool Initialize(VkInstance instance,VkPhysicalDevice physical,VkDevice device,
                    uint32_t family,uint32_t queueIndex,uint32_t width,uint32_t height) {
        Reset();
        if(!instance||!physical||!device||!width||!height)return false;
        loader_=LoadLibraryExW(L"vulkan-1.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!loader_)return false;
        const auto gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader_,"vkGetInstanceProcAddr"));
        if(!gipa){Reset();return false;}
        const auto gdpa=reinterpret_cast<PFN_vkGetDeviceProcAddr>(gipa(instance,"vkGetDeviceProcAddr"));
        const auto memoryProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(gipa(instance,"vkGetPhysicalDeviceMemoryProperties"));
        const auto families=reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(gipa(instance,"vkGetPhysicalDeviceQueueFamilyProperties"));
        if(!gdpa||!memoryProperties||!families){Reset();return false;}
#define LOADVK(name) name##_=reinterpret_cast<PFN_##name>(gdpa(device,#name));if(!name##_){Reset();return false;}
        LOADVK(vkCreateCommandPool) LOADVK(vkDestroyCommandPool)
        LOADVK(vkAllocateCommandBuffers) LOADVK(vkResetCommandPool)
        LOADVK(vkBeginCommandBuffer) LOADVK(vkEndCommandBuffer)
        LOADVK(vkCreateBuffer) LOADVK(vkDestroyBuffer) LOADVK(vkGetBufferMemoryRequirements)
        LOADVK(vkAllocateMemory) LOADVK(vkFreeMemory) LOADVK(vkBindBufferMemory)
        LOADVK(vkMapMemory) LOADVK(vkUnmapMemory)
        LOADVK(vkCreateFence) LOADVK(vkDestroyFence) LOADVK(vkGetFenceStatus)
        LOADVK(vkResetFences) LOADVK(vkWaitForFences) LOADVK(vkQueueSubmit)
        LOADVK(vkDeviceWaitIdle)
        LOADVK(vkCmdPipelineBarrier) LOADVK(vkCmdCopyBufferToImage)
#undef LOADVK
        uint32_t count{};families(physical,&count,nullptr);
        std::vector<VkQueueFamilyProperties> props(count);families(physical,&count,props.data());
        if(family>=count||queueIndex>=props[family].queueCount||
           !(props[family].queueFlags&VK_QUEUE_GRAPHICS_BIT)){Reset();return false;}
        const auto getQueue=reinterpret_cast<PFN_vkGetDeviceQueue>(gdpa(device,"vkGetDeviceQueue"));
        if(!getQueue){Reset();return false;}
        device_=device;width_=width;height_=height;getQueue(device,family,queueIndex,&queue_);
        if(!queue_){Reset();return false;}
        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pool.queueFamilyIndex=family;
        VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};buffer.size=VkDeviceSize(width)*height*4;buffer.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        if(vkCreateCommandPool_(device,&pool,nullptr,&pool_)!=VK_SUCCESS||
           vkCreateBuffer_(device,&buffer,nullptr,&staging_)!=VK_SUCCESS){Reset();return false;}
        VkCommandBufferAllocateInfo cb{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};cb.commandPool=pool_;cb.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;cb.commandBufferCount=1;
        if(vkAllocateCommandBuffers_(device,&cb,&command_)!=VK_SUCCESS){Reset();return false;}
        VkMemoryRequirements req{};vkGetBufferMemoryRequirements_(device,staging_,&req);
        VkPhysicalDeviceMemoryProperties mem{};memoryProperties(physical,&mem);uint32_t type=UINT32_MAX;
        for(uint32_t n=0;n<mem.memoryTypeCount;++n)if((req.memoryTypeBits&(1u<<n))&&
            (mem.memoryTypes[n].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==
            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){type=n;break;}
        if(type==UINT32_MAX){Reset();return false;}
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};alloc.allocationSize=req.size;alloc.memoryTypeIndex=type;
        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        if(vkAllocateMemory_(device,&alloc,nullptr,&memory_)!=VK_SUCCESS||
           vkBindBufferMemory_(device,staging_,memory_,0)!=VK_SUCCESS||
           vkMapMemory_(device,memory_,0,VK_WHOLE_SIZE,0,&mapped_)!=VK_SUCCESS||
           vkCreateFence_(device,&fence,nullptr,&fence_)!=VK_SUCCESS){Reset();return false;}
        return true;
    }
    bool Submit(VkImage image,const void* bgra,size_t bytes) {
        if(!Active()||!image||!bgra||bytes!=size_t(width_)*height_*4||Poll()!=VK_SUCCESS)return false;
        if(vkResetCommandPool_(device_,pool_,0)!=VK_SUCCESS||vkResetFences_(device_,1,&fence_)!=VK_SUCCESS)return false;
        memcpy(mapped_,bgra,bytes);
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if(vkBeginCommandBuffer_(command_,&begin)!=VK_SUCCESS)return false;
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.image=image;
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier_(command_,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={width_,height_,1};
        vkCmdCopyBufferToImage_(command_,staging_,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier_(command_,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        if(vkEndCommandBuffer_(command_)!=VK_SUCCESS)return false;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command_;
        failure_=vkQueueSubmit_(queue_,1,&submit,fence_);if(failure_!=VK_SUCCESS)return false;
        busy_=true;return true;
    }
};
}
