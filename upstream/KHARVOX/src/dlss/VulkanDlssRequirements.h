#pragma once
#include "VulkanDlss.h"
#include <vector>

namespace kharvox::dlss {
inline constexpr const char* EngineVersion="KHARVOX-pilot-1";

// Query before creating the corresponding Vulkan object. Copies NGX-owned
// results immediately; later discovery calls cannot invalidate these names.
// Use the same runtime/cache/project identity as VulkanDlss initialization.
std::vector<VkExtensionProperties> InstanceRequirements(const Options&);
std::vector<VkExtensionProperties> DeviceRequirements(const Options&);

struct ExtensionSelection {
    std::vector<std::string> names;
    std::vector<std::string> missing;
    bool Ready() const {return missing.empty();}
    // Returned pointers borrow this object's strings. Keep both objects alive
    // and do not modify/copy/move the selection until vkCreate* has returned.
    std::vector<const char*> Pointers() const;
};

// Merge transactionally: failure leaves names equal to the host list, so the
// host can keep its original renderer. available must include any enabled
// layers' advertised extensions. No guessing core-promotion equivalences.
// This only selects extension names: it neither enables every supported
// feature nor edits the host's pNext, feature bits, queues or API version.
ExtensionSelection SelectExtensions(uint32_t hostCount,const char* const* host,
    const std::vector<VkExtensionProperties>& required,
    const std::vector<VkExtensionProperties>& available);

// Enumerate through the host's next-layer dispatch (not a new Vulkan loader).
// Bounded retries handle VK_INCOMPLETE without accepting truncated results.
std::vector<VkExtensionProperties> InstanceExtensions(PFN_vkEnumerateInstanceExtensionProperties);
std::vector<VkExtensionProperties> DeviceExtensions(VkPhysicalDevice,PFN_vkEnumerateDeviceExtensionProperties);

// The discovered KHR_buffer_device_address extension also needs its feature.
// Preserve existing chains, including loader/OpenXR structures. If the host
// already supplies that feature (standalone or Vulkan 1.2), it must be enabled;
// do not mutate caller memory or insert a duplicate structure to override it.
// Other NGX feature requirements are not inferred from extension names.
class DeviceFeatureSelection {
    VkDeviceCreateInfo info_{};
    VkPhysicalDeviceBufferDeviceAddressFeatures address_{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
    std::string error_;
public:
    DeviceFeatureSelection()=default;
    DeviceFeatureSelection(const DeviceFeatureSelection&)=delete;
    DeviceFeatureSelection& operator=(const DeviceFeatureSelection&)=delete;
    bool Prepare(const VkDeviceCreateInfo&,const std::vector<VkExtensionProperties>& required,
        VkPhysicalDevice,PFN_vkGetPhysicalDeviceFeatures2);
    const VkDeviceCreateInfo& Info() const {return info_;}
    const std::string& Error() const {return error_;}
};
}
