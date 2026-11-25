#include "DoomNeuralVulkan.h"
#include "WorkerProtocol.h"
#include "RuntimePaths.h"
#include "RuntimeLog.h"
#include <sstream>

namespace doomnp {
using namespace es2np;
static void VkCheck(VkResult r,const char* what){if(r!=VK_SUCCESS)throw std::runtime_error(std::string(what)+" Vulkan="+std::to_string(r));}
struct VulkanPost::Impl {
 unsigned diagnostic{};
 Config config{};bool loaded{},failed{},pending{},externalOwned=true;std::string message="Neural Post 1.1-dev1";
 VkDevice device{};VkQueue queue{};uint32_t family{};KharvoxVulkanDispatch vk{};
 VkImage image{};VkDeviceMemory memory{};VkCommandPool pool{};VkCommandBuffer cmd{};VkFence fence{};
 uint32_t width{},height{};VkFormat format{};
 HMODULE module{};void* context{};Process12 process12{};Destroy destroy{};Suspend suspendFn{};ReadStatus readStatus{};
 ComPtr<ID3D12Device> d12;ComPtr<ID3D12CommandQueue> q12;ComPtr<ID3D12Resource> shared,retained;
 ComPtr<ID3D12CommandAllocator> alloc;ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12Fence> done;
 Handle event;uint64_t sequence{};bool d3dPending{};
 void load(){if(loaded)return;loaded=true;std::string error;
  if(!Read(kharvox::runtimePath(L"DOOMNeuralPost.ini"),config,error)){failed=true;message=error;}
  kharvox::writeRuntimeLog("[NEURAL-POST]",failed?"Configuracion rechazada; original conservado: "+message:
      (config.values[Enabled]?"1.1-dev1 activado por INI; punto de ejecucion anterior a XR release; esperando escena":"1.1-dev1 desactivado por INI o archivo ausente"),true);
 }
 bool enabled(){load();return !failed&&config.values[Enabled]!=0;}
 void barrier(VkImage img,VkImageLayout old,VkImageLayout next,uint32_t from=VK_QUEUE_FAMILY_IGNORED,uint32_t to=VK_QUEUE_FAMILY_IGNORED){
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.oldLayout=old;b.newLayout=next;b.image=img;b.srcQueueFamilyIndex=from;b.dstQueueFamilyIndex=to;
  b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};b.srcAccessMask=from==VK_QUEUE_FAMILY_EXTERNAL?0:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
  b.dstAccessMask=to==VK_QUEUE_FAMILY_EXTERNAL?0:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
  vk.cmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);
 }
 void begin(){VkCheck(vk.resetCommandBuffer(cmd,0),"Neural reset");VkCommandBufferBeginInfo b{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};b.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;VkCheck(vk.beginCommandBuffer(cmd,&b),"Neural begin");}
 void submit(){VkCheck(vk.endCommandBuffer(cmd),"Neural end");VkCheck(vk.resetFences(device,1,&fence),"Neural reset fence");VkSubmitInfo s{VK_STRUCTURE_TYPE_SUBMIT_INFO};s.commandBufferCount=1;s.pCommandBuffers=&cmd;
  VkCheck(vk.queueSubmit(queue,1,&s,fence),"Neural submit");pending=true;
  VkCheck(vk.waitForFences(device,1,&fence,VK_TRUE,5000000000ull),"Neural completion");pending=false;
 }
 void wait12(){Check(q12->Signal(done.Get(),++sequence),"Neural bridge signal");d3dPending=true;Check(done->SetEventOnCompletion(sequence,event),"Neural bridge event");Require(WaitForSingleObject(event,5000)==WAIT_OBJECT_0&&done->GetCompletedValue()!=UINT64_MAX,"Neural bridge GPU timeout");d3dPending=false;}
 void clearImage(){if(image)vk.destroyImage(device,image,nullptr);image={};if(memory)vk.freeMemory(device,memory,nullptr);memory={};shared.Reset();width=height=0;externalOwned=true;}
 void initialize(VkPhysicalDevice physical,VkDevice dev,VkQueue q,uint32_t f,const KharvoxVulkanDispatch& dispatch){
  device=dev;queue=q;family=f;vk=dispatch;
  // Match the Windows adapter LUID, including PCs with identical GPU models.
  Require(vk.getPhysicalDeviceProperties2,"Falta consulta de identidad Vulkan");
  VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};props.pNext=&id;vk.getPhysicalDeviceProperties2(physical,&props);Require(id.deviceLUIDValid,"Vulkan no publica LUID");
  LUID luid{};static_assert(sizeof(luid)==VK_LUID_SIZE);memcpy(&luid,id.deviceLUID,sizeof(luid));ComPtr<IDXGIFactory4> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"Neural DXGI factory");
  ComPtr<IDXGIAdapter1> chosen;Check(factory->EnumAdapterByLuid(luid,IID_PPV_ARGS(&chosen)),"Adaptador Vulkan/D3D12 por LUID");Check(D3D12CreateDevice(chosen.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&d12)),"Neural D3D12 device");
  D3D12_COMMAND_QUEUE_DESC desc{};Check(d12->CreateCommandQueue(&desc,IID_PPV_ARGS(&q12)),"Neural D3D12 queue");Check(d12->CreateCommandAllocator(desc.Type,IID_PPV_ARGS(&alloc)),"Neural allocator");Check(d12->CreateCommandList(0,desc.Type,alloc.Get(),nullptr,IID_PPV_ARGS(&list)),"Neural list");Check(list->Close(),"Neural list close");Check(d12->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&done)),"Neural D3D fence");event.reset(CreateEventW(nullptr,FALSE,FALSE,nullptr));Require(event.h,"Neural event");
  VkCommandPoolCreateInfo pc{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pc.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;pc.queueFamilyIndex=family;VkCheck(vk.createCommandPool(device,&pc,nullptr,&pool),"Neural pool");
  VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=pool;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;VkCheck(vk.allocateCommandBuffers(device,&ca,&cmd),"Neural command");VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkCheck(vk.createFence(device,&fc,nullptr,&fence),"Neural fence");
  module=LoadLibraryExW(kharvox::runtimePath(L"DOOMNeuralPost.dll").c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);Require(module,"Falta DOOMNeuralPost.dll");
  auto create=reinterpret_cast<Create>(GetProcAddress(module,"ES2NeuralPost_Create"));process12=reinterpret_cast<Process12>(GetProcAddress(module,"ES2NeuralPost_Process12"));destroy=reinterpret_cast<Destroy>(GetProcAddress(module,"ES2NeuralPost_Destroy"));suspendFn=reinterpret_cast<Suspend>(GetProcAddress(module,"ES2NeuralPost_Suspend"));readStatus=reinterpret_cast<ReadStatus>(GetProcAddress(module,"ES2NeuralPost_Status"));Require(create&&process12&&destroy&&suspendFn&&readStatus,"ABI Neural incompatible");
  context=create(&config,kharvox::runtimePath(L"NeuralRuntime").c_str(),kharvox::logPath(L"NeuralPost").c_str(),diagnostic);Require(context,"No se pudo crear Neural");
 }
 void sized(uint32_t w,uint32_t h,VkFormat fmt){
  if(width==w&&height==h&&format==fmt)return;clearImage();
  DXGI_FORMAT dx=fmt==VK_FORMAT_B8G8R8A8_UNORM||fmt==VK_FORMAT_B8G8R8A8_SRGB?DXGI_FORMAT_B8G8R8A8_UNORM:DXGI_FORMAT_R8G8B8A8_UNORM;
  VkFormat raw=dx==DXGI_FORMAT_B8G8R8A8_UNORM?VK_FORMAT_B8G8R8A8_UNORM:VK_FORMAT_R8G8B8A8_UNORM;
  D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;rd.Width=w;rd.Height=h;rd.DepthOrArraySize=rd.MipLevels=1;rd.Format=dx;rd.SampleDesc.Count=1;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
  Check(d12->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_SHARED,&rd,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&shared)),"Neural shared atlas");
  Handle handle;Check(d12->CreateSharedHandle(shared.Get(),nullptr,GENERIC_ALL,nullptr,&handle.h),"Neural shared handle");
  VkExternalMemoryImageCreateInfo ex{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};ex.handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
  VkImageCreateInfo ic{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ic.pNext=&ex;ic.imageType=VK_IMAGE_TYPE_2D;ic.format=raw;ic.extent={w,h,1};ic.mipLevels=ic.arrayLayers=1;ic.samples=VK_SAMPLE_COUNT_1_BIT;ic.tiling=VK_IMAGE_TILING_OPTIMAL;ic.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;ic.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
  VkCheck(vk.createImage(device,&ic,nullptr,&image),"Neural import image");VkMemoryRequirements req{};vk.getImageMemoryRequirements(device,image,&req);
  auto get=reinterpret_cast<PFN_vkGetMemoryWin32HandlePropertiesKHR>(vk.getDeviceProcAddr(device,"vkGetMemoryWin32HandlePropertiesKHR"));Require(get,"Falta VK_KHR_external_memory_win32");VkMemoryWin32HandlePropertiesKHR bits{VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR};VkCheck(get(device,VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT,handle,&bits),"Neural handle properties");
  auto mask=bits.memoryTypeBits&req.memoryTypeBits;Require(mask!=0,"Memoria compartida incompatible");uint32_t type{};while(!(mask&(1u<<type)))++type;
  VkImportMemoryWin32HandleInfoKHR im{VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR};im.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;im.handle=handle;
  VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};dedicated.image=image;im.pNext=&dedicated;
  VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ma.pNext=&im;ma.allocationSize=req.size;ma.memoryTypeIndex=type;VkCheck(vk.allocateMemory(device,&ma,nullptr,&memory),"Neural import memory");VkCheck(vk.bindImageMemory(device,image,memory,0),"Neural bind");width=w;height=h;format=fmt;
 }
 bool run(VkFormat fmt,const EyeImage(&eyes)[2],Frame frame){
  Require(fmt==VK_FORMAT_R8G8B8A8_UNORM||fmt==VK_FORMAT_R8G8B8A8_SRGB||fmt==VK_FORMAT_B8G8R8A8_UNORM||fmt==VK_FORMAT_B8G8R8A8_SRGB,"Neural necesita RGBA8/BGRA8");
  const auto w=eyes[0].rect.extent.width,h=eyes[0].rect.extent.height;Require(w>=64&&h>=64&&w<=8192&&h<=16384,"Neural eye size");
  for(int i=0;i<2;++i){auto r=eyes[i].rect;Require(eyes[i].image&&r.extent.width==w&&r.extent.height==h&&r.offset.x>=0&&r.offset.y>=0&&uint64_t(r.offset.x)+w<=eyes[i].extent.width&&uint64_t(r.offset.y)+h<=eyes[i].extent.height,"Neural eye rectangle");frame.eyes[i].left=i*w;frame.eyes[i].top=0;frame.eyes[i].right=(i+1)*w;frame.eyes[i].bottom=h;}
  frame.width=w*2;frame.height=h;if(!ValidFrame(frame)){suspendFn(context);message="Neural suspendido: pose o identidad de imagen invalida";return false;}sized(w*2,h,fmt);
  begin();barrier(image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_QUEUE_FAMILY_EXTERNAL,family);
  for(int e=0;e<2;++e){barrier(eyes[e].image,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);VkImageCopy c{};c.srcSubresource=c.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};c.srcOffset={eyes[e].rect.offset.x,eyes[e].rect.offset.y,0};c.dstOffset={int(e*w),0,0};c.extent={w,h,1};vk.cmdCopyImage(cmd,eyes[e].image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&c);barrier(eyes[e].image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);}
  barrier(image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,family,VK_QUEUE_FAMILY_EXTERNAL);submit();
  ComPtr<ID3D12Resource> output;const auto dx=shared->GetDesc().Format;
  d3dPending=true;const bool ok=process12(context,d12.Get(),q12.Get(),shared.Get(),D3D12_RESOURCE_STATE_COMMON,dx,&frame,&output);Status status;readStatus(context,&status);message=status.message;
  if(!ok){wait12();if(status.state==Fault)failed=true;return false;}
  Check(alloc->Reset(),"Neural return allocator");Check(list->Reset(alloc.Get(),nullptr),"Neural return list");Barrier(list.Get(),output.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);Barrier(list.Get(),shared.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);list->CopyResource(shared.Get(),output.Get());Barrier(list.Get(),shared.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);Barrier(list.Get(),output.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);Check(list->Close(),"Neural return close");ID3D12CommandList* lists[]{list.Get()};retained=output;d3dPending=true;q12->ExecuteCommandLists(1,lists);wait12();retained.Reset();
  begin();barrier(image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_QUEUE_FAMILY_EXTERNAL,family);
  for(int e=0;e<2;++e){barrier(eyes[e].image,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);VkImageCopy c{};c.srcSubresource=c.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};c.srcOffset={int(e*w),0,0};c.dstOffset={eyes[e].rect.offset.x,eyes[e].rect.offset.y,0};c.extent={w,h,1};vk.cmdCopyImage(cmd,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,eyes[e].image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&c);barrier(eyes[e].image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);}
  barrier(image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,family,VK_QUEUE_FAMILY_EXTERNAL);submit();message="Neural aplicado al par antes de xrReleaseSwapchainImage/xrEndFrame";return true;
 }
 bool close(){if(pending||d3dPending)return false;if(context&&destroy)destroy(context);context=nullptr;clearImage();if(fence)vk.destroyFence(device,fence,nullptr);if(pool)vk.destroyCommandPool(device,pool,nullptr);if(module)FreeLibrary(module);module=nullptr;return true;}
};
VulkanPost::VulkanPost(unsigned diagnostic):p(std::make_unique<Impl>()){p->diagnostic=diagnostic;}
VulkanPost::~VulkanPost(){if(p&&!p->close())p.release();}
bool VulkanPost::enabled(){return p->enabled();}
const char* VulkanPost::message()const{return p->message.c_str();}
bool VulkanPost::gpuReady()const{return !p->pending&&!p->d3dPending;}
void VulkanPost::suspend(){if(p->context&&p->suspendFn)p->suspendFn(p->context);}
bool VulkanPost::reset(){if(!p->close())return false;p=std::make_unique<Impl>();return true;}
bool VulkanPost::process(VkPhysicalDevice physical,VkDevice device,VkQueue queue,uint32_t family,const KharvoxVulkanDispatch& vk,VkFormat format,const EyeImage(&eyes)[2],Frame frame){
 if(!p->enabled())return false;
 try{if(!p->device)p->initialize(physical,device,queue,family,vk);Require(p->device==device&&p->queue==queue,"Dispositivo Neural cambiado");return p->run(format,eyes,frame);}
 catch(const std::exception& e){p->failed=true;p->message=e.what();return false;}
}
}
