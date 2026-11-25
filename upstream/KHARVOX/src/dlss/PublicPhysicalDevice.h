#pragma once
#include <vulkan/vulkan.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace kharvox::dlss {
inline VkPhysicalDevice MatchPublicPhysical(VkInstance publicInstance,PFN_vkGetInstanceProcAddr publicGipa,
    VkInstance downstreamInstance,PFN_vkGetInstanceProcAddr downstreamGipa,VkPhysicalDevice downstreamPhysical){
    if(!publicInstance||!publicGipa||!downstreamInstance||!downstreamGipa||!downstreamPhysical)
        throw std::runtime_error("Missing public/downstream GPU identity context");
    auto query=[](VkInstance instance,PFN_vkGetInstanceProcAddr gipa,VkPhysicalDevice physical){
        // The extension is explicitly enabled, including on Vulkan 1.0.
        auto fn=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(gipa(instance,"vkGetPhysicalDeviceProperties2KHR"));
        if(!fn)throw std::runtime_error("GPU UUID query unavailable");
        VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
        VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};props.pNext=&id;
        fn(physical,&props);
        if(std::all_of(std::begin(id.deviceUUID),std::end(id.deviceUUID),[](uint8_t v){return v==0;}))
            throw std::runtime_error("GPU UUID unavailable");
        id.pNext=nullptr;return id;
    };
    const auto target=query(downstreamInstance,downstreamGipa,downstreamPhysical);
    auto enumerate=reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(publicGipa(publicInstance,"vkEnumeratePhysicalDevices"));
    if(!enumerate)throw std::runtime_error("Public GPU enumeration unavailable");
    uint32_t count{};
    if(enumerate(publicInstance,&count,nullptr)!=VK_SUCCESS||!count||count>256)
        throw std::runtime_error("Public GPU count invalid");
    std::vector<VkPhysicalDevice> devices(count);
    if(enumerate(publicInstance,&count,devices.data())!=VK_SUCCESS||count>devices.size())
        throw std::runtime_error("Public GPU enumeration failed");
    VkPhysicalDevice match{};
    for(uint32_t n=0;n<count;++n){
        const auto id=query(publicInstance,publicGipa,devices[n]);
        if(!std::memcmp(target.deviceUUID,id.deviceUUID,VK_UUID_SIZE)){
            if(match)throw std::runtime_error("Ambiguous public GPU UUID");
            match=devices[n];
        }
    }
    if(!match)throw std::runtime_error("No matching public GPU UUID");
    return match;
}
}
