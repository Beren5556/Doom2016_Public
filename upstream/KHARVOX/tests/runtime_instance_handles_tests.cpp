#include <windows.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include "../src/dlss/PublicPhysicalDevice.h"

static void require(bool ok){if(!ok)std::abort();}
#ifdef KHARVOX_TEST_INSTANCE_LAYER
static VkInstance publicInstance{}, downstreamInstance{};
static PFN_vkGetInstanceProcAddr nextGipa{};
static VkResult VKAPI_CALL create(const VkInstanceCreateInfo* ci,const VkAllocationCallbacks* a,VkInstance* out){
    auto chain=reinterpret_cast<const VkLayerInstanceCreateInfo*>(ci->pNext);
    while(chain && (chain->sType!=VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO || chain->function!=VK_LAYER_LINK_INFO))
        chain=reinterpret_cast<const VkLayerInstanceCreateInfo*>(chain->pNext);
    require(chain && chain->u.pLayerInfo);
    auto link=chain->u.pLayerInfo;nextGipa=link->pfnNextGetInstanceProcAddr;
    const_cast<VkLayerInstanceCreateInfo*>(chain)->u.pLayerInfo=link->pNext;
    publicInstance=*out;
    auto result=reinterpret_cast<PFN_vkCreateInstance>(nextGipa(nullptr,"vkCreateInstance"))(ci,a,out);
    if(result==VK_SUCCESS)downstreamInstance=*out;
    return result;
}
extern "C" __declspec(dllexport) VkInstance testPublicInstance(){return publicInstance;}
extern "C" __declspec(dllexport) VkInstance testDownstreamInstance(){return downstreamInstance;}
extern "C" __declspec(dllexport) bool testPublicPhysical(){
    auto loader=GetModuleHandleW(L"vulkan-1.dll");
    auto publicGipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader,"vkGetInstanceProcAddr"));
    auto enumerate=reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(nextGipa(downstreamInstance,"vkEnumeratePhysicalDevices"));
    uint32_t count{};require(enumerate(downstreamInstance,&count,nullptr)==VK_SUCCESS&&count);
    std::vector<VkPhysicalDevice> physical(count);require(enumerate(downstreamInstance,&count,physical.data())==VK_SUCCESS);
    for(auto p:physical){
        auto matched=kharvox::dlss::MatchPublicPhysical(publicInstance,publicGipa,downstreamInstance,nextGipa,p);
        VkPhysicalDeviceProperties properties{};
        reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(publicGipa(publicInstance,"vkGetPhysicalDeviceProperties"))(matched,&properties);
        require(properties.vendorID&&properties.deviceID);
    }
    return true;
}
extern "C" __declspec(dllexport) VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance i,const char* n){
    if(!std::strcmp(n,"vkGetInstanceProcAddr"))return reinterpret_cast<PFN_vkVoidFunction>(vkGetInstanceProcAddr);
    if(!std::strcmp(n,"vkCreateInstance"))return reinterpret_cast<PFN_vkVoidFunction>(create);
    return nextGipa?nextGipa(i,n):nullptr;
}
#else
int main(int argc,char** argv){
    require(argc==2 || (argc==3 && !std::strcmp(argv[2],"--unwrapped")));
    auto library=LoadLibraryA(argv[1]);require(library);
    auto loader=LoadLibraryW(L"vulkan-1.dll");require(loader);
    auto gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader,"vkGetInstanceProcAddr"));require(gipa);
    const char* layer="VK_LAYER_KHARVOX_INSTANCE_TEST";
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_0;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pApplicationInfo=&app;
    ci.enabledLayerCount=1;ci.ppEnabledLayerNames=&layer;
    const char* extension=VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
    ci.enabledExtensionCount=1;ci.ppEnabledExtensionNames=&extension;
    VkInstance instance{};
    require(reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr,"vkCreateInstance"))(&ci,nullptr,&instance)==VK_SUCCESS);
    using Handle=VkInstance(*)();
    auto publicHandle=reinterpret_cast<Handle>(GetProcAddress(library,"testPublicInstance"))();
    auto downstreamHandle=reinterpret_cast<Handle>(GetProcAddress(library,"testDownstreamInstance"))();
    require(publicHandle==instance);
    if(argc==2)require(GetModuleHandleW(L"renderdoc.dll") && downstreamHandle!=publicHandle);
    else require(downstreamHandle==publicHandle);
    uint32_t count{};
    auto enumerate=reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(gipa(publicHandle,"vkEnumeratePhysicalDevices"));
    require(enumerate(publicHandle,&count,nullptr)==VK_SUCCESS && count);
    VkPhysicalDevice physical{};count=1;
    const auto result=enumerate(publicHandle,&count,&physical);require(result==VK_SUCCESS || result==VK_INCOMPLETE);
    VkPhysicalDeviceProperties properties{};
    reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(gipa(publicHandle,"vkGetPhysicalDeviceProperties"))(physical,&properties);
    require(properties.vendorID && properties.deviceID);
    require(reinterpret_cast<bool(*)()>(GetProcAddress(library,"testPublicPhysical"))());
    std::cout<<"Downstream to public physical UUID mapping PASS\n";
    std::cout<<(argc==2?"RenderDoc wrapped instance differs; ":"Unwrapped instance unchanged; ")
        <<"saved loader seed matches public instance; public enumeration/properties PASS: "<<properties.deviceName<<"\n";
    reinterpret_cast<PFN_vkDestroyInstance>(gipa(instance,"vkDestroyInstance"))(instance,nullptr);
    FreeLibrary(loader);FreeLibrary(library);
}
#endif
