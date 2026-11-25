#include <windows.h>
#include <tlhelp32.h>
#include "../src/dlss/VulkanDlss.h"
#include "../src/dlss/VulkanDlssRequirements.h"
#include "../src/dlss/VulkanFunctions.h"
#include <nvsdk_ngx_vk.h>
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstring>

using namespace kharvox::dlss;
static void need(bool yes,const char* text){if(!yes)throw std::runtime_error(text);}
static void ngxCheck(NVSDK_NGX_Result r,const char* text){if(NVSDK_NGX_FAILED(r))throw std::runtime_error(std::string(text)+": "+std::to_string(uint32_t(r)));}
struct Lab {
    Options o;Functions f;HMODULE loader{};VkPhysicalDeviceMemoryProperties memory{};
    PFN_vkDestroyInstance destroyInstance{};PFN_vkDestroyDevice destroyDevice{};
    PFN_vkCreateBuffer createBuffer{};PFN_vkDestroyBuffer destroyBuffer{};PFN_vkGetBufferMemoryRequirements bufferRequirements{};
    PFN_vkBindBufferMemory bindBuffer{};PFN_vkMapMemory map{};PFN_vkUnmapMemory unmap{};
    PFN_vkCmdClearColorImage clear{};PFN_vkCmdCopyImageToBuffer readback{};PFN_vkQueueWaitIdle queueIdle{};
    VkCommandPool pool{};VkCommandBuffer command{};VkFence done{};bool motion32{};
    struct Owned {Image ref;VkDeviceMemory memory{};bool ownsImage{};};
    std::array<std::array<Owned,3>,2> inputs;
    void Initialize(const wchar_t* runtime,const wchar_t* cache,bool extendedStorage=false){
        motion32=extendedStorage; o.runtimeDirectory=std::filesystem::absolute(runtime);o.cacheDirectory=std::filesystem::absolute(cache);
        o.projectId="953a53d7-aeb4-49bb-957d-4a3e5c31c6f6";o.externallySynchronized=true;o.diagnosticLogging=true;
        std::filesystem::create_directories(o.cacheDirectory);
        loader=LoadLibraryExW(L"vulkan-1.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);need(loader!=nullptr,"Vulkan loader");
        o.gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader,"vkGetInstanceProcAddr"));
        auto instanceSelection=SelectExtensions(0,nullptr,InstanceRequirements(o),InstanceExtensions(reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(o.gipa(nullptr,"vkEnumerateInstanceExtensionProperties"))));
        need(instanceSelection.Ready(),"Missing NGX instance extension");auto extensions=instanceSelection.Pointers();
        for(const auto* name:extensions)std::cout<<"Instance extension "<<name<<'\n';
        auto create=reinterpret_cast<PFN_vkCreateInstance>(o.gipa(nullptr,"vkCreateInstance"));
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_2;
        VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ici.pApplicationInfo=&app;ici.enabledExtensionCount=uint32_t(extensions.size());ici.ppEnabledExtensionNames=extensions.data();
        vkCheck(create(&ici,nullptr,&o.instance),"Create instance");
#define INSTANCE(name) auto name=reinterpret_cast<PFN_##name>(o.gipa(o.instance,#name));need(name!=nullptr,#name)
        INSTANCE(vkEnumeratePhysicalDevices);INSTANCE(vkGetPhysicalDeviceProperties);INSTANCE(vkGetPhysicalDeviceMemoryProperties);
        INSTANCE(vkGetPhysicalDeviceQueueFamilyProperties);INSTANCE(vkGetPhysicalDeviceFeatures2);INSTANCE(vkCreateDevice);
        destroyInstance=reinterpret_cast<PFN_vkDestroyInstance>(o.gipa(o.instance,"vkDestroyInstance"));
        o.gdpa=reinterpret_cast<PFN_vkGetDeviceProcAddr>(o.gipa(o.instance,"vkGetDeviceProcAddr"));
        uint32_t count{};vkCheck(vkEnumeratePhysicalDevices(o.instance,&count,nullptr),"Physical count");std::vector<VkPhysicalDevice> devices(count);vkCheck(vkEnumeratePhysicalDevices(o.instance,&count,devices.data()),"Physical devices");
        for(auto physical:devices){VkPhysicalDeviceProperties p{};vkGetPhysicalDeviceProperties(physical,&p);if(p.vendorID!=0x10de)continue;
            uint32_t n{};vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,nullptr);std::vector<VkQueueFamilyProperties> queues(n);vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,queues.data());
            for(uint32_t q=0;q<n;++q)if((queues[q].queueFlags&(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT))==(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT)){o.physical=physical;o.queueFamily=q;break;}
            if(o.physical){std::cout<<"GPU "<<p.deviceName<<" driver="<<p.driverVersion<<'\n';break;}}
        need(o.physical!=nullptr,"NVIDIA Vulkan GPU unavailable");
        const auto requiredFeatures=DeviceRequirements(o);
        auto deviceSelection=SelectExtensions(0,nullptr,requiredFeatures,DeviceExtensions(o.physical,reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(o.gipa(o.instance,"vkEnumerateDeviceExtensionProperties"))));
        need(deviceSelection.Ready(),"Missing NGX device extension");extensions=deviceSelection.Pointers();
        for(const auto* name:extensions)std::cout<<"Device extension "<<name<<'\n';
        float priority=1;VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};q.queueFamilyIndex=o.queueFamily;q.queueCount=1;q.pQueuePriorities=&priority;
        VkDeviceCreateInfo d{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};d.queueCreateInfoCount=1;d.pQueueCreateInfos=&q;d.enabledExtensionCount=uint32_t(extensions.size());d.ppEnabledExtensionNames=extensions.data();
        VkPhysicalDeviceFeatures core{};core.shaderStorageImageExtendedFormats=extendedStorage?VK_TRUE:VK_FALSE;d.pEnabledFeatures=&core;
        DeviceFeatureSelection featureSelection;if(!featureSelection.Prepare(d,requiredFeatures,o.physical,vkGetPhysicalDeviceFeatures2))throw std::runtime_error(featureSelection.Error());
        vkCheck(vkCreateDevice(o.physical,&featureSelection.Info(),nullptr,&o.device),"Create DLSS device");
        std::cout<<"DLSS device: buffer-device-address negotiated; storageImageExtendedFormats="<<extendedStorage<<'\n';
        f.Load(o.device,o.gdpa);vkGetPhysicalDeviceMemoryProperties(o.physical,&memory);
#define DEVICE(member,name) member=reinterpret_cast<PFN_##name>(o.gdpa(o.device,#name));need(member!=nullptr,#name)
        DEVICE(destroyDevice,vkDestroyDevice);DEVICE(createBuffer,vkCreateBuffer);DEVICE(destroyBuffer,vkDestroyBuffer);DEVICE(bufferRequirements,vkGetBufferMemoryRequirements);
        DEVICE(bindBuffer,vkBindBufferMemory);DEVICE(map,vkMapMemory);DEVICE(unmap,vkUnmapMemory);DEVICE(clear,vkCmdClearColorImage);DEVICE(readback,vkCmdCopyImageToBuffer);DEVICE(queueIdle,vkQueueWaitIdle);
        auto getQueue=reinterpret_cast<PFN_vkGetDeviceQueue>(o.gdpa(o.device,"vkGetDeviceQueue"));getQueue(o.device,o.queueFamily,0,&o.queue);
        VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};cp.queueFamilyIndex=o.queueFamily;vkCheck(f.vkCreateCommandPool(o.device,&cp,nullptr,&pool),"Lab pool");
        VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=pool;ca.commandBufferCount=1;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;vkCheck(f.vkAllocateCommandBuffers(o.device,&ca,&command),"Lab command");
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};vkCheck(f.vkCreateFence(o.device,&fi,nullptr,&done),"Lab fence");
#undef INSTANCE
#undef DEVICE
    }
    uint32_t MemoryType(uint32_t bits,VkMemoryPropertyFlags flags){for(uint32_t n=0;n<memory.memoryTypeCount;++n)if((bits&(1u<<n))&&(memory.memoryTypes[n].propertyFlags&flags)==flags)return n;throw std::runtime_error("Memory type");}
    void Begin(){vkCheck(f.vkResetCommandPool(o.device,pool,0),"Reset lab");VkCommandBufferBeginInfo b{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};vkCheck(f.vkBeginCommandBuffer(command,&b),"Begin lab");}
    void Finish(){vkCheck(f.vkEndCommandBuffer(command),"End lab");vkCheck(f.vkResetFences(o.device,1,&done),"Reset lab fence");VkSubmitInfo s{VK_STRUCTURE_TYPE_SUBMIT_INFO};s.commandBufferCount=1;s.pCommandBuffers=&command;vkCheck(f.vkQueueSubmit(o.queue,1,&s,done),"Submit lab");vkCheck(f.vkWaitForFences(o.device,1,&done,VK_TRUE,10000000000ull),"Wait lab");}
    void Barrier(const Image& i,VkImageLayout before,VkImageLayout after){VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=i.image;b.subresourceRange=i.range;b.oldLayout=before;b.newLayout=after;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.srcAccessMask=before==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT;b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;f.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);}
    void ClearInputs(){for(auto& eye:inputs)for(auto& image:eye)if(image.ref.view){f.vkDestroyImageView(o.device,image.ref.view,nullptr);image.ref.view=VK_NULL_HANDLE;}for(auto& eye:inputs)for(auto& image:eye){if(image.ownsImage&&image.ref.image)f.vkDestroyImage(o.device,image.ref.image,nullptr);if(image.memory)f.vkFreeMemory(o.device,image.memory,nullptr);image={};}}
    PairInput Inputs(uint32_t width,uint32_t height,uint64_t epoch,Mode mode,bool arrayLayers,bool submit=true){
        ClearInputs();Begin();PairInput p;p.mode=mode;p.temporalContractValidated=true;p.hdr=true;p.reset=true;
        for(unsigned e=0;e<2;++e){for(unsigned k=0;k<3;++k){auto& owned=inputs[e][k];auto& i=owned.ref;i.device=o.device;i.extent={width,height};i.identity={1,epoch,e};i.format=k==0?VK_FORMAT_R16G16B16A16_SFLOAT:k==1?VK_FORMAT_R32_SFLOAT:(motion32?VK_FORMAT_R32G32_SFLOAT:VK_FORMAT_R16G16_SFLOAT);i.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                if(arrayLayers&&e==1){i.image=inputs[0][k].ref.image;i.range.baseArrayLayer=1;}
                else {owned.ownsImage=true;VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.imageType=VK_IMAGE_TYPE_2D;ci.extent={width,height,1};ci.format=i.format;ci.mipLevels=1;ci.arrayLayers=arrayLayers?2:1;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.usage=i.usage;
                vkCheck(f.vkCreateImage(o.device,&ci,nullptr,&i.image),"Input image");VkMemoryRequirements req{};f.vkGetImageMemoryRequirements(o.device,i.image,&req);VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=MemoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);vkCheck(f.vkAllocateMemory(o.device,&ai,nullptr,&owned.memory),"Input memory");vkCheck(f.vkBindImageMemory(o.device,i.image,owned.memory,0),"Input bind");
                }
                VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=i.image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=i.format;vi.subresourceRange=i.range;vkCheck(f.vkCreateImageView(o.device,&vi,nullptr,&i.view),"Input view");
                Barrier(i,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);VkClearColorValue c{};
                if(k==0){c.float32[e==0?0:2]=0.75f;c.float32[1]=0.125f;c.float32[3]=1;}else if(k==1)c.float32[0]=0.5f;
                clear(command,i.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&c,1,&i.range);Barrier(i,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,i.layout);
            }
            p.eyes[e].color=inputs[e][0].ref;p.eyes[e].depth=inputs[e][1].ref;p.eyes[e].motion=inputs[e][2].ref;
            p.output[e]={mode==Mode::Dlaa?width:width*2,mode==Mode::Dlaa?height:height*2};
        }if(submit)Finish();return p;
    }
    static float Half(uint16_t h){const float sign=(h&0x8000)?-1.f:1.f;const int exp=(h>>10)&31;const int mantissa=h&1023;if(exp==31)return mantissa?NAN:sign*INFINITY;return sign*(exp?std::ldexp(1.f+float(mantissa)/1024,exp-15):std::ldexp(float(mantissa),-24));}
    void CheckPixels(const Output& out){
        need(out.eyes[0].image!=out.eyes[1].image,"Output eye images alias");
        for(unsigned e=0;e<2;++e){const auto& i=out.eyes[e];const auto bytes=VkDeviceSize(i.extent.width)*i.extent.height*8;
            VkBufferCreateInfo b{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};b.size=bytes;b.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;VkBuffer buffer{};vkCheck(createBuffer(o.device,&b,nullptr,&buffer),"Readback buffer");
            VkMemoryRequirements req{};bufferRequirements(o.device,buffer,&req);VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=MemoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);VkDeviceMemory allocation{};vkCheck(f.vkAllocateMemory(o.device,&ai,nullptr,&allocation),"Readback memory");vkCheck(bindBuffer(o.device,buffer,allocation,0),"Readback bind");
            Begin();Barrier(i,i.layout,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={i.extent.width,i.extent.height,1};readback(command,i.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);Barrier(i,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,i.layout);
            VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};host.buffer=buffer;host.size=VK_WHOLE_SIZE;host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;f.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);Finish();
            void* mapped{};vkCheck(map(o.device,allocation,0,bytes,0,&mapped),"Readback map");const auto* pixel=static_cast<uint16_t*>(mapped);double sums[3]{};
            for(size_t n=0;n<size_t(i.extent.width)*i.extent.height;++n)for(unsigned c=0;c<3;++c){auto value=Half(pixel[n*4+c]);need(std::isfinite(value),"Non-finite DLSS output");sums[c]+=value;}
            unmap(o.device,allocation);destroyBuffer(o.device,buffer,nullptr);f.vkFreeMemory(o.device,allocation,nullptr);
            need(sums[e==0?0:2]>sums[e==0?2:0]+0.2*i.extent.width*i.extent.height,"Eye color lost/crossed or output empty");
        }
    }
    ~Lab(){if(o.device){if(queueIdle)queueIdle(o.queue);ClearInputs();if(done)f.vkDestroyFence(o.device,done,nullptr);if(pool)f.vkDestroyCommandPool(o.device,pool,nullptr);if(destroyDevice)destroyDevice(o.device,nullptr);}if(o.instance&&destroyInstance)destroyInstance(o.instance,nullptr);if(loader)FreeLibrary(loader);}
};
static Output wait(VulkanDlss& provider){Output out;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);for(;;){const auto status=provider.Poll(out);if(status==Status::Ready)return out;if(status!=Status::Pending)throw std::runtime_error(provider.Error());need(std::chrono::steady_clock::now()<deadline,"DLSS completion timed out");std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
#ifndef KHARVOX_DLSS_RECORD_TEST
int wmain(int argc,wchar_t** argv){try{
    need(argc==4,"Expected runtime directory, cache directory and actual module path");Lab lab;lab.Initialize(argv[1],argv[2]);VulkanDlss provider(lab.o);
    bool runtimeChecked=false;
    uint64_t epoch=0;for(const bool arrays:{false,true})for(const auto mode:{Mode::Dlaa,Mode::Quality})for(const auto size:{128u,160u,128u}){
        auto pair=lab.Inputs(size,size+32,++epoch,mode,arrays);
        auto invalid=pair;invalid.temporalContractValidated=false;need(provider.Submit(invalid)==Status::Rejected,"Unknown temporal contract accepted");
        invalid=pair;invalid.eyes[1].motion.identity.eye=0;need(provider.Submit(invalid)==Status::Rejected,"Crossed eye accepted");
        invalid=pair;invalid.eyes[1].depth.identity.frame=2;need(provider.Submit(invalid)==Status::Rejected,"Crossed frame accepted");
        invalid=pair;invalid.eyes[0].motion.range.layerCount=2;need(provider.Submit(invalid)==Status::Rejected,"Two-layer NGX view accepted");
        invalid=pair;invalid.eyes[0].jitterX=NAN;need(provider.Submit(invalid)==Status::Rejected,"Unknown jitter accepted");
        invalid=pair;invalid.eyes[0].motion.format=VK_FORMAT_R32_SFLOAT;need(provider.Submit(invalid)==Status::Rejected,"Packed velocity accepted");
        for(uint64_t frame=1;frame<=4;++frame){for(auto& eye:pair.eyes)for(auto* i:{&eye.color,&eye.depth,&eye.motion})i->identity.frame=frame;pair.reset=frame==1;
            const auto status=provider.Submit(pair);if(status!=Status::Pending)throw std::runtime_error(provider.Error());
            if(!runtimeChecked){const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());need(snapshot!=INVALID_HANDLE_VALUE,"Module enumeration unavailable");MODULEENTRY32W entry{};entry.dwSize=sizeof(entry);
                if(Module32FirstW(snapshot,&entry))do{std::error_code ec;if(std::filesystem::equivalent(std::filesystem::path(entry.szExePath),std::filesystem::path(argv[3]),ec)&&!ec){std::wcout<<L"Verified actual runtime "<<entry.szExePath<<L'\n';runtimeChecked=true;break;}}while(Module32NextW(snapshot,&entry));CloseHandle(snapshot);
                need(runtimeChecked,"Expected actual DLSS provider was not loaded; inspect NGX diagnostics before accepting results");}
            need(provider.Submit(pair)==Status::Busy,"In-flight overwrite accepted");const auto out=wait(provider);
            need(out.eyes[0].identity.frame==frame&&out.eyes[1].identity.eye==1,"Output identity lost");
            need(provider.Submit(pair)==Status::Busy,"Output reused before last reader");need(!provider.Release(VK_NULL_HANDLE),"Missing reader completion accepted");
            lab.CheckPixels(out);
            VkFence held{};
            if(frame==1){VkFenceCreateInfo ci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};vkCheck(lab.f.vkCreateFence(lab.o.device,&ci,nullptr,&held),"Held consumer fence");need(provider.Release(held),"Output hold failed");need(provider.Submit(pair)==Status::Busy,"Unfinished consumer reused output");VkSubmitInfo signal{VK_STRUCTURE_TYPE_SUBMIT_INFO};vkCheck(lab.f.vkQueueSubmit(lab.o.queue,1,&signal,held),"Consumer signal");vkCheck(lab.f.vkWaitForFences(lab.o.device,1,&held,VK_TRUE,10000000000ull),"Consumer completion");}
            else need(provider.Release(lab.done),"Output release failed");
            need(provider.Submit(pair)==Status::Rejected,"Duplicate temporal frame accepted");
            if(held)lab.f.vkDestroyFence(lab.o.device,held,nullptr);
        }
        std::cout<<"PASS "<<(mode==Mode::Dlaa?"DLAA":"SR")<<" two eyes "<<size<<"x"<<size+32<<" epoch="<<epoch<<" arrayLayers="<<arrays<<"\n";
    }
    PairInput off;need(provider.Submit(off)==Status::Disabled,"Disabled mode recorded work");need(provider.Evaluations()==96,"Unexpected successful evaluation count");
    std::cout<<"PASS 96 real NGX Vulkan evaluations, private eye histories, two array layers, resize/epoch, identity rejection and consumer retirement. Synthetic inputs; no game/VR/FPS claim.\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
#endif
