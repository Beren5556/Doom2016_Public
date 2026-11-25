#pragma once
#include "VulkanDlssRequirements.h"
#include <utility>

namespace kharvox::dlss {
// Explicit dependency closure for the currently qualified NGX requirements.
// Unknown requirements are rejected until their dependencies/features are
// reviewed. API 1.0 needs device-group creation/group for KHR buffer addresses.
std::vector<VkExtensionProperties> StartupRequirements(std::vector<VkExtensionProperties>,bool instance,uint32_t apiVersion);

class InstanceStartup {
    ExtensionSelection extensions_;
    std::vector<const char*> names_;
    VkInstanceCreateInfo info_{};
    std::string error_;
public:
    Options options;
    uint32_t apiVersion{VK_API_VERSION_1_0};
    explicit InstanceStartup(Options o):options(std::move(o)){}
    InstanceStartup(const InstanceStartup&)=delete;
    InstanceStartup& operator=(const InstanceStartup&)=delete;
    bool Prepare(const VkInstanceCreateInfo&,PFN_vkGetInstanceProcAddr,PFN_vkEnumerateInstanceExtensionProperties);
    const VkInstanceCreateInfo& Info() const {return info_;}
    const std::string& Error() const {return error_;}
};

class DeviceStartup {
    ExtensionSelection extensions_,runtimeExtensions_;
    std::vector<const char*> names_,runtimeNames_;
    DeviceFeatureSelection features_,runtimeFeatures_;
    std::string error_;
    VkDeviceCreateInfo original_{},runtimeOriginal_{};
    bool ready_{};
public:
    Options options;
    explicit DeviceStartup(const InstanceStartup& i):options(i.options),apiVersion_(i.apiVersion){}
    bool Prepare(VkPhysicalDevice,const VkDeviceCreateInfo&,const VkDeviceCreateInfo& runtimeHost);
    const VkDeviceCreateInfo& Info() const {return ready_?features_.Info():original_;}
    const VkDeviceCreateInfo& RuntimeInfo() const {return ready_?runtimeFeatures_.Info():runtimeOriginal_;}
    const std::string& Error() const {return error_;}
private:
    uint32_t apiVersion_;
};

// Startup only, before exposing the new device to game work. Performs NGX
// initialization/capability query/shutdown without creating/evaluating DLSS
// features or touching game images. options dispatch must bypass SFS hooks.
bool ProbeCapabilities(const Options&,std::string& error);
}
