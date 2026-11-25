// Isolated capability/create probe. Never injected into a game or global runtime.
#define KHARVOX_DLSS_CHAIN_TEST
#include "VulkanTestDevice.h"
#include "../src/dlss/VulkanDlss.h"
#include "../src/dlss/VulkanDlssRequirements.h"
#include <nvsdk_ngx_helpers_vk.h>
#include <filesystem>
#include <iomanip>
#include <cmath>
#include "../src/neural/SnippetCompatibility.h"

namespace dl=kharvox::dlss;
static dl::Options options;
static dl::ExtensionSelection selected;
static std::vector<const char*> extensions(VkInstance instance,VkPhysicalDevice physical){
 options.instance=instance;options.physical=physical;
 const auto gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(GetModuleHandleW(L"vulkan-1.dll"),"vkGetInstanceProcAddr"));
 const auto required=instance?dl::DeviceRequirements(options):dl::InstanceRequirements(options);
 const auto available=instance?dl::DeviceExtensions(physical,reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(gipa(instance,"vkEnumerateDeviceExtensionProperties"))):
  dl::InstanceExtensions(reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(gipa(nullptr,"vkEnumerateInstanceExtensionProperties")));
 selected=dl::SelectExtensions(0,nullptr,required,available);
 if(!selected.Ready())throw std::runtime_error("Required Vulkan extensions unavailable");
 return selected.Pointers();
}
static void result(const char* stage,unsigned value){std::cout<<stage<<"=0x"<<std::hex<<value<<std::dec<<'\n';}
int main(int argc,char** argv)try{
 std::cout.setf(std::ios::unitbuf);
 if(argc!=4)throw std::runtime_error("Expected supplied NR DLL, SR runtime directory and private cache");
 options.runtimeDirectory=std::filesystem::absolute(argv[2]);options.cacheDirectory=std::filesystem::absolute(argv[3]);
 options.projectId="953a53d7-aeb4-49bb-957d-4a3e5c31c6f6";
 std::filesystem::create_directories(options.cacheDirectory);
 VulkanTestDevice gpu(false,{256,256},extensions);
 const auto runtime=options.runtimeDirectory.wstring();const wchar_t* paths[]{runtime.c_str()};
 NVSDK_NGX_FeatureCommonInfo info{};info.PathListInfo.Path=paths;info.PathListInfo.Length=1;
 auto status=NVSDK_NGX_VULKAN_Init_with_ProjectID(options.projectId.c_str(),NVSDK_NGX_ENGINE_TYPE_CUSTOM,"DOOM-Neural-probe",options.cacheDirectory.c_str(),gpu.instance,gpu.physical,gpu.device,gpu.gipa,gpu.resolve,&info);
 result("ngxInit",status);if(NVSDK_NGX_FAILED(status))return 1;
 NVSDK_NGX_Parameter* parameters{};
 status=NVSDK_NGX_VULKAN_GetCapabilityParameters(&parameters);result("parameters",status);if(NVSDK_NGX_FAILED(status))return 1;
 const auto library=std::filesystem::absolute(argv[1]);
 HMODULE module=LoadLibraryExW(library.c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
 if(!module){result("loadWin32",GetLastError());return 1;}
 kharvox::neural::compatibility::HookSnippetCallerCheck(module);
 using Init=NVSDK_NGX_Result(__cdecl*)(unsigned long long,const wchar_t*,VkInstance,VkPhysicalDevice,VkDevice,PFN_vkGetInstanceProcAddr,PFN_vkGetDeviceProcAddr,int,const NVSDK_NGX_Parameter*);
 using Create=NVSDK_NGX_Result(__cdecl*)(VkDevice,VkCommandBuffer,int,const NVSDK_NGX_Parameter*,NVSDK_NGX_Handle**);
 using Release=NVSDK_NGX_Result(__cdecl*)(NVSDK_NGX_Handle*);
 using Evaluate=NVSDK_NGX_Result(__cdecl*)(VkCommandBuffer,const NVSDK_NGX_Handle*,const NVSDK_NGX_Parameter*,void*);
 using Shutdown=NVSDK_NGX_Result(__cdecl*)(VkDevice);
 const auto init=reinterpret_cast<Init>(GetProcAddress(module,"NVSDK_NGX_VULKAN_Init_Ext2"));
 const auto create=reinterpret_cast<Create>(GetProcAddress(module,"NVSDK_NGX_VULKAN_CreateFeature1"));
 const auto release=reinterpret_cast<Release>(GetProcAddress(module,"NVSDK_NGX_VULKAN_ReleaseFeature"));
 const auto evaluate=reinterpret_cast<Evaluate>(GetProcAddress(module,"NVSDK_NGX_VULKAN_EvaluateFeature"));
 const auto shutdown=reinterpret_cast<Shutdown>(GetProcAddress(module,"NVSDK_NGX_VULKAN_Shutdown1"));
 if(!init||!create||!release||!evaluate||!shutdown)throw std::runtime_error("Supplied Neural Vulkan exports missing");
 parameters->Set("DLSSNR.Enabled",1u);parameters->Set("DLSSNR.Width",gpu.extent.width);parameters->Set("DLSSNR.Height",gpu.extent.height);
 parameters->Set("DLSSNR.Hint.Render.Preset",0u);parameters->Set("DLSSNR.UICorrection",0u);
 parameters->Set("CreationNodeMask",1u);parameters->Set("VisibilityNodeMask",1u);
 status=init(0x24480451ull,options.cacheDirectory.c_str(),gpu.instance,gpu.physical,gpu.device,gpu.gipa,gpu.resolve,0x15,parameters);
 result("neuralVulkanInit",status);
 if(!NVSDK_NGX_FAILED(status)){
  constexpr auto usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  const auto color=gpu.MakeTexture(VK_FORMAT_R16G16B16A16_SFLOAT,usage),depth=gpu.MakeTexture(VK_FORMAT_R32_SFLOAT,usage),motion=gpu.MakeTexture(VK_FORMAT_R32G32_SFLOAT,usage),output=gpu.MakeTexture(VK_FORMAT_R16G16B16A16_SFLOAT,usage);
  const size_t pixels=size_t(gpu.extent.width)*gpu.extent.height;
  std::vector<uint16_t> colors(pixels*2*4),sentinel(pixels*2*4,0x7e00);
  for(size_t p=0;p<pixels*2;++p){colors[p*4]=p<pixels?0x3a00:0x3000;colors[p*4+1]=0x3400;colors[p*4+2]=p<pixels?0x3000:0x3a00;colors[p*4+3]=0x3c00;}
  std::vector<float> depths(pixels*2,.5f),motions(pixels*4,0);
  const auto cb=gpu.MakeBuffer(colors.size()*2,VK_BUFFER_USAGE_TRANSFER_SRC_BIT),db=gpu.MakeBuffer(depths.size()*4,VK_BUFFER_USAGE_TRANSFER_SRC_BIT),mb=gpu.MakeBuffer(motions.size()*4,VK_BUFFER_USAGE_TRANSFER_SRC_BIT),ob=gpu.MakeBuffer(sentinel.size()*2,VK_BUFFER_USAGE_TRANSFER_SRC_BIT),readback=gpu.MakeBuffer(sentinel.size()*2,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
  gpu.Write(cb,colors.data());gpu.Write(db,depths.data());gpu.Write(mb,motions.data());gpu.Write(ob,sentinel.data());
  std::array<std::array<VkImageView,2>,4> views{};std::array<VulkanTestDevice::Texture,4> textures{color,depth,motion,output};
  for(unsigned i=0;i<4;++i)for(unsigned eye=0;eye<2;++eye){VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=textures[i].handle;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=textures[i].format;vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,eye,1};gpu.Check(gpu.vkCreateImageView(gpu.device,&vi,nullptr,&views[i][eye]));}
  gpu.Begin();gpu.Upload(color,cb,VK_IMAGE_LAYOUT_UNDEFINED);gpu.Upload(depth,db,VK_IMAGE_LAYOUT_UNDEFINED);gpu.Upload(motion,mb,VK_IMAGE_LAYOUT_UNDEFINED);gpu.Upload(output,ob,VK_IMAGE_LAYOUT_UNDEFINED);gpu.Finish();
  for(unsigned model=0;model<3;++model){
   parameters->Set("DLSSNR.Hint.Render.Preset",model);
   std::array<NVSDK_NGX_Handle*,2> handles{};
   gpu.Begin();for(auto& handle:handles){status=create(gpu.device,gpu.command,18,parameters,&handle);result("neuralVulkanCreate",status);if(NVSDK_NGX_FAILED(status))throw std::runtime_error("Neural create failed");}gpu.Finish();
   for(unsigned frame=0;frame<3;++frame){
    gpu.Begin();gpu.Upload(output,ob,frame||model?VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);gpu.Barrier(output,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL);
    for(unsigned eye=0;eye<2;++eye){
     std::array<NVSDK_NGX_Resource_VK,4> resources{};const char* fields[]{"Color","Depth","MVec","Output"};
     for(unsigned i=0;i<4;++i){resources[i]=NVSDK_NGX_Create_ImageView_Resource_VK(views[i][eye],textures[i].handle,{VK_IMAGE_ASPECT_COLOR_BIT,0,1,eye,1},textures[i].format,gpu.extent.width,gpu.extent.height,i==3);const std::string key=std::string("DLSSNR.")+fields[i];parameters->Set(key.c_str(),static_cast<void*>(&resources[i]));parameters->Set((key+"SubrectBaseX").c_str(),0u);parameters->Set((key+"SubrectBaseY").c_str(),0u);parameters->Set((key+"SubrectWidth").c_str(),gpu.extent.width);parameters->Set((key+"SubrectHeight").c_str(),gpu.extent.height);}
     parameters->Set("DLSSNR.DepthInverted",0u);parameters->Set("DLSSNR.Reset",frame?0u:1u);parameters->Set("DLSSNR.MVecScaleX",1.f);parameters->Set("DLSSNR.MVecScaleY",1.f);parameters->Set("Jitter.Offset.X",0.f);parameters->Set("Jitter.Offset.Y",0.f);parameters->Set("FrameTimeDeltaInMsec",11.111f);
     status=evaluate(gpu.command,handles[eye],parameters,nullptr);result("neuralVulkanEvaluate",status);if(NVSDK_NGX_FAILED(status))throw std::runtime_error("Neural evaluate failed");
    }
    gpu.Barrier(output,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);gpu.Download(output,readback);gpu.Finish();
    void* mapped{};gpu.Check(gpu.vkMapMemory(gpu.device,readback.allocation,0,readback.size,0,&mapped));const auto* halves=static_cast<const uint16_t*>(mapped);size_t invalid{};for(size_t i=0;i<sentinel.size();++i)if((halves[i]&0x7c00)==0x7c00)++invalid;
    std::cout<<"Neural model="<<model<<" frame="<<frame<<" invalidPixels="<<invalid<<" eye0RGBhalf="<<halves[0]<<','<<halves[1]<<','<<halves[2]<<" eye1RGBhalf="<<halves[pixels*4]<<','<<halves[pixels*4+1]<<','<<halves[pixels*4+2]<<'\n';gpu.vkUnmapMemory(gpu.device,readback.allocation);
    if(invalid)throw std::runtime_error("Neural did not write finite output across both eyes");
   }
   for(auto handle:handles)result("neuralRelease",release(handle));
  }
  for(auto pair:views)for(auto view:pair)gpu.vkDestroyImageView(gpu.device,view,nullptr);
  result("neuralShutdown",shutdown(gpu.device));
 }
 FreeLibrary(module);NVSDK_NGX_VULKAN_DestroyParameters(parameters);NVSDK_NGX_VULKAN_Shutdown1(gpu.device);
 std::cout<<"Neural Vulkan inference and readback passed for Auto/A/B, two independent eyes; game/VR not started\n";return NVSDK_NGX_FAILED(status)?3:0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
