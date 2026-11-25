#include "../src/dlss/VulkanDlssRequirements.h"
#include "../src/dlss/VulkanDlssStartup.h"
#include "../src/vulkan/DoomReconstructionFeatures.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace kharvox::dlss;
namespace {
void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
VkExtensionProperties prop(const char* name,uint32_t version=1){VkExtensionProperties p{};std::strncpy(p.extensionName,name,VK_MAX_EXTENSION_NAME_SIZE-1);p.specVersion=version;return p;}
template<class F> void rejects(F fn){bool failed=false;try{fn();}catch(const std::runtime_error&){failed=true;}need(failed,"Invalid discovery/enumeration was accepted");}
unsigned queries{},fills{};bool unstable{},error{};
bool addressSupported=true;
void VKAPI_PTR features(VkPhysicalDevice,VkPhysicalDeviceFeatures2* out){
    auto address=static_cast<VkPhysicalDeviceBufferDeviceAddressFeatures*>(out->pNext);
    need(address->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,"Wrong feature query");
    address->bufferDeviceAddress=addressSupported?VK_TRUE:VK_FALSE;
    // Supported does not mean requested: these must not be enabled by the plan.
    address->bufferDeviceAddressCaptureReplay=address->bufferDeviceAddressMultiDevice=VK_TRUE;
    out->features.shaderInt64=out->features.geometryShader=VK_TRUE;
}
VkResult VKAPI_PTR enumerate(const char*,uint32_t* n,VkExtensionProperties* p){
    if(error)return VK_ERROR_INITIALIZATION_FAILED;
    if(!p){++queries;*n=queries==1?1:2;return VK_SUCCESS;}
    ++fills;p[0]=prop("VK_test_one");
    if(unstable||fills==1)return VK_INCOMPLETE;
    need(*n>=2,"Retry did not re-query count");p[1]=prop("VK_test_two",2);*n=2;return VK_SUCCESS;
}
}
int main()try{
    const char* host[]{"VK_host", "VK_test_one"};
    std::vector<VkExtensionProperties> available{prop("VK_test_one"),prop("VK_test_two",2)};
    auto selected=SelectExtensions(2,host,{prop("VK_test_one"),prop("VK_test_two",2),prop("VK_test_two")},available);
    need(selected.Ready()&&selected.names==std::vector<std::string>{"VK_host","VK_test_one","VK_test_two"},"Host order or de-duplication changed");
    available.clear();auto copied=selected;selected={};const auto pointers=copied.Pointers();
    need(pointers.size()==3&&!std::strcmp(pointers[2],"VK_test_two"),"Selected names are borrowed from a temporary");
    auto missing=SelectExtensions(2,host,{prop("VK_test_two"),prop("VK_absent"),prop("VK_absent")},{prop("VK_test_two")});
    need(!missing.Ready()&&missing.missing==std::vector<std::string>{"VK_absent"}&&missing.names==std::vector<std::string>{host[0],host[1]},"Failed plan partially enabled requirements");
    auto old=SelectExtensions(2,host,{prop("VK_test_one",2)},{prop("VK_test_one",1)});
    need(!old.Ready()&&old.missing.size()==1,"Insufficient extension revision accepted, even when already enabled");
    need(SelectExtensions(0,nullptr,{},{}).Ready(),"Empty requirements rejected");
    rejects([&]{SelectExtensions(1,nullptr,{},{});});
    const char* invalid[]{nullptr};rejects([&]{SelectExtensions(1,invalid,{},{});});
    auto malformed=prop("unused");std::memset(malformed.extensionName,'x',VK_MAX_EXTENSION_NAME_SIZE);
    rejects([&]{SelectExtensions(0,nullptr,{malformed},{});});
    rejects([&]{SelectExtensions(0,nullptr,{}, {malformed});});
    auto result=InstanceExtensions(&enumerate);
    need(queries==2&&fills==2&&result.size()==2&&result[1].specVersion==2,"VK_INCOMPLETE did not retry faithfully");
    queries=fills=0;unstable=true;rejects([&]{InstanceExtensions(&enumerate);});need(queries==4&&fills==4,"Unbounded enumeration retry");
    error=true;rejects([&]{InstanceExtensions(&enumerate);});
    rejects([&]{InstanceExtensions(nullptr);});
    rejects([&]{DeviceExtensions(VK_NULL_HANDLE,nullptr);});
    rejects([&]{InstanceRequirements({});});rejects([&]{DeviceRequirements({});});
    const std::vector<VkExtensionProperties> addressRequired{prop(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME)};
    const auto physical=reinterpret_cast<VkPhysicalDevice>(uintptr_t(1));
    VkPhysicalDeviceMultiviewFeatures mv{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};mv.multiview=VK_TRUE;
    VkPhysicalDeviceFeatures core{};core.samplerAnisotropy=VK_TRUE;
    VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};ci.pNext=&mv;ci.pEnabledFeatures=&core;ci.queueCreateInfoCount=1;ci.pQueueCreateInfos=&queue;
    const auto before=ci;DeviceFeatureSelection plan;
    need(plan.Prepare(ci,addressRequired,physical,&features),"Feature insertion rejected");
    const auto added=static_cast<const VkPhysicalDeviceBufferDeviceAddressFeatures*>(plan.Info().pNext);
    need(added->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES&&added->bufferDeviceAddress&&added->pNext==&mv&&
        !added->bufferDeviceAddressCaptureReplay&&!added->bufferDeviceAddressMultiDevice,"Feature plan enabled unrequested flags or lost host chain");
    need(plan.Info().pEnabledFeatures==&core&&plan.Info().pQueueCreateInfos==&queue&&std::memcmp(&ci,&before,sizeof(ci))==0&&!mv.pNext,"Host structures were changed");
    addressSupported=false;need(!plan.Prepare(ci,addressRequired,physical,&features)&&plan.Info().pNext==ci.pNext,"Unsupported feature did not roll back");addressSupported=true;
    VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};v12.pNext=&mv;ci.pNext=&v12;
    need(!plan.Prepare(ci,addressRequired,physical,&features),"Overrode explicit host feature disable");
    v12.bufferDeviceAddress=VK_TRUE;need(plan.Prepare(ci,addressRequired,physical,&features)&&plan.Info().pNext==&v12,"Duplicated host Vulkan 1.2 feature");
    VkPhysicalDeviceBufferDeviceAddressFeatures existing{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};existing.bufferDeviceAddress=VK_TRUE;ci.pNext=&existing;
    need(plan.Prepare(ci,addressRequired,physical,&features)&&plan.Info().pNext==&existing,"Duplicated host standalone feature");
    existing.pNext=&v12;need(!plan.Prepare(ci,addressRequired,physical,&features),"Duplicate feature forms accepted");existing.pNext=nullptr;
    VkPhysicalDeviceFeatures2 hostFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};hostFeatures.pNext=&mv;ci.pNext=&hostFeatures;ci.pEnabledFeatures=nullptr;
    need(plan.Prepare(ci,addressRequired,physical,&features)&&!plan.Info().pEnabledFeatures&&hostFeatures.pNext==&mv,"Features2 host changed");
    const char* conflicting[]{VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME};ci.enabledExtensionCount=1;ci.ppEnabledExtensionNames=conflicting;
    need(!plan.Prepare(ci,addressRequired,physical,&features),"Conflicting EXT address extension accepted");ci.enabledExtensionCount=0;
    need(!plan.Prepare(ci,addressRequired,physical,nullptr),"Missing query accepted");
    mv.pNext=&mv;need(!plan.Prepare(ci,addressRequired,physical,&features),"Cyclic feature chain accepted");mv.pNext=nullptr;
    need(plan.Prepare(ci,{},VK_NULL_HANDLE,nullptr)&&plan.Info().pNext==ci.pNext,"Empty requirements changed host");
    auto deps=StartupRequirements({prop(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME)},false,VK_API_VERSION_1_0);
    need(deps.size()==2&&!std::strcmp(deps[1].extensionName,VK_KHR_DEVICE_GROUP_EXTENSION_NAME),"API1.0 address dependency missing");
    need(StartupRequirements({prop(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME)},false,VK_API_VERSION_1_1).size()==1,"Unnecessary core dependency added");
    need(StartupRequirements({},true,0).size()==2&&StartupRequirements({},true,VK_API_VERSION_1_2).size()==1,"Instance dependency closure incorrect");
    rejects([&]{StartupRequirements({prop("VK_unknown_future_requirement")},false,VK_API_VERSION_1_2);});
    {
        VkPhysicalDeviceFeatures supported{},caller{},owned{};
        supported.shaderStorageImageExtendedFormats=supported.shaderInt64=VK_TRUE;
        caller.samplerAnisotropy=VK_TRUE;
        VkPhysicalDeviceMultiviewFeatures stereo{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};stereo.multiview=VK_TRUE;
        VkDeviceCreateInfo game{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};game.pNext=&stereo;game.pEnabledFeatures=&caller;
        auto runtime=game;
        need(kharvox::prepareDoomReconstructionFeatures(false,supported,game,runtime,owned)&&game.pEnabledFeatures==&caller,"Disabled Layer changed features");
        need(kharvox::prepareDoomReconstructionFeatures(true,supported,game,runtime,owned),"DLAA startup did not reserve live foveation feature");
        need(kharvox::doomStorageImageFormatsEnabled(game)&&kharvox::doomStorageImageFormatsEnabled(runtime)&&
            !caller.shaderStorageImageExtendedFormats&&owned.samplerAnisotropy&&!owned.shaderInt64&&game.pNext==&stereo&&runtime.pNext==&stereo,
            "Live feature negotiation changed caller, enabled unrelated supported bits or lost either creation chain");
        DeviceFeatureSelection ngx;
        need(ngx.Prepare(game,addressRequired,physical,&features)&&kharvox::doomStorageImageFormatsEnabled(ngx.Info()),"NGX preparation lost live foveation capability");
        game.pEnabledFeatures=runtime.pEnabledFeatures=&caller;supported.shaderStorageImageExtendedFormats=VK_FALSE;
        need(!kharvox::prepareDoomReconstructionFeatures(true,supported,game,runtime,owned)&&game.pEnabledFeatures==&caller,"Unsupported core feature accepted or partially mutated");
        supported.shaderStorageImageExtendedFormats=VK_TRUE;
        VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};f2.pNext=&stereo;
        game.pNext=runtime.pNext=&f2;game.pEnabledFeatures=runtime.pEnabledFeatures=nullptr;
        need(!kharvox::prepareDoomReconstructionFeatures(true,supported,game,runtime,owned)&&!f2.features.shaderStorageImageExtendedFormats,"Overrode explicit Features2 disable");
        f2.features.shaderStorageImageExtendedFormats=VK_TRUE;
        need(kharvox::prepareDoomReconstructionFeatures(true,supported,game,runtime,owned)&&game.pNext==&f2&&runtime.pNext==&f2&&
            !game.pEnabledFeatures&&!runtime.pEnabledFeatures&&f2.pNext==&stereo,"Features2 chain duplicated or mutated");
    }
    std::cout<<"PASS extension ownership/rollback/revisions, bounded enumeration, feature support, legacy/Features2/Vulkan1.2 chain preservation and conflicts; no GPU required\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
