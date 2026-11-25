#include "VulkanDlss.h"
#include "VulkanDlssRequirements.h"
#include "VulkanFunctions.h"
#include <nvsdk_ngx_helpers_vk.h>
#include <cmath>
#include <utility>
#include <cstdio>
#include <map>
#include <mutex>
#include "../neural/SnippetCompatibility.h"

namespace kharvox::dlss {
namespace {
void need(bool condition,const char* text){if(!condition)throw std::runtime_error(text);}
void ngx(NVSDK_NGX_Result result,const char* text){if(NVSDK_NGX_FAILED(result))throw std::runtime_error(std::string(text)+": "+std::to_string(uint32_t(result)));}
bool equal(VkExtent2D a,VkExtent2D b){return a.width==b.width&&a.height==b.height;}
bool finite(float value){return std::isfinite(value);}
// Central/peripheral/SR/Neural processors share one NGX device lifetime. A
// branch retiring must never shut down another branch's live feature handles.
struct NgxSession {
    VkDevice device{};std::filesystem::path runtime,cache;std::string project;
    ~NgxSession(){if(device)NVSDK_NGX_VULKAN_Shutdown1(device);}
};
std::mutex sessionsMutex;
std::map<VkDevice,std::weak_ptr<NgxSession>> sessions;
struct NeuralSession {
    using Init=NVSDK_NGX_Result(__cdecl*)(unsigned long long,const wchar_t*,VkInstance,VkPhysicalDevice,VkDevice,PFN_vkGetInstanceProcAddr,PFN_vkGetDeviceProcAddr,int,const NVSDK_NGX_Parameter*);
    using Create=NVSDK_NGX_Result(__cdecl*)(VkDevice,VkCommandBuffer,int,const NVSDK_NGX_Parameter*,NVSDK_NGX_Handle**);
    using Evaluate=NVSDK_NGX_Result(__cdecl*)(VkCommandBuffer,const NVSDK_NGX_Handle*,const NVSDK_NGX_Parameter*,void*);
    using Release=NVSDK_NGX_Result(__cdecl*)(NVSDK_NGX_Handle*);
    using Shutdown=NVSDK_NGX_Result(__cdecl*)(VkDevice);
    HMODULE module{};VkDevice device{};Create create{};Evaluate evaluate{};Release release{};Shutdown shutdown{};
    std::shared_ptr<NgxSession> ngxSession;
    ~NeuralSession(){if(device&&shutdown)shutdown(device);if(module)FreeLibrary(module);}
};
std::map<std::pair<VkDevice,std::filesystem::path>,std::weak_ptr<NeuralSession>> neuralSessions;
}
struct VulkanDlss::State {
    Options options;Functions f;VkPhysicalDeviceMemoryProperties memory{};
    VkCommandPool pool{};VkCommandBuffer command{};VkFence fence{},consumer{};
    NVSDK_NGX_Parameter* capabilities{};std::shared_ptr<NgxSession> session;std::shared_ptr<NeuralSession> neural;
    bool pending{},ready{},pairFailed{},fatal{};
    bool recorded{},clearAfterConsumer{};uint64_t recordedFrame{},recordedEpoch{};
    uint64_t ticket{},evaluations{},resetEvaluations{},lastFrame{},lastEpoch{};std::string error;
    struct Eye {
        Image output;VkDeviceMemory allocation{};
        NVSDK_NGX_Handle* handle{};NVSDK_NGX_Parameter* parameters{};
        VkExtent2D input{};Mode mode{Mode::Off};int flags{};unsigned model{};
    };
    std::array<Eye,2> eyes;
    explicit State(Options in):options(std::move(in)){}
    struct QueueLock {
        Options& o;explicit QueueLock(Options& v):o(v){if(o.lockQueue)o.lockQueue();}
        ~QueueLock(){if(o.unlockQueue)o.unlockQueue();}
    };
    void Initialize(){
        need(options.instance&&options.physical&&options.device&&options.queue&&options.gipa&&options.gdpa,"Missing Vulkan device context");
        need((options.lockQueue&&options.unlockQueue)||(!options.lockQueue&&!options.unlockQueue&&options.externallySynchronized),"Missing shared Vulkan queue synchronization");
        need(!options.projectId.empty()&&options.runtimeDirectory.is_absolute()&&options.cacheDirectory.is_absolute(),"Explicit project/runtime/cache identity required");
        need(std::filesystem::exists(options.runtimeDirectory/L"nvngx_dlss.dll"),"Requested DLSS runtime missing");
        std::filesystem::create_directories(options.cacheDirectory);
        f.Load(options.device,options.gdpa);
        auto getMemory=reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(options.gipa(options.instance,"vkGetPhysicalDeviceMemoryProperties"));
        need(getMemory!=nullptr,"Physical memory query unavailable");getMemory(options.physical,&memory);
        const auto runtime=options.runtimeDirectory.wstring();const wchar_t* paths[]{runtime.c_str()};
        NVSDK_NGX_FeatureCommonInfo common{};common.PathListInfo.Path=paths;common.PathListInfo.Length=1;
        // NGX can replace the requested DLL with a driver-managed cache .bin.
        // Preserve diagnostics so the adapter can attest the selected provider.
        if(options.diagnosticLogging){common.LoggingInfo.LoggingCallback=[](const char* message,NVSDK_NGX_Logging_Level,NVSDK_NGX_Feature){std::fprintf(stderr,"[NGX] %s\n",message);};
            common.LoggingInfo.MinimumLoggingLevel=NVSDK_NGX_LOGGING_LEVEL_VERBOSE;common.LoggingInfo.DisableOtherLoggingSinks=true;}
        need(!options.ngxInstance||(options.ngxPhysical&&options.ngxGipa),"Incomplete public NGX context");
        {
            std::lock_guard<std::mutex> lock(sessionsMutex);session=sessions[options.device].lock();
            if(session)need(session->runtime==options.runtimeDirectory&&session->cache==options.cacheDirectory&&session->project==options.projectId,"Conflicting NGX runtime identity on shared device");
            else{
                auto created=std::make_shared<NgxSession>();created->runtime=options.runtimeDirectory;created->cache=options.cacheDirectory;created->project=options.projectId;
                ngx(NVSDK_NGX_VULKAN_Init_with_ProjectID(options.projectId.c_str(),NVSDK_NGX_ENGINE_TYPE_CUSTOM,EngineVersion,options.cacheDirectory.c_str(),options.ngxInstance?options.ngxInstance:options.instance,options.ngxInstance?options.ngxPhysical:options.physical,options.device,options.ngxInstance?options.ngxGipa:options.gipa,options.gdpa,&common),"NGX Vulkan init");
                created->device=options.device;session=created;sessions[options.device]=session;
            }
        }
        ngx(NVSDK_NGX_VULKAN_GetCapabilityParameters(&capabilities),"NGX capabilities");int supported{};
        ngx(capabilities->Get(NVSDK_NGX_Parameter_SuperSampling_Available,&supported),"NGX SR query");need(supported!=0,"Vulkan DLSS unavailable");
        VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};cp.queueFamilyIndex=options.queueFamily;
        vkCheck(f.vkCreateCommandPool(options.device,&cp,nullptr,&pool),"DLSS command pool");
        VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=pool;ca.commandBufferCount=1;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        vkCheck(f.vkAllocateCommandBuffers(options.device,&ca,&command),"DLSS command buffer");
        VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};vkCheck(f.vkCreateFence(options.device,&fc,nullptr,&fence),"DLSS fence");
    }
    void Clear(Eye& eye){
        if(eye.handle){if(eye.mode==Mode::Neural&&neural)neural->release(eye.handle);else NVSDK_NGX_VULKAN_ReleaseFeature(eye.handle);}
        if(eye.parameters)NVSDK_NGX_VULKAN_DestroyParameters(eye.parameters);
        if(eye.output.view)f.vkDestroyImageView(options.device,eye.output.view,nullptr);
        if(eye.output.image)f.vkDestroyImage(options.device,eye.output.image,nullptr);
        if(eye.allocation)f.vkFreeMemory(options.device,eye.allocation,nullptr);eye={};
    }
    ~State(){
        if(fatal&&f.vkDeviceWaitIdle){QueueLock lock(options);f.vkDeviceWaitIdle(options.device);}
        if(pending)f.vkWaitForFences(options.device,1,&fence,VK_TRUE,UINT64_MAX);
        if(consumer)f.vkWaitForFences(options.device,1,&consumer,VK_TRUE,UINT64_MAX);
        for(auto& eye:eyes)Clear(eye);
        if(fence)f.vkDestroyFence(options.device,fence,nullptr);
        if(pool)f.vkDestroyCommandPool(options.device,pool,nullptr);
        if(capabilities)NVSDK_NGX_VULKAN_DestroyParameters(capabilities);
        neural.reset();session.reset();
    }
    void Validate(const PairInput& pair){
        need(pair.temporalContractValidated,"Temporal inputs have not been qualified by the adapter");
        need(pair.mode>=Mode::Dlaa&&pair.mode<=Mode::Neural,"Unsupported reconstruction mode");
        need(pair.preset==0||(pair.preset>=10&&pair.preset<=13),"Unsupported SR preset");
        if(pair.mode==Mode::Neural){need(!pair.hdr&&pair.neuralModel<=2&&!pair.motionJittered,"Neural requires qualified LDR, unjittered motion and Auto/A/B model");
            need(pair.appearance.style<=2,"Neural style range");for(float v:{pair.appearance.intensity,pair.appearance.structure,pair.appearance.tone})need(std::isfinite(v)&&v>=0&&v<=2,"Neural appearance range");need(std::isfinite(pair.appearance.skin)&&pair.appearance.skin>=-1&&pair.appearance.skin<=2,"Neural skin range");}
        const auto id=pair.eyes[0].color.identity;
        need(id.frame&&id.epoch,"Missing frame/epoch identity");
        need(id.epoch>lastEpoch||(id.epoch==lastEpoch&&id.frame>lastFrame),"Stale frame or epoch");
        for(unsigned e=0;e<2;++e){const auto& eye=pair.eyes[e];const auto extent=eye.color.extent;
            need(extent.width>=32&&extent.height>=32&&extent.width<=16384&&extent.height<=16384,"Invalid input size");
            need(pair.output[e].width>=extent.width&&pair.output[e].height>=extent.height&&pair.output[e].width<=16384&&pair.output[e].height<=16384,"Invalid output size");
            const bool native=pair.mode==Mode::Dlaa||pair.mode==Mode::Neural;
            need(!native||equal(extent,pair.output[e]),"DLAA/Neural must preserve native size");
            need(native||!equal(extent,pair.output[e]),"SR must have a separate output size");
            need(finite(eye.jitterX)&&finite(eye.jitterY)&&std::abs(eye.jitterX)<=1&&std::abs(eye.jitterY)<=1&&finite(eye.mvScaleX)&&finite(eye.mvScaleY)&&eye.mvScaleX!=0&&eye.mvScaleY!=0,"Invalid jitter/motion scale");
            need(finite(eye.preExposure)&&eye.preExposure>0&&finite(eye.exposureScale)&&eye.exposureScale>0&&finite(eye.frameTimeMs)&&eye.frameTimeMs>0,"Invalid exposure/frame time");
            for(const auto* image:{&eye.color,&eye.depth,&eye.motion}){
                need(image->device==options.device&&image->image&&image->view,"Input device/image/view mismatch");
                need(image->identity.eye==e&&image->identity.frame==id.frame&&image->identity.epoch==id.epoch,"Crossed eye/frame/epoch");
                need(equal(image->extent,extent)&&image->range.levelCount==1&&image->range.layerCount==1&&image->samples==VK_SAMPLE_COUNT_1_BIT,"Input extent/subresource/sample mismatch");
                need((image->usage&VK_IMAGE_USAGE_SAMPLED_BIT)!=0,"Input lacks sampled usage");
                need(image->layout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL||image->layout==VK_IMAGE_LAYOUT_GENERAL||image->layout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,"Unknown input layout");
                for(const auto& owned:eyes)need(image->image!=owned.output.image,"Output cannot reenter as input");
            }
            need(eye.color.format==VK_FORMAT_R16G16B16A16_SFLOAT||eye.color.format==VK_FORMAT_B10G11R11_UFLOAT_PACK32,"Color format requires explicit conversion");
            need(eye.motion.format==VK_FORMAT_R16G16_SFLOAT||eye.motion.format==VK_FORMAT_R32G32_SFLOAT,"Motion format requires explicit conversion");
            need(eye.depth.format==VK_FORMAT_R32_SFLOAT||eye.depth.format==VK_FORMAT_D32_SFLOAT,"Depth format requires explicit conversion");
            need(eye.color.range.aspectMask==VK_IMAGE_ASPECT_COLOR_BIT&&eye.motion.range.aspectMask==VK_IMAGE_ASPECT_COLOR_BIT&&
                eye.depth.range.aspectMask==(eye.depth.format==VK_FORMAT_D32_SFLOAT?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT),"Invalid aspect");
        }
        for(unsigned k=0;k<3;++k){const Image* left[]{&pair.eyes[0].color,&pair.eyes[0].depth,&pair.eyes[0].motion};const Image* right[]{&pair.eyes[1].color,&pair.eyes[1].depth,&pair.eyes[1].motion};
            need(left[k]->image!=right[k]->image||left[k]->range.baseArrayLayer!=right[k]->range.baseArrayLayer,"Both eyes alias one layer");}
    }
    Status Availability(){
        if(fatal)return Status::Failed;
        if(pending||ready||recorded)return Status::Busy;
        if(consumer){const auto r=f.vkGetFenceStatus(options.device,consumer);
            if(r==VK_NOT_READY)return Status::Busy;
            if(r!=VK_SUCCESS){fatal=true;error="Consumer completion failed";return Status::Failed;}
            consumer=VK_NULL_HANDLE;
        }
        if(clearAfterConsumer){for(auto& eye:eyes)Clear(eye);clearAfterConsumer=false;}
        return Status::Ready;
    }
    void EnsureNeural(){
        if(neural)return;
        need(options.neuralLibrary.is_absolute()&&std::filesystem::is_regular_file(options.neuralLibrary),"Explicit private Neural DLL required");
        std::lock_guard<std::mutex> lock(sessionsMutex);const auto key=std::make_pair(options.device,options.neuralLibrary);
        neural=neuralSessions[key].lock();if(neural)return;
        auto created=std::make_shared<NeuralSession>();created->ngxSession=session;
        created->module=LoadLibraryExW(options.neuralLibrary.c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);need(created->module!=nullptr,"Load private Neural DLL failed");
        kharvox::neural::compatibility::HookSnippetCallerCheck(created->module);
        const auto init=reinterpret_cast<NeuralSession::Init>(GetProcAddress(created->module,"NVSDK_NGX_VULKAN_Init_Ext2"));
        created->create=reinterpret_cast<NeuralSession::Create>(GetProcAddress(created->module,"NVSDK_NGX_VULKAN_CreateFeature1"));
        created->evaluate=reinterpret_cast<NeuralSession::Evaluate>(GetProcAddress(created->module,"NVSDK_NGX_VULKAN_EvaluateFeature"));
        created->release=reinterpret_cast<NeuralSession::Release>(GetProcAddress(created->module,"NVSDK_NGX_VULKAN_ReleaseFeature"));
        created->shutdown=reinterpret_cast<NeuralSession::Shutdown>(GetProcAddress(created->module,"NVSDK_NGX_VULKAN_Shutdown1"));
        need(init&&created->create&&created->evaluate&&created->release&&created->shutdown,"Neural Vulkan ABI exports missing");
        ngx(init(0x24480451ull,options.cacheDirectory.c_str(),options.ngxInstance?options.ngxInstance:options.instance,options.ngxInstance?options.ngxPhysical:options.physical,options.device,options.ngxInstance?options.ngxGipa:options.gipa,options.gdpa,0x15,capabilities),"Private Neural Vulkan init");
        created->device=options.device;neural=created;neuralSessions[key]=neural;
    }
    void Prepare(Eye& eye,VkExtent2D input,VkExtent2D output,Mode mode,int flags,unsigned model,VkCommandBuffer target){
        if(eye.handle&&equal(eye.input,input)&&equal(eye.output.extent,output)&&eye.mode==mode&&eye.flags==flags&&eye.model==model)return;
        Clear(eye);eye.input=input;eye.mode=mode;eye.flags=flags;eye.model=model;
        eye.output.device=options.device;eye.output.extent=output;eye.output.format=VK_FORMAT_R16G16B16A16_SFLOAT;
        eye.output.usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        eye.output.layout=VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.imageType=VK_IMAGE_TYPE_2D;ci.format=eye.output.format;ci.extent={output.width,output.height,1};ci.mipLevels=ci.arrayLayers=1;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.usage=eye.output.usage;
        vkCheck(f.vkCreateImage(options.device,&ci,nullptr,&eye.output.image),"Private DLSS output");
        VkMemoryRequirements req{};f.vkGetImageMemoryRequirements(options.device,eye.output.image,&req);uint32_t type=UINT32_MAX;
        for(uint32_t i=0;i<memory.memoryTypeCount;++i)if((req.memoryTypeBits&(1u<<i))&&(memory.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)){type=i;break;}
        need(type!=UINT32_MAX,"No device-local memory for DLSS");
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=type;
        vkCheck(f.vkAllocateMemory(options.device,&ai,nullptr,&eye.allocation),"Private DLSS memory");vkCheck(f.vkBindImageMemory(options.device,eye.output.image,eye.allocation,0),"Private DLSS bind");
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=eye.output.image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=eye.output.format;vi.subresourceRange=eye.output.range;
        vkCheck(f.vkCreateImageView(options.device,&vi,nullptr,&eye.output.view),"Private DLSS view");
        ngx(NVSDK_NGX_VULKAN_AllocateParameters(&eye.parameters),"NGX eye parameters");
        if(mode==Mode::Neural){
            EnsureNeural();auto* p=eye.parameters;p->Set("DLSSNR.Enabled",1u);p->Set("DLSSNR.Width",input.width);p->Set("DLSSNR.Height",input.height);
            p->Set("DLSSNR.Hint.Render.Preset",model);p->Set("DLSSNR.UICorrection",0u);p->Set("CreationNodeMask",1u);p->Set("VisibilityNodeMask",1u);
            ngx(neural->create(options.device,target,18,p,&eye.handle),"Neural eye feature");return;
        }
        // The same explicit hint applies to DLAA and all SR quality levels.
        // These are Super Resolution hints, not Ray Reconstruction presets.
        for(const char* key:{NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA,
            NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality,
            NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced,
            NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance,
            NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance,
            NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality}){
            eye.parameters->Set(key,model);
            unsigned int selected{};
            ngx(eye.parameters->Get(key,&selected),"Read back DLSS preset");
            need(selected==model,"DLSS preset was not retained");
        }
        NVSDK_NGX_DLSS_Create_Params create{};create.Feature.InWidth=input.width;create.Feature.InHeight=input.height;
        create.Feature.InTargetWidth=output.width;create.Feature.InTargetHeight=output.height;
        switch(mode){
        case Mode::Dlaa:create.Feature.InPerfQualityValue=NVSDK_NGX_PerfQuality_Value_DLAA;break;
        case Mode::Quality:create.Feature.InPerfQualityValue=NVSDK_NGX_PerfQuality_Value_MaxQuality;break;
        case Mode::Balanced:create.Feature.InPerfQualityValue=NVSDK_NGX_PerfQuality_Value_Balanced;break;
        case Mode::Performance:create.Feature.InPerfQualityValue=NVSDK_NGX_PerfQuality_Value_MaxPerf;break;
        case Mode::UltraPerformance:create.Feature.InPerfQualityValue=NVSDK_NGX_PerfQuality_Value_UltraPerformance;break;
        default:throw std::runtime_error("Unsupported feature quality");
        }
        create.InFeatureCreateFlags=flags;
        ngx(NGX_VULKAN_CREATE_DLSS_EXT1(options.device,target,1,1,&eye.handle,eye.parameters,&create),"NGX eye feature");
    }
    void Barrier(VkCommandBuffer target,const Image& image,VkImageLayout before,VkImageLayout after){
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=image.image;b.subresourceRange=image.range;
        b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.oldLayout=before;b.newLayout=after;
        b.srcAccessMask=before==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
        b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
        f.vkCmdPipelineBarrier(target,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);
    }
    static NVSDK_NGX_Resource_VK Resource(const Image& i,bool writable){return NVSDK_NGX_Create_ImageView_Resource_VK(i.view,i.image,i.range,i.format,i.extent.width,i.extent.height,writable);}
    void Evaluate(const PairInput& pair,VkCommandBuffer target){
        const int flags=NVSDK_NGX_DLSS_Feature_Flags_MVLowRes|(pair.hdr?NVSDK_NGX_DLSS_Feature_Flags_AutoExposure:0)|
            (pair.hdr?NVSDK_NGX_DLSS_Feature_Flags_IsHDR:0)|(pair.depthInverted?NVSDK_NGX_DLSS_Feature_Flags_DepthInverted:0)|(pair.motionJittered?NVSDK_NGX_DLSS_Feature_Flags_MVJittered:0);
        pairFailed=false;error.clear();
        for(unsigned e=0;e<2;++e){auto& eye=eyes[e];const auto& input=pair.eyes[e];
            Prepare(eye,input.color.extent,pair.output[e],pair.mode,flags,pair.mode==Mode::Neural?pair.neuralModel:pair.preset,target);
            auto color=Resource(input.color,false),depth=Resource(input.depth,false),motion=Resource(input.motion,false),output=Resource(eye.output,true);
            for(const auto* i:{&input.color,&input.depth,&input.motion})Barrier(target,*i,i->layout,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            const bool fresh=eye.output.layout==VK_IMAGE_LAYOUT_UNDEFINED;
            Barrier(target,eye.output,eye.output.layout,VK_IMAGE_LAYOUT_GENERAL);eye.output.layout=VK_IMAGE_LAYOUT_GENERAL;
            NVSDK_NGX_VK_DLSS_Eval_Params eval{};eval.Feature.pInColor=&color;eval.Feature.pInOutput=&output;
            eval.pInDepth=&depth;eval.pInMotionVectors=&motion;eval.InJitterOffsetX=input.jitterX;eval.InJitterOffsetY=input.jitterY;
            eval.InMVScaleX=input.mvScaleX;eval.InMVScaleY=input.mvScaleY;eval.InRenderSubrectDimensions={input.color.extent.width,input.color.extent.height};
            eval.InPreExposure=input.preExposure;eval.InExposureScale=input.exposureScale;eval.InFrameTimeDeltaInMsec=input.frameTimeMs;
            eval.InReset=pair.reset||fresh||input.color.identity.epoch!=lastEpoch||input.color.identity.frame!=lastFrame+1;
            NVSDK_NGX_Result result;
            if(pair.mode==Mode::Neural){
                auto* p=eye.parameters;p->Set("DLSSNR.Hint.Render.Preset",pair.neuralModel);p->Set("DLSSNR.Reset",eval.InReset?1u:0u);
                p->Set("DLSSNR.Style",int(pair.appearance.style));p->Set("DLSSNR.Intensity",pair.appearance.intensity);p->Set("DLSSNR.LocalStructureStrength",pair.appearance.structure);p->Set("DLSSNR.LocalToneStrength",pair.appearance.tone);
                if(pair.appearance.skin>=0)p->Set("DLSSNR.SkinStructureStrength",pair.appearance.skin);p->Set("DLSSNR.UseAutoMask",pair.appearance.autoMask?1u:0u);
                unsigned style{},mask{};float intensity{},structure{},tone{},skin{};
                ngx(p->Get("DLSSNR.Style",&style),"Neural style readback");ngx(p->Get("DLSSNR.UseAutoMask",&mask),"Neural mask readback");
                ngx(p->Get("DLSSNR.Intensity",&intensity),"Neural intensity readback");ngx(p->Get("DLSSNR.LocalStructureStrength",&structure),"Neural structure readback");ngx(p->Get("DLSSNR.LocalToneStrength",&tone),"Neural tone readback");
                need(style==pair.appearance.style&&mask==unsigned(pair.appearance.autoMask)&&intensity==pair.appearance.intensity&&structure==pair.appearance.structure&&tone==pair.appearance.tone,"Neural appearance parameters not retained");
                if(pair.appearance.skin>=0){ngx(p->Get("DLSSNR.SkinStructureStrength",&skin),"Neural skin readback");need(skin==pair.appearance.skin,"Neural skin parameter not retained");}
                p->Set("DLSSNR.DepthInverted",pair.depthInverted?1u:0u);p->Set("DLSSNR.MVecScaleX",input.mvScaleX);p->Set("DLSSNR.MVecScaleY",input.mvScaleY);
                p->Set("Jitter.Offset.X",input.jitterX);p->Set("Jitter.Offset.Y",input.jitterY);p->Set("FrameTimeDeltaInMsec",input.frameTimeMs);
                NVSDK_NGX_Resource_VK* resources[]{&color,&depth,&motion,&output};const char* names[]{"Color","Depth","MVec","Output"};
                for(unsigned i=0;i<4;++i){const std::string key=std::string("DLSSNR.")+names[i];p->Set(key.c_str(),static_cast<void*>(resources[i]));p->Set((key+"SubrectBaseX").c_str(),0u);p->Set((key+"SubrectBaseY").c_str(),0u);p->Set((key+"SubrectWidth").c_str(),input.color.extent.width);p->Set((key+"SubrectHeight").c_str(),input.color.extent.height);}
                result=neural->evaluate(target,eye.handle,p,nullptr);
            }else result=NGX_VULKAN_EVALUATE_DLSS_EXT(target,eye.handle,eye.parameters,&eval);
            for(const auto* i:{&input.color,&input.depth,&input.motion})Barrier(target,*i,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,i->layout);
            if(NVSDK_NGX_FAILED(result)){pairFailed=true;error="NGX evaluation failed for eye "+std::to_string(e)+": "+std::to_string(uint32_t(result));break;}
            ++evaluations;if(eval.InReset)++resetEvaluations;eye.output.identity=input.color.identity;
        }
    }
};
VulkanDlss::VulkanDlss(Options o):state_(std::make_unique<State>(std::move(o))){state_->Initialize();}
VulkanDlss::~VulkanDlss()=default;
Status VulkanDlss::Submit(const PairInput& pair){auto& s=*state_;
    if(pair.mode==Mode::Off)return Status::Disabled;
    const auto available=s.Availability();if(available!=Status::Ready)return available;
    try{s.Validate(pair);}catch(const std::exception& e){s.error=e.what();return Status::Rejected;}
    bool recording=false;
    try{
        vkCheck(s.f.vkResetCommandPool(s.options.device,s.pool,0),"Reset DLSS pool");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkCheck(s.f.vkBeginCommandBuffer(s.command,&begin),"Begin DLSS");recording=true;
        s.Evaluate(pair,s.command);
        vkCheck(s.f.vkEndCommandBuffer(s.command),"End DLSS");recording=false;
        vkCheck(s.f.vkResetFences(s.options.device,1,&s.fence),"Reset DLSS fence");
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&s.command;
        {State::QueueLock lock(s.options);const auto result=s.f.vkQueueSubmit(s.options.queue,1,&submit,s.fence);
            if(result!=VK_SUCCESS){s.fatal=true;s.error="Vulkan submission failed: "+std::to_string(result);return Status::Failed;}}
        s.pending=true;++s.ticket;s.lastEpoch=pair.eyes[0].color.identity.epoch;s.lastFrame=pair.eyes[0].color.identity.frame;
        return Status::Pending;
    }catch(const std::exception& e){s.error=e.what();s.pairFailed=true;
        // A failed creation/recording invalidates both private histories. No
        // submitted work refers to them in this path; the original inputs stay.
        if(recording)s.f.vkEndCommandBuffer(s.command);
        for(auto& eye:s.eyes)s.Clear(eye);
        return Status::Failed;
    }
}
Status VulkanDlss::Record(const PairInput& pair,VkCommandBuffer command,Output& out){auto& s=*state_;out={};
    if(pair.mode==Mode::Off)return Status::Disabled;
    const auto available=s.Availability();if(available!=Status::Ready)return available;
    try{need(command!=VK_NULL_HANDLE,"Missing caller command buffer");s.Validate(pair);}
    catch(const std::exception& e){s.error=e.what();return Status::Rejected;}
    // From this point even partial failure may have written commands referring
    // to private allocations. Retain them until explicit discard or retirement.
    s.recorded=true;s.recordedFrame=pair.eyes[0].color.identity.frame;s.recordedEpoch=pair.eyes[0].color.identity.epoch;
    out.ticket=++s.ticket;
    try{s.Evaluate(pair,command);if(s.pairFailed)return Status::Failed;
        for(unsigned e=0;e<2;++e){auto& image=s.eyes[e].output;
            s.Barrier(command,image,image.layout,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            image.layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;out.eyes[e]=image;}
        return Status::Recorded;
    }catch(const std::exception& e){s.error=e.what();s.pairFailed=true;return Status::Failed;}
}
bool VulkanDlss::CommitRecorded(uint64_t ticket,VkFence completion){auto& s=*state_;
    if(!s.recorded||ticket!=s.ticket||!completion||completion==s.fence)return false;
    s.consumer=completion;s.clearAfterConsumer=s.pairFailed;s.recorded=false;
    if(!s.pairFailed){s.lastFrame=s.recordedFrame;s.lastEpoch=s.recordedEpoch;}
    return true;
}
bool VulkanDlss::CancelRecorded(uint64_t ticket){auto& s=*state_;
    if(!s.recorded||ticket!=s.ticket)return false;
    for(auto& eye:s.eyes)s.Clear(eye);
    s.recorded=false;s.pairFailed=false;return true;
}
bool VulkanDlss::RetireRecordedAfterDeviceIdle(uint64_t ticket){return RetireRecordedAfterCompletion(ticket);}
bool VulkanDlss::ReleaseResources(){auto& s=*state_;
    if(s.Availability()!=Status::Ready)return false;
    for(auto& eye:s.eyes)s.Clear(eye);
    s.lastFrame=s.lastEpoch=0;s.pairFailed=false;s.error.clear();return true;
}
bool VulkanDlss::RetireRecordedAfterCompletion(uint64_t ticket){auto& s=*state_;
    if(!s.recorded||ticket!=s.ticket)return false;
    if(s.pairFailed){for(auto& eye:s.eyes)s.Clear(eye);}
    else{s.lastFrame=s.recordedFrame;s.lastEpoch=s.recordedEpoch;}
    s.recorded=false;s.pairFailed=false;return true;
}
Status VulkanDlss::Poll(Output& out){auto& s=*state_;out={};if(s.fatal)return Status::Failed;
    if(s.recorded)return Status::Recorded;
    if(s.pending){const auto r=s.f.vkGetFenceStatus(s.options.device,s.fence);if(r==VK_NOT_READY)return Status::Pending;
        if(r!=VK_SUCCESS){s.error="DLSS GPU completion failed";s.fatal=true;return Status::Failed;}
        s.pending=false;
        if(s.pairFailed){for(auto& eye:s.eyes)s.Clear(eye);return Status::Failed;}s.ready=true;
    }
    if(!s.ready)return Status::Disabled;
    out.ticket=s.ticket;for(unsigned e=0;e<2;++e)out.eyes[e]=s.eyes[e].output;return Status::Ready;
}
bool VulkanDlss::Release(VkFence completion){auto& s=*state_;if(!s.ready||!completion||completion==s.fence)return false;
    s.consumer=completion;s.ready=false;return true;
}
const std::string& VulkanDlss::Error()const{return state_->error;}
uint64_t VulkanDlss::Evaluations()const{return state_->evaluations;}
uint64_t VulkanDlss::ResetEvaluations()const{return state_->resetEvaluations;}
}
