#include <windows.h>
#include "../src/dlss/VulkanDlssStartup.h"
#include "../src/vulkan/GlobalInstanceExtensions.h"
#include <iostream>
#include <stdexcept>
#include <cstring>
using namespace kharvox::dlss;
static void need(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
struct ProbeDispatch {
    inline static PFN_vkGetDeviceProcAddr next{};
    inline static VkDevice device{};
    inline static unsigned workCalls{};
    template<class T> static T fn(const char* name){return reinterpret_cast<T>(next(device,name));}
    static VkResult VKAPI_PTR Submit(VkQueue q,uint32_t n,const VkSubmitInfo* s,VkFence f){++workCalls;return fn<PFN_vkQueueSubmit>("vkQueueSubmit")(q,n,s,f);}
    static VkResult VKAPI_PTR Idle(VkDevice d){++workCalls;return fn<PFN_vkDeviceWaitIdle>("vkDeviceWaitIdle")(d);}
    static VkResult VKAPI_PTR QueueIdle(VkQueue q){++workCalls;return fn<PFN_vkQueueWaitIdle>("vkQueueWaitIdle")(q);}
    static VkResult VKAPI_PTR Wait(VkDevice d,uint32_t n,const VkFence* f,VkBool32 a,uint64_t t){++workCalls;return fn<PFN_vkWaitForFences>("vkWaitForFences")(d,n,f,a,t);}
    static VkResult VKAPI_PTR Begin(VkCommandBuffer c,const VkCommandBufferBeginInfo* b){++workCalls;return fn<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(c,b);}
    static VkResult VKAPI_PTR End(VkCommandBuffer c){++workCalls;return fn<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(c);}
    static PFN_vkVoidFunction VKAPI_PTR Get(VkDevice d,const char* name){
#define OBSERVE(n,f) if(!std::strcmp(name,n))return reinterpret_cast<PFN_vkVoidFunction>(&f)
        OBSERVE("vkQueueSubmit",Submit);OBSERVE("vkDeviceWaitIdle",Idle);OBSERVE("vkQueueWaitIdle",QueueIdle);
        OBSERVE("vkWaitForFences",Wait);OBSERVE("vkBeginCommandBuffer",Begin);OBSERVE("vkEndCommandBuffer",End);
#undef OBSERVE
        return next(d,name);
    }
};
struct Handles {
    HMODULE loader{};VkInstance instance{};VkDevice device{};
    PFN_vkDestroyInstance destroyInstance{};PFN_vkDestroyDevice destroyDevice{};
    ~Handles(){if(device&&destroyDevice)destroyDevice(device,nullptr);if(instance&&destroyInstance)destroyInstance(instance,nullptr);if(loader)FreeLibrary(loader);}
};
static void run(const std::filesystem::path& runtime,const std::filesystem::path& cache,uint32_t version){
    Handles h;h.loader=LoadLibraryExW(L"vulkan-1.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);need(h.loader,"Vulkan loader missing");
    const auto gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(h.loader,"vkGetInstanceProcAddr"));need(gipa,"Vulkan dispatch missing");
    Options o;o.runtimeDirectory=runtime;o.cacheDirectory=cache;o.projectId="953a53d7-aeb4-49bb-957d-4a3e5c31c6f6";o.externallySynchronized=true;
    InstanceStartup start(o);VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=version;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pApplicationInfo=&app;
    if(!start.Prepare(ci,gipa,kharvox::globalInstanceExtensions()))throw std::runtime_error(start.Error());
    need(start.Info().pApplicationInfo==&app&&app.apiVersion==version&&!ci.enabledExtensionCount,"Instance host was changed");
    const auto create=reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr,"vkCreateInstance"));
    need(create(&start.Info(),nullptr,&h.instance)==VK_SUCCESS,"Prepared instance creation failed");start.options.instance=h.instance;
    if(version==VK_API_VERSION_1_0){start.options.ngxInstance=h.instance;start.options.ngxGipa=gipa;}
    h.destroyInstance=reinterpret_cast<PFN_vkDestroyInstance>(gipa(h.instance,"vkDestroyInstance"));
#define GET(n) const auto n=reinterpret_cast<PFN_##n>(gipa(h.instance,#n));need(n,#n)
    GET(vkEnumeratePhysicalDevices);GET(vkGetPhysicalDeviceProperties);GET(vkGetPhysicalDeviceQueueFamilyProperties);GET(vkCreateDevice);GET(vkGetDeviceProcAddr);
#undef GET
    uint32_t count{};need(vkEnumeratePhysicalDevices(h.instance,&count,nullptr)==VK_SUCCESS,"GPU count failed");std::vector<VkPhysicalDevice> devices(count);
    need(vkEnumeratePhysicalDevices(h.instance,&count,devices.data())==VK_SUCCESS,"GPU list failed");VkPhysicalDevice physical{};uint32_t family{};
    for(auto p:devices){VkPhysicalDeviceProperties properties{};vkGetPhysicalDeviceProperties(p,&properties);if(properties.vendorID!=0x10de)continue;
        uint32_t n{};vkGetPhysicalDeviceQueueFamilyProperties(p,&n,nullptr);std::vector<VkQueueFamilyProperties> queues(n);vkGetPhysicalDeviceQueueFamilyProperties(p,&n,queues.data());
        for(uint32_t i=0;i<n;++i)if((queues[i].queueFlags&(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT))==(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT)){physical=p;family=i;break;}
        if(physical)break;
    }
    need(physical,"NVIDIA graphics+compute queue unavailable");
    float priority=1;VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};queue.queueFamilyIndex=family;queue.queueCount=1;queue.pQueuePriorities=&priority;
    VkPhysicalDeviceMultiviewFeatures mv{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES},runtimeMv{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};mv.multiview=runtimeMv.multiview=VK_TRUE;
    const char* hostExtension=VK_KHR_MULTIVIEW_EXTENSION_NAME;
    VkDeviceCreateInfo d{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};d.pNext=&mv;d.queueCreateInfoCount=1;d.pQueueCreateInfos=&queue;d.enabledExtensionCount=1;d.ppEnabledExtensionNames=&hostExtension;
    VkPhysicalDeviceFeatures2 hostFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};hostFeatures.pNext=&mv;
    if(version>=VK_API_VERSION_1_2)d.pNext=&hostFeatures;
    auto runtimeD=d;runtimeD.pNext=&runtimeMv;
    DeviceStartup device(start);if(!device.Prepare(physical,d,runtimeD))throw std::runtime_error(device.Error());
    const auto down=static_cast<const VkPhysicalDeviceBufferDeviceAddressFeatures*>(device.Info().pNext);
    const auto xr=static_cast<const VkPhysicalDeviceBufferDeviceAddressFeatures*>(device.RuntimeInfo().pNext);
    need(down!=xr&&down->pNext==d.pNext&&xr->pNext==runtimeD.pNext&&down->bufferDeviceAddress&&xr->bufferDeviceAddress,"Separate host chains lost");
    const auto& chosen=version<VK_API_VERSION_1_2?device.Info():device.RuntimeInfo();
    need(vkCreateDevice(physical,&chosen,nullptr,&h.device)==VK_SUCCESS,"Prepared device creation failed");
    h.destroyDevice=reinterpret_cast<PFN_vkDestroyDevice>(vkGetDeviceProcAddr(h.device,"vkDestroyDevice"));
    auto options=device.options;options.device=h.device;options.queueFamily=family;options.gdpa=&ProbeDispatch::Get;
    ProbeDispatch::next=vkGetDeviceProcAddr;ProbeDispatch::device=h.device;ProbeDispatch::workCalls=0;
    reinterpret_cast<PFN_vkGetDeviceQueue>(vkGetDeviceProcAddr(h.device,"vkGetDeviceQueue"))(h.device,family,0,&options.queue);
    std::string error;if(!ProbeCapabilities(options,error))throw std::runtime_error(error);
    need(!ProbeDispatch::workCalls,"Startup probe recorded/submitted/waited for GPU work");
    std::cout<<"PASS Vulkan "<<VK_API_VERSION_MAJOR(version)<<'.'<<VK_API_VERSION_MINOR(version)<<" NGX capability probe; multiview, distinct downstream/runtime chains; zero work dispatches\n";
}
int main(int argc,char** argv)try{
    need(argc==3,"Expected runtime/cache paths");const auto runtime=std::filesystem::absolute(argv[1]),cache=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(cache);
    run(runtime,cache,VK_API_VERSION_1_0);run(runtime,cache,VK_API_VERSION_1_2);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
