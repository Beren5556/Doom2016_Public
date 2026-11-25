#pragma once
#include <windows.h>
#include <vulkan/vulkan.h>
#include <vector>
#include <iostream>
#include <stdexcept>
#include <string>
#include <cstring>
#include "../src/vulkan/DoomReconstructionFeatures.h"
#ifdef KHARVOX_DLSS_CHAIN_TEST
#include "../src/dlss/VulkanDlssRequirements.h"
#endif

// Small synchronous, real-GPU fixture. Waiting is deliberate in tests only.
struct VulkanTestDevice {
    static void Check(VkResult r){if(r!=VK_SUCCESS)throw std::runtime_error("Vulkan fixture failure: "+std::to_string(r));}
    HMODULE loader{};VkInstance instance{};VkPhysicalDevice physical{};VkDevice device{};VkQueue queue{};uint32_t family{};
    VkExtent2D extent{8,8};PFN_vkGetInstanceProcAddr gipa{};
    using ExtensionQuery=std::vector<const char*>(*)(VkInstance,VkPhysicalDevice);
    VkPhysicalDeviceMemoryProperties memory{};PFN_vkDestroyInstance destroyInstance{};PFN_vkDestroyDevice destroyDevice{};
    VkCommandPool pool{};VkCommandBuffer command{};VkFence done{};PFN_vkGetDeviceProcAddr resolve{};
#define FUNCTIONS(X) \
    X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) X(vkBindBufferMemory) \
    X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements) X(vkBindImageMemory) \
    X(vkAllocateMemory) X(vkFreeMemory) X(vkMapMemory) X(vkUnmapMemory) \
    X(vkCreateImageView) X(vkDestroyImageView) X(vkCreateSampler) X(vkDestroySampler) \
    X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkResetCommandPool) X(vkAllocateCommandBuffers) \
    X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkCmdPipelineBarrier) X(vkQueueSubmit) X(vkQueueWaitIdle) \
    X(vkCmdCopyBufferToImage) X(vkCmdCopyImageToBuffer) X(vkCreateRenderPass) X(vkDestroyRenderPass) \
    X(vkCreateFramebuffer) X(vkDestroyFramebuffer) X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) \
    X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) \
    X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) X(vkCreateShaderModule) X(vkDestroyShaderModule) \
    X(vkCreateGraphicsPipelines) X(vkDestroyPipeline) X(vkCmdBeginRenderPass) X(vkCmdEndRenderPass) \
    X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) X(vkCmdDraw) X(vkCreateFence) X(vkDestroyFence) X(vkResetFences)
#define DECLARE(n) PFN_##n n{};
    FUNCTIONS(DECLARE)
#undef DECLARE
    struct Buffer {VkBuffer handle{};VkDeviceMemory allocation{};VkDeviceSize size{};};
    struct Texture {VkImage handle{};VkDeviceMemory allocation{};VkImageView view{};VkFormat format{};};
    std::vector<Buffer> buffers;std::vector<Texture> textures;
    VkPhysicalDeviceProperties properties{};
    bool storageImageExtendedFormatsEnabled{};
    VulkanTestDevice(bool fragmentStores=false,VkExtent2D size={8,8},ExtensionQuery extensions=nullptr,bool vertexStores=false,bool ownLayerFeatures=false):extent(size){
        loader=LoadLibraryExW(L"vulkan-1.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!loader)throw std::runtime_error("Vulkan loader missing");
        gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader,"vkGetInstanceProcAddr"));
        const auto create=reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr,"vkCreateInstance"));
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=extensions?VK_API_VERSION_1_2:VK_API_VERSION_1_1;
        auto required=extensions?extensions(VK_NULL_HANDLE,VK_NULL_HANDLE):std::vector<const char*>{};
        VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ici.pApplicationInfo=&app;ici.enabledExtensionCount=uint32_t(required.size());ici.ppEnabledExtensionNames=required.data();Check(create(&ici,nullptr,&instance));
#define INSTANCE(n) const auto n=reinterpret_cast<PFN_##n>(gipa(instance,#n));if(!n)throw std::runtime_error(#n)
        INSTANCE(vkEnumeratePhysicalDevices);INSTANCE(vkGetPhysicalDeviceQueueFamilyProperties);INSTANCE(vkGetPhysicalDeviceFeatures2);
        INSTANCE(vkGetPhysicalDeviceMemoryProperties);INSTANCE(vkGetPhysicalDeviceProperties);INSTANCE(vkCreateDevice);INSTANCE(vkGetDeviceProcAddr);
        destroyInstance=reinterpret_cast<PFN_vkDestroyInstance>(gipa(instance,"vkDestroyInstance"));
        uint32_t count{};Check(vkEnumeratePhysicalDevices(instance,&count,nullptr));std::vector<VkPhysicalDevice> physicals(count);Check(vkEnumeratePhysicalDevices(instance,&count,physicals.data()));
        VkPhysicalDeviceMultiviewFeatures mv{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};VkPhysicalDeviceFeatures enabled{};
        for(auto candidate:physicals){VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};features.pNext=&mv;vkGetPhysicalDeviceFeatures2(candidate,&features);if(!mv.multiview)continue;
            VkPhysicalDeviceProperties candidateProperties{};vkGetPhysicalDeviceProperties(candidate,&candidateProperties);if(extensions&&candidateProperties.vendorID!=0x10de)continue;
            uint32_t n{};vkGetPhysicalDeviceQueueFamilyProperties(candidate,&n,nullptr);std::vector<VkQueueFamilyProperties> queues(n);vkGetPhysicalDeviceQueueFamilyProperties(candidate,&n,queues.data());
            if(!features.features.shaderStorageImageExtendedFormats||(fragmentStores&&!features.features.fragmentStoresAndAtomics)||(vertexStores&&!features.features.vertexPipelineStoresAndAtomics))continue;
            for(uint32_t q=0;q<n;++q)if((queues[q].queueFlags&(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT))==(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT)){physical=candidate;family=q;enabled.shaderStorageImageExtendedFormats=VK_TRUE;enabled.fragmentStoresAndAtomics=fragmentStores?VK_TRUE:VK_FALSE;enabled.vertexPipelineStoresAndAtomics=vertexStores?VK_TRUE:VK_FALSE;break;}if(physical)break;}
        if(!physical)throw std::runtime_error("Multiview GPU unavailable");vkGetPhysicalDeviceProperties(physical,&properties);std::cout<<"GPU "<<properties.deviceName<<'\n';
        vkGetPhysicalDeviceMemoryProperties(physical,&memory);mv.multiviewGeometryShader=mv.multiviewTessellationShader=VK_FALSE;
        float priority=1;VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};q.queueFamilyIndex=family;q.queueCount=1;q.pQueuePriorities=&priority;
        VkDeviceCreateInfo d{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};d.pNext=&mv;d.pEnabledFeatures=&enabled;d.queueCreateInfoCount=1;d.pQueueCreateInfos=&q;
        VkPhysicalDeviceFeatures ownCore{};
        if(ownLayerFeatures){
            // DOOM does not request this bit when it starts in DLAA. Exercise
            // production negotiation instead of silently enabling it in tests.
            enabled.shaderStorageImageExtendedFormats=VK_FALSE;
            VkPhysicalDeviceFeatures2 supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};vkGetPhysicalDeviceFeatures2(physical,&supported);
            auto runtime=d;
            if(!kharvox::prepareDoomReconstructionFeatures(true,supported.features,d,runtime,ownCore))throw std::runtime_error("Own-Layer core feature negotiation failed");
            if(enabled.shaderStorageImageExtendedFormats||!kharvox::doomStorageImageFormatsEnabled(runtime))throw std::runtime_error("Modified caller features or lost XR feature");
        }
#ifdef KHARVOX_DLSS_CHAIN_TEST
        kharvox::dlss::DeviceFeatureSelection dlssFeatures;
#endif
        if(extensions){required=extensions(instance,physical);d.enabledExtensionCount=uint32_t(required.size());d.ppEnabledExtensionNames=required.data();
#ifdef KHARVOX_DLSS_CHAIN_TEST
            std::vector<VkExtensionProperties> properties;for(const auto* name:required){VkExtensionProperties p{};std::strncpy(p.extensionName,name,VK_MAX_EXTENSION_NAME_SIZE-1);properties.push_back(p);}
            if(!dlssFeatures.Prepare(d,properties,physical,vkGetPhysicalDeviceFeatures2))throw std::runtime_error(dlssFeatures.Error());
            d=dlssFeatures.Info();std::cout<<"DLSS feature negotiation preserves SFS multiview and core bits; adds buffer device address only\n";
#endif
        }
        storageImageExtendedFormatsEnabled=kharvox::doomStorageImageFormatsEnabled(d);
        Check(vkCreateDevice(physical,&d,nullptr,&device));
        destroyDevice=reinterpret_cast<PFN_vkDestroyDevice>(vkGetDeviceProcAddr(device,"vkDestroyDevice"));
        resolve=vkGetDeviceProcAddr;
#define LOAD(n) n=reinterpret_cast<PFN_##n>(vkGetDeviceProcAddr(device,#n));if(!n)throw std::runtime_error(#n);
        FUNCTIONS(LOAD)
#undef LOAD
#undef INSTANCE
#undef FUNCTIONS
        const auto getQueue=reinterpret_cast<PFN_vkGetDeviceQueue>(vkGetDeviceProcAddr(device,"vkGetDeviceQueue"));getQueue(device,family,0,&queue);
        VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};cp.queueFamilyIndex=family;Check(vkCreateCommandPool(device,&cp,nullptr,&pool));
        VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=pool;ca.commandBufferCount=1;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;Check(vkAllocateCommandBuffers(device,&ca,&command));
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};Check(vkCreateFence(device,&fi,nullptr,&done));
    }
    uint32_t MemoryType(uint32_t bits,VkMemoryPropertyFlags flags){for(uint32_t i=0;i<memory.memoryTypeCount;++i)if((bits&(1u<<i))&&(memory.memoryTypes[i].propertyFlags&flags)==flags)return i;throw std::runtime_error("Memory type missing");}
    Buffer MakeBuffer(VkDeviceSize size,VkBufferUsageFlags usage){
        Buffer b{};b.size=size;VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};ci.size=size;ci.usage=usage;Check(vkCreateBuffer(device,&ci,nullptr,&b.handle));
        VkMemoryRequirements req{};vkGetBufferMemoryRequirements(device,b.handle,&req);VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=MemoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        Check(vkAllocateMemory(device,&ai,nullptr,&b.allocation));Check(vkBindBufferMemory(device,b.handle,b.allocation,0));buffers.push_back(b);return b;
    }
    void Write(Buffer b,const void* data){void* mapped{};Check(vkMapMemory(device,b.allocation,0,b.size,0,&mapped));std::memcpy(mapped,data,size_t(b.size));vkUnmapMemory(device,b.allocation);}
    std::vector<float> Read(Buffer b){void* mapped{};Check(vkMapMemory(device,b.allocation,0,b.size,0,&mapped));std::vector<float> data(size_t(b.size)/4);std::memcpy(data.data(),mapped,size_t(b.size));vkUnmapMemory(device,b.allocation);return data;}
    Texture MakeTexture(VkFormat format,VkImageUsageFlags usage,uint32_t inputLayers=2,VkImageAspectFlags aspects=VK_IMAGE_ASPECT_COLOR_BIT){
        Texture t{};t.format=format;VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.imageType=VK_IMAGE_TYPE_2D;ci.extent={extent.width,extent.height,1};ci.mipLevels=1;ci.arrayLayers=inputLayers;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.format=format;ci.usage=usage;Check(vkCreateImage(device,&ci,nullptr,&t.handle));
        VkMemoryRequirements req{};vkGetImageMemoryRequirements(device,t.handle,&req);VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=MemoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        Check(vkAllocateMemory(device,&ai,nullptr,&t.allocation));Check(vkBindImageMemory(device,t.handle,t.allocation,0));
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=t.handle;vi.viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY;vi.format=format;vi.subresourceRange={aspects,0,1,0,2};Check(vkCreateImageView(device,&vi,nullptr,&t.view));textures.push_back(t);return t;
    }
    void Begin(){Check(vkResetCommandPool(device,pool,0));VkCommandBufferBeginInfo b{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};Check(vkBeginCommandBuffer(command,&b));}
    void Finish(){Check(vkEndCommandBuffer(command));Check(vkResetFences(device,1,&done));VkSubmitInfo s{VK_STRUCTURE_TYPE_SUBMIT_INFO};s.commandBufferCount=1;s.pCommandBuffers=&command;Check(vkQueueSubmit(queue,1,&s,done));Check(vkQueueWaitIdle(queue));}
    void Barrier(Texture t,VkImageLayout before,VkImageLayout after,VkImageAspectFlags aspects=VK_IMAGE_ASPECT_COLOR_BIT){VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=t.handle;b.subresourceRange={aspects,0,1,0,2};b.oldLayout=before;b.newLayout=after;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.srcAccessMask=before==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT;b.dstAccessMask=VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT;vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);}
    void Upload(Texture t,Buffer b,VkImageLayout old){Barrier(t,old,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);VkBufferImageCopy c{};c.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,2};c.imageExtent={extent.width,extent.height,1};vkCmdCopyBufferToImage(command,b.handle,t.handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&c);Barrier(t,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);}
    void Download(Texture t,Buffer b){VkBufferImageCopy c{};c.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,2};c.imageExtent={extent.width,extent.height,1};vkCmdCopyImageToBuffer(command,t.handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,b.handle,1,&c);
        VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};host.buffer=b.handle;host.size=VK_WHOLE_SIZE;host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);}
    ~VulkanTestDevice(){if(device){if(vkQueueWaitIdle)vkQueueWaitIdle(queue);for(auto t:textures){vkDestroyImageView(device,t.view,nullptr);vkDestroyImage(device,t.handle,nullptr);vkFreeMemory(device,t.allocation,nullptr);}for(auto b:buffers){vkDestroyBuffer(device,b.handle,nullptr);vkFreeMemory(device,b.allocation,nullptr);}if(done)vkDestroyFence(device,done,nullptr);if(pool)vkDestroyCommandPool(device,pool,nullptr);destroyDevice(device,nullptr);}if(instance)destroyInstance(instance,nullptr);if(loader)FreeLibrary(loader);}
};
