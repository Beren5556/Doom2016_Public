#include "VulkanDlssStartup.h"
#include "PublicPhysicalDevice.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace kharvox::dlss {
std::vector<VkExtensionProperties> StartupRequirements(std::vector<VkExtensionProperties> required,bool instance,uint32_t apiVersion){
    if(!apiVersion)apiVersion=VK_API_VERSION_1_0;
    if(VK_API_VERSION_VARIANT(apiVersion)||VK_API_VERSION_MAJOR(apiVersion)!=1)throw std::runtime_error("Unqualified Vulkan API variant/version");
    for(const auto& p:required){
        if(!std::memchr(p.extensionName,0,VK_MAX_EXTENSION_NAME_SIZE))throw std::runtime_error("Malformed NGX extension");
        const std::string name=p.extensionName;
        const bool known=instance?name==VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME:
            name==VK_NVX_BINARY_IMPORT_EXTENSION_NAME||name==VK_NVX_IMAGE_VIEW_HANDLE_EXTENSION_NAME||
            name==VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME||name==VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME;
        if(!known)throw std::runtime_error("Unqualified NGX startup requirement: "+name);
    }
    auto add=[&](const char* name,uint32_t version){
        if(std::none_of(required.begin(),required.end(),[&](const auto& p){return !std::strcmp(p.extensionName,name);})){VkExtensionProperties p{};std::strncpy(p.extensionName,name,VK_MAX_EXTENSION_NAME_SIZE-1);p.specVersion=version;required.push_back(p);}
    };
    if(instance){
        add(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,1);
        // Device requirements cannot be queried until an instance exists.
        if(apiVersion<VK_API_VERSION_1_1)add(VK_KHR_DEVICE_GROUP_CREATION_EXTENSION_NAME,1);
    }else if(apiVersion<VK_API_VERSION_1_1&&std::any_of(required.begin(),required.end(),[](const auto& p){return !std::strcmp(p.extensionName,VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);}))
        add(VK_KHR_DEVICE_GROUP_EXTENSION_NAME,1);
    return required;
}
bool InstanceStartup::Prepare(const VkInstanceCreateInfo& host,PFN_vkGetInstanceProcAddr gipa,PFN_vkEnumerateInstanceExtensionProperties enumerate){
    info_=host;error_.clear();
    try{
        if(!gipa)throw std::runtime_error("Missing next-layer Vulkan instance dispatch");
        options.gipa=gipa;apiVersion=host.pApplicationInfo&&host.pApplicationInfo->apiVersion?host.pApplicationInfo->apiVersion:VK_API_VERSION_1_0;
        const auto required=StartupRequirements(InstanceRequirements(options),true,apiVersion);
        const auto available=InstanceExtensions(enumerate);
        extensions_=SelectExtensions(host.enabledExtensionCount,host.ppEnabledExtensionNames,required,available);
        if(!extensions_.Ready())throw std::runtime_error("Missing NGX instance extension: "+extensions_.missing.front());
        names_=extensions_.Pointers();info_.enabledExtensionCount=uint32_t(names_.size());info_.ppEnabledExtensionNames=names_.data();return true;
    }catch(const std::exception& e){error_=e.what();info_=host;return false;}
}
bool DeviceStartup::Prepare(VkPhysicalDevice physical,const VkDeviceCreateInfo& host,const VkDeviceCreateInfo& runtimeHost){
    original_=host;runtimeOriginal_=runtimeHost;ready_=false;error_.clear();
    try{
        options.physical=physical;
        if(!options.instance||!options.gipa||!physical)throw std::runtime_error("Missing prepared instance/device context");
        auto get=[&](const char* name){return options.gipa(options.instance,name);};
        if(options.ngxInstance){
            options.ngxPhysical=MatchPublicPhysical(options.ngxInstance,options.ngxGipa,options.instance,options.gipa,physical);
        }
        const auto required=StartupRequirements(DeviceRequirements(options),false,apiVersion_);
        const auto available=DeviceExtensions(physical,reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(get("vkEnumerateDeviceExtensionProperties")));
        extensions_=SelectExtensions(host.enabledExtensionCount,host.ppEnabledExtensionNames,required,available);
        runtimeExtensions_=SelectExtensions(runtimeHost.enabledExtensionCount,runtimeHost.ppEnabledExtensionNames,required,available);
        if(!extensions_.Ready()||!runtimeExtensions_.Ready())throw std::runtime_error("Missing NGX device extension");
        const auto query=reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(get(apiVersion_<VK_API_VERSION_1_1?"vkGetPhysicalDeviceFeatures2KHR":"vkGetPhysicalDeviceFeatures2"));
        names_=extensions_.Pointers();runtimeNames_=runtimeExtensions_.Pointers();
        auto info=host,runtimeInfo=runtimeHost;
        info.enabledExtensionCount=uint32_t(names_.size());info.ppEnabledExtensionNames=names_.data();
        runtimeInfo.enabledExtensionCount=uint32_t(runtimeNames_.size());runtimeInfo.ppEnabledExtensionNames=runtimeNames_.data();
        if(!features_.Prepare(info,required,physical,query))throw std::runtime_error(features_.Error());
        if(!runtimeFeatures_.Prepare(runtimeInfo,required,physical,query))throw std::runtime_error(runtimeFeatures_.Error());
        ready_=true;return true;
    }catch(const std::exception& e){error_=e.what();return false;}
}
bool ProbeCapabilities(const Options& options,std::string& error){
    error.clear();try{VulkanDlss probe(options);if(probe.Evaluations())throw std::runtime_error("Unexpected evaluation during startup probe");return true;}
    catch(const std::exception& e){error=e.what();return false;}
}
}
