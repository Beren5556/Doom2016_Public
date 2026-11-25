#include "VulkanDlssRequirements.h"
#include <nvsdk_ngx_vk.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace kharvox::dlss {
namespace {
std::string Name(const VkExtensionProperties& p){
    const auto end=static_cast<const char*>(std::memchr(p.extensionName,0,VK_MAX_EXTENSION_NAME_SIZE));
    if(!end||end==p.extensionName)throw std::runtime_error("Malformed Vulkan extension name");
    return {p.extensionName,end};
}
std::vector<VkExtensionProperties> Discover(const Options& o,bool device){
    if(o.projectId.empty()||!o.runtimeDirectory.is_absolute()||!o.cacheDirectory.is_absolute()||
       !std::filesystem::is_regular_file(o.runtimeDirectory/L"nvngx_dlss.dll"))
        throw std::runtime_error("Explicit NGX project/runtime/cache identity required for discovery");
    if(device&&(!o.instance||!o.physical))throw std::runtime_error("NGX device discovery requires instance and physical device");
    const auto runtime=o.runtimeDirectory.wstring();const wchar_t* paths[]{runtime.c_str()};
    NVSDK_NGX_FeatureCommonInfo common{};common.PathListInfo.Path=paths;common.PathListInfo.Length=1;
    NVSDK_NGX_FeatureDiscoveryInfo d{};d.SDKVersion=NVSDK_NGX_Version_API;d.FeatureID=NVSDK_NGX_Feature_SuperSampling;
    d.Identifier.IdentifierType=NVSDK_NGX_Application_Identifier_Type_Project_Id;
    d.Identifier.v.ProjectDesc={o.projectId.c_str(),NVSDK_NGX_ENGINE_TYPE_CUSTOM,EngineVersion};
    d.ApplicationDataPath=o.cacheDirectory.c_str();d.FeatureInfo=&common;
    uint32_t count{};VkExtensionProperties* properties{};
    if(device&&o.ngxInstance&&!o.ngxPhysical)throw std::runtime_error("Missing public NGX physical device");
    const auto result=device?NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(o.ngxInstance?o.ngxInstance:o.instance,o.ngxInstance?o.ngxPhysical:o.physical,&d,&count,&properties):
        NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(&d,&count,&properties);
    if(NVSDK_NGX_FAILED(result))throw std::runtime_error("NGX extension discovery failed: "+std::to_string(uint32_t(result)));
    if(count>4096||(count&&!properties))throw std::runtime_error("Invalid NGX extension discovery result");
    std::vector<VkExtensionProperties> owned;
    if(count)owned.assign(properties,properties+count);
    for(const auto& p:owned)Name(p);
    return owned;
}
template<class F> std::vector<VkExtensionProperties> Enumerate(F call){
    for(unsigned attempt=0;attempt<4;++attempt){
        uint32_t count{};const auto first=call(&count,nullptr);
        if(first!=VK_SUCCESS&&first!=VK_INCOMPLETE)throw std::runtime_error("Vulkan extension count query failed: "+std::to_string(first));
        if(count>4096)throw std::runtime_error("Invalid Vulkan extension count");
        if(first==VK_INCOMPLETE)continue;
        if(!count)return {};
        std::vector<VkExtensionProperties> result(count);const auto capacity=count;
        const auto second=call(&count,result.data());
        if(second==VK_INCOMPLETE)continue;
        if(second!=VK_SUCCESS||count>capacity)throw std::runtime_error("Vulkan extension enumeration failed");
        result.resize(count);for(const auto& p:result)Name(p);return result;
    }
    throw std::runtime_error("Vulkan extension list kept changing");
}
}
std::vector<VkExtensionProperties> InstanceRequirements(const Options& o){return Discover(o,false);}
std::vector<VkExtensionProperties> DeviceRequirements(const Options& o){return Discover(o,true);}
std::vector<VkExtensionProperties> InstanceExtensions(PFN_vkEnumerateInstanceExtensionProperties fn){
    if(!fn)throw std::runtime_error("Missing instance extension enumerator");
    return Enumerate([&](uint32_t* n,VkExtensionProperties* p){return fn(nullptr,n,p);});
}
std::vector<VkExtensionProperties> DeviceExtensions(VkPhysicalDevice device,PFN_vkEnumerateDeviceExtensionProperties fn){
    if(!device||!fn)throw std::runtime_error("Missing device extension enumerator/context");
    return Enumerate([&](uint32_t* n,VkExtensionProperties* p){return fn(device,nullptr,n,p);});
}
std::vector<const char*> ExtensionSelection::Pointers() const {
    std::vector<const char*> out;out.reserve(names.size());for(const auto& n:names)out.push_back(n.c_str());return out;
}
ExtensionSelection SelectExtensions(uint32_t count,const char* const* host,
    const std::vector<VkExtensionProperties>& required,const std::vector<VkExtensionProperties>& available){
    if(count&&!host)throw std::runtime_error("Missing host extension names");
    ExtensionSelection out;
    for(uint32_t n=0;n<count;++n){if(!host[n]||!host[n][0])throw std::runtime_error("Invalid host extension name");out.names.emplace_back(host[n]);}
    for(const auto& p:available)Name(p);
    for(const auto& p:required){const auto name=Name(p);
        const bool present=std::any_of(available.begin(),available.end(),[&](const auto& a){return name==a.extensionName&&a.specVersion>=p.specVersion;});
        if(!present&&std::find(out.missing.begin(),out.missing.end(),name)==out.missing.end())out.missing.push_back(name);
    }
    if(!out.Ready())return out;
    for(const auto& p:required){const auto name=Name(p);if(std::find(out.names.begin(),out.names.end(),name)==out.names.end())out.names.push_back(name);}
    return out;
}
bool DeviceFeatureSelection::Prepare(const VkDeviceCreateInfo& host,const std::vector<VkExtensionProperties>& required,
    VkPhysicalDevice physical,PFN_vkGetPhysicalDeviceFeatures2 query){
    info_=host;address_={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};error_.clear();
    auto fail=[&](const char* why){error_=why;return false;};
    bool needed=false;for(const auto& p:required)needed|=Name(p)==VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME;
    if(!needed)return true;
    if(host.enabledExtensionCount&&!host.ppEnabledExtensionNames)return fail("Missing host extensions");
    for(uint32_t i=0;i<host.enabledExtensionCount;++i){const char* name=host.ppEnabledExtensionNames[i];
        if(!name)return fail("Invalid host extension");
        if(std::strcmp(name,VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME)==0)return fail("KHR and EXT buffer device address cannot both be enabled");
    }
    unsigned count=0;bool already=false;
    std::vector<const VkBaseInStructure*> visited;
    for(auto node=reinterpret_cast<const VkBaseInStructure*>(host.pNext);node;node=node->pNext){
        if(visited.size()>=256||std::find(visited.begin(),visited.end(),node)!=visited.end())return fail("Invalid device feature chain");
        visited.push_back(node);
        if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES){++count;already=reinterpret_cast<const VkPhysicalDeviceBufferDeviceAddressFeatures*>(node)->bufferDeviceAddress!=0;}
        if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES){++count;already=reinterpret_cast<const VkPhysicalDeviceVulkan12Features*>(node)->bufferDeviceAddress!=0;}
    }
    if(count>1)return fail("Duplicate buffer device address feature structures");
    if(count&&!already)return fail("Host explicitly disabled buffer device address");
    if(!physical||!query)return fail("Buffer device address support query unavailable");
    VkPhysicalDeviceBufferDeviceAddressFeatures supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};features.pNext=&supported;query(physical,&features);
    if(!supported.bufferDeviceAddress)return fail("Buffer device address unsupported");
    if(!count){address_.bufferDeviceAddress=VK_TRUE;address_.pNext=const_cast<void*>(host.pNext);info_.pNext=&address_;}
    return true;
}
}
