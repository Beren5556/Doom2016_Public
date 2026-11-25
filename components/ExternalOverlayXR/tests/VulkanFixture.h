#pragma once
#include <windows.h>
#include <vulkan/vulkan.h>
#include <vector>
#include <stdexcept>
#include <iostream>
#include <string>

// Real Vulkan device behind a synthetic XR runtime. Never loads an XR runtime.
struct VulkanFixture {
    HMODULE loader{};VkInstance instance{};VkPhysicalDevice physical{};VkDevice device{};
    VkQueue queue{};uint32_t family{};VkImage image{};VkDeviceMemory imageMemory{};
    VkPhysicalDeviceMemoryProperties memory{};
#define MEMBER(name) PFN_##name name{};
    MEMBER(vkDestroyInstance) MEMBER(vkDestroyDevice) MEMBER(vkQueueWaitIdle)
    MEMBER(vkCreateImage) MEMBER(vkDestroyImage) MEMBER(vkGetImageMemoryRequirements)
    MEMBER(vkAllocateMemory) MEMBER(vkFreeMemory) MEMBER(vkBindImageMemory)
    MEMBER(vkCreateBuffer) MEMBER(vkDestroyBuffer) MEMBER(vkGetBufferMemoryRequirements)
    MEMBER(vkBindBufferMemory) MEMBER(vkMapMemory) MEMBER(vkUnmapMemory)
    MEMBER(vkCreateCommandPool) MEMBER(vkDestroyCommandPool) MEMBER(vkAllocateCommandBuffers)
    MEMBER(vkBeginCommandBuffer) MEMBER(vkEndCommandBuffer) MEMBER(vkCmdPipelineBarrier)
    MEMBER(vkCmdCopyImageToBuffer) MEMBER(vkQueueSubmit)
#undef MEMBER
    static void Check(VkResult r){if(r!=VK_SUCCESS)throw std::runtime_error("Vulkan fixture operation failed: "+std::to_string(r));}
    uint32_t MemoryType(uint32_t bits,VkMemoryPropertyFlags flags){
        for(uint32_t n=0;n<memory.memoryTypeCount;++n)if((bits&(1u<<n))&&(memory.memoryTypes[n].propertyFlags&flags)==flags)return n;
        throw std::runtime_error("Vulkan fixture memory unavailable");
    }
    void Initialize(){
        loader=LoadLibraryExW(L"vulkan-1.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!loader)throw std::runtime_error("Vulkan loader missing");
        auto gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader,"vkGetInstanceProcAddr"));
        auto create=reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr,"vkCreateInstance"));
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_1;
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};info.pApplicationInfo=&app;
        Check(create(&info,nullptr,&instance));
#define INSTANCE(name) auto name=reinterpret_cast<PFN_##name>(gipa(instance,#name));if(!name)throw std::runtime_error(#name)
        INSTANCE(vkEnumeratePhysicalDevices);INSTANCE(vkGetPhysicalDeviceQueueFamilyProperties);
        INSTANCE(vkGetPhysicalDeviceMemoryProperties);INSTANCE(vkGetPhysicalDeviceProperties);
        INSTANCE(vkGetDeviceProcAddr);INSTANCE(vkCreateDevice);
        vkDestroyInstance=reinterpret_cast<PFN_vkDestroyInstance>(gipa(instance,"vkDestroyInstance"));
        uint32_t count{};Check(vkEnumeratePhysicalDevices(instance,&count,nullptr));
        std::vector<VkPhysicalDevice> candidates(count);Check(vkEnumeratePhysicalDevices(instance,&count,candidates.data()));
        for(auto p:candidates){uint32_t n{};vkGetPhysicalDeviceQueueFamilyProperties(p,&n,nullptr);
            std::vector<VkQueueFamilyProperties> queues(n);vkGetPhysicalDeviceQueueFamilyProperties(p,&n,queues.data());
            for(uint32_t q=0;q<n;++q)if(queues[q].queueFlags&VK_QUEUE_GRAPHICS_BIT){physical=p;family=q;break;}
            if(physical)break;}
        if(!physical)throw std::runtime_error("No Vulkan graphics device");
        VkPhysicalDeviceProperties props{};vkGetPhysicalDeviceProperties(physical,&props);std::cout<<"Vulkan GPU: "<<props.deviceName<<'\n';
        vkGetPhysicalDeviceMemoryProperties(physical,&memory);
        float priority=1;VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};q.queueFamilyIndex=family;q.queueCount=1;q.pQueuePriorities=&priority;
        VkDeviceCreateInfo d{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};d.queueCreateInfoCount=1;d.pQueueCreateInfos=&q;
        Check(vkCreateDevice(physical,&d,nullptr,&device));
#define DEVICE(name) name=reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device,#name));if(!name)throw std::runtime_error(#name)
        DEVICE(vkDestroyDevice);DEVICE(vkQueueWaitIdle);DEVICE(vkCreateImage);DEVICE(vkDestroyImage);
        DEVICE(vkGetImageMemoryRequirements);DEVICE(vkAllocateMemory);DEVICE(vkFreeMemory);DEVICE(vkBindImageMemory);
        DEVICE(vkCreateBuffer);DEVICE(vkDestroyBuffer);DEVICE(vkGetBufferMemoryRequirements);DEVICE(vkBindBufferMemory);
        DEVICE(vkMapMemory);DEVICE(vkUnmapMemory);DEVICE(vkCreateCommandPool);DEVICE(vkDestroyCommandPool);
        DEVICE(vkAllocateCommandBuffers);DEVICE(vkBeginCommandBuffer);DEVICE(vkEndCommandBuffer);
        DEVICE(vkCmdPipelineBarrier);DEVICE(vkCmdCopyImageToBuffer);DEVICE(vkQueueSubmit);
        auto getQueue=reinterpret_cast<PFN_vkGetDeviceQueue>(vkGetDeviceProcAddr(device,"vkGetDeviceQueue"));getQueue(device,family,0,&queue);
#undef DEVICE
#undef INSTANCE
    }
    void DestroyImage(){if(image){Check(vkQueueWaitIdle(queue));vkDestroyImage(device,image,nullptr);vkFreeMemory(device,imageMemory,nullptr);image=VK_NULL_HANDLE;imageMemory=VK_NULL_HANDLE;}}
    void MakeImage(uint32_t width,uint32_t height){
        DestroyImage();VkImageCreateInfo i{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};i.imageType=VK_IMAGE_TYPE_2D;i.format=VK_FORMAT_B8G8R8A8_SRGB;
        i.extent={width,height,1};i.mipLevels=i.arrayLayers=1;i.samples=VK_SAMPLE_COUNT_1_BIT;
        i.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        Check(vkCreateImage(device,&i,nullptr,&image));VkMemoryRequirements req{};vkGetImageMemoryRequirements(device,image,&req);
        VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};a.allocationSize=req.size;a.memoryTypeIndex=MemoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        Check(vkAllocateMemory(device,&a,nullptr,&imageMemory));Check(vkBindImageMemory(device,image,imageMemory,0));
    }
    std::vector<unsigned char> Readback(uint32_t width,uint32_t height){
        Check(vkQueueWaitIdle(queue));const VkDeviceSize bytes=VkDeviceSize(width)*height*4;
        VkBufferCreateInfo b{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};b.size=bytes;b.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VkBuffer buffer{};Check(vkCreateBuffer(device,&b,nullptr,&buffer));VkMemoryRequirements req{};vkGetBufferMemoryRequirements(device,buffer,&req);
        VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};a.allocationSize=req.size;a.memoryTypeIndex=MemoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VkDeviceMemory allocation{};Check(vkAllocateMemory(device,&a,nullptr,&allocation));Check(vkBindBufferMemory(device,buffer,allocation,0));
        VkCommandPoolCreateInfo p{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};p.queueFamilyIndex=family;VkCommandPool pool{};Check(vkCreateCommandPool(device,&p,nullptr,&pool));
        VkCommandBufferAllocateInfo c{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};c.commandPool=pool;c.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;c.commandBufferCount=1;
        VkCommandBuffer command{};Check(vkAllocateCommandBuffers(device,&c,&command));VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};Check(vkBeginCommandBuffer(command,&begin));
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.image=image;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={width,height,1};vkCmdCopyImageToBuffer(command,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);
        barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;barrier.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;barrier.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;host.buffer=buffer;host.size=VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
        Check(vkEndCommandBuffer(command));VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;
        Check(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE));Check(vkQueueWaitIdle(queue));void* data{};Check(vkMapMemory(device,allocation,0,bytes,0,&data));
        auto* pixels=static_cast<unsigned char*>(data);std::vector<unsigned char> result(pixels,pixels+bytes);vkUnmapMemory(device,allocation);
        vkDestroyCommandPool(device,pool,nullptr);vkDestroyBuffer(device,buffer,nullptr);vkFreeMemory(device,allocation,nullptr);return result;
    }
    ~VulkanFixture(){if(device){DestroyImage();vkDestroyDevice(device,nullptr);}if(instance)vkDestroyInstance(instance,nullptr);if(loader)FreeLibrary(loader);}
};
