#include "../overlay/DoomLayerControl.h"
#include "NativeSfs.h"
#include "NativeDispatch.h"
#include "TemporalAuditCapture.h"
#include "DoomGeometryAudit.h"
#include "DoomTemporalBridge.h"
#include "DoomFinalBridge.h"
#include "DoomFinalScene.h"
#include "DoomFinalMapping.h"
#include "DoomFinalPass.h"
#ifdef KHARVOX_HAVE_RECONSTRUCTION
#include "DoomReconstructionSession.h"
#endif
#include "DoomTemporalUniforms.h"
#include "DoomTemporalParameters.h"
#include "DoomTemporalFailure.h"
#ifdef KHARVOX_HAVE_DLSS_STARTUP
#include "DoomDlssSession.h"
#endif
#include "TemporalContextVulkan.h"
#include <sstream>
#include "SourceRing.h"
#include "PushReplay.h"
#include "CommandCpuTiming.h"
#include "CommandLookupCache.h"
#include "CommandBindings.h"
#include "UnlockedDriverScope.h"
#include "ShaderCompiler.h"
#include "ShaderProfile.h"
#include "PipelineIdentity.h"
#include "ShadowProjection.h"
#include "StereoResources.h"
#include "FrameProjection.h"
#include "SourcePoseHistory.h"
#include "../native/NativeStereo.h"
#include "../common/RuntimeLog.h"
#include "../common/DiagnosticLogging.h"
#include <windows.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <type_traits>

namespace kharvox::sfs {
namespace {
// Developer-only native multiview/OpenXR prototype. Headset validation and
// complete profile lighting corrections are required before a playable release.
void note(const std::string& text){kharvox::writeRuntimeLog("[SFS]",text,true);}
struct CommandState {
    bool primary{},recordingSupported{};
    uint32_t family{UINT32_MAX};
    bool stereo{};
    VkPipeline compute{};
    VkPipeline graphics{};
    struct Descriptor {VkPipelineLayout layout{};VkDescriptorSet set{};std::vector<uint32_t> dynamic;};
    std::vector<Descriptor> descriptors,computeDescriptors;
    CommandBindings bindings;
    PushReplay pushes;
    std::vector<VkImageMemoryBarrier> imageBarriers;
    std::vector<VkImageSubresourceRange> clearRanges;
    uint64_t auditGeneration{},auditDraw{},contextAllocation{};
    std::vector<uint32_t> auditOperations;
    VkRenderPass auditPass{};VkFramebuffer auditFramebuffer{};
    VkRect2D auditArea{};uint32_t auditSubpass{};
    VkRenderPassBeginInfo bridgeBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    uint32_t passDraws{};bool bridgeInline{};
};
struct State {
    std::mutex statusMutex;std::string statusError;uint64_t completedFrame{},fpsFrame{},fpsStart{},fpsCount{};double renderFps{};
    bool controlsDraining{};
    struct SceneWrite {uint64_t frame{};VkCommandBuffer command{};bool submitted{},completed{};};
    std::mutex sceneWritesMutex;
    std::unordered_map<VkImage,SceneWrite> sceneWriteFrames;
    std::unordered_set<std::string> finalSkipReasons;
#ifdef KHARVOX_HAVE_DLSS_STARTUP
    std::unique_ptr<DoomDlssSession> dlss;
    std::optional<kharvox::dlss::Options> liveOptions;
    uint64_t controlPoll{},controlAttempt{};
    std::optional<overlay::Selection> outputSelection;
#endif
#ifdef KHARVOX_HAVE_RECONSTRUCTION
    std::unique_ptr<DoomReconstructionSession> reconstruction;
#endif
    std::unordered_set<uint64_t> finalShaderHashes;
    std::unordered_map<VkRenderPass,bool> finalPasses;
    std::unordered_map<VkDescriptorSet,VkDescriptorImageInfo> finalColors;
    std::unordered_map<VkPipeline,std::unique_ptr<DoomFinalBridge>> finalBridges;
    std::mutex dlssMutex;
    VkPipeline dlssPipeline{};
    std::unordered_map<VkCommandPool,uint32_t> poolFamilies;
    VkDevice device{};NativeDispatch dispatch;
    bool bridgeEnabled{};PFN_vkGetDeviceProcAddr bridgeDispatch{};VkPhysicalDeviceMemoryProperties bridgeMemory{};
    std::unordered_map<VkRenderPass,bool> bridgePasses;
    std::unordered_map<VkPipelineLayout,VkDescriptorSetLayout> bridgeLayouts;
    std::unordered_map<VkFramebuffer,VkExtent2D> bridgeExtents;
    std::unordered_map<VkDescriptorSet,VkDescriptorImageInfo> bridgeColors;
    std::unordered_map<VkPipeline,std::unique_ptr<DoomTemporalBridge>> bridges;
    std::atomic<uint64_t> bridgeDraws{};
    std::atomic<uint32_t> bridgeSkipReasons{};
    std::unique_ptr<DoomTemporalUniforms> temporalUniforms;
    std::shared_ptr<doom_temporal::ViewSnapshots> temporalViews;
    std::optional<doom_temporal::ViewSnapshot> temporalPrevious;
    std::optional<DoomTemporalParameters> temporalParameters;
    DoomTemporalUniformSnapshot temporalSnapshot{};
    std::mutex temporalSnapshotMutex;
    uint64_t temporalSnapshotSerial{};
    std::atomic<uint64_t> temporalReads{},temporalReadMisses{};
    unsigned temporalFailureFiles{};uint64_t temporalFailureNextRead{};
    std::unique_ptr<SourceRing> sources;
    // Command buffers are externally synchronized by Vulkan callers. Parallel
    // recorders share resource metadata; only creation/retirement needs exclusivity.
    std::shared_mutex mutex;
    Images images;
    std::unique_ptr<TemporalAuditCapture> audit;
    std::unique_ptr<kharvox::temporal_context::VulkanMetadata> context;
    std::unordered_map<VkDescriptorSetLayout,bool> auditLayouts;
    std::unordered_map<VkPipelineLayout,bool> auditPipelineLayouts;
    std::unordered_map<VkImage,VkImageCreateInfo> auditImages;
    std::unordered_map<VkDescriptorSetLayout,std::array<uint32_t,2>> auditLayoutDynamic;
    std::unordered_map<VkDescriptorSet,std::array<uint32_t,2>> auditSetDynamic;
    std::unordered_map<VkPipeline,TemporalAuditOperation> auditPipelines;
    std::unordered_map<VkFramebuffer,std::vector<VkImageView>> auditFramebufferViews;
    std::unordered_map<VkSwapchainKHR,std::vector<VkImage>> swapchains;
    VkBuffer params{};VkDeviceMemory paramsMemory{};
    FrameUniforms pendingUniforms{};
    native::FramePose pendingPose{},renderPose{};
    std::unordered_map<VkImage,native::FramePose> imagePoses;
    std::unordered_map<VkImage,FrameUniforms> imageUniforms;
    SourcePoseHistory sourcePoseHistory;
    FrameUniforms renderUniforms;
    uint64_t sourcePoseSamples{};
    bool pending{},completed{true},frameValid{};
    bool profileTiming{};
    uint64_t profiledFrames{},retireNs{},uploadNs{},maxRetireNs{};
    std::unordered_map<VkShaderModule,std::vector<uint32_t>> shaders;
    std::unordered_map<std::string,VkShaderModule> compiled;
    std::unordered_set<VkShaderModule> geometryAuditModules;
    std::unordered_map<VkRenderPass,VkRenderPass> passes;
    std::unordered_map<VkImageView,uint32_t> viewLayers;
    std::unordered_map<VkImageView,VkImageViewCreateInfo> viewInfos;
    std::unordered_map<VkImageView,std::array<VkImageView,2>> eyeViews;
    std::unordered_map<VkFramebuffer,bool> framebufferStereo;
    std::unordered_map<VkPipeline,VkPipeline> stereoPipelines;
    std::unordered_map<VkPipeline,bool> computeStereo;
    std::unordered_map<VkPipeline,std::array<VkPipeline,2>> indirectPipelines;
    std::unordered_map<VkFramebuffer,bool> mixedFramebuffers;
    std::atomic<uint64_t> indirectMono{},indirectStereo{},mixedPasses{};
    uint32_t mixedDiagnostics{};
    uint32_t materialDiagnostics{};
    uint32_t auditLayoutDiagnostics{};
    std::unordered_map<VkDescriptorSetLayout,uint32_t> dynamicCounts;
    std::unordered_map<VkDescriptorSet,uint32_t> setDynamicCounts;
    std::unordered_map<VkDescriptorSet,VkDescriptorPool> setPools;
    std::unordered_map<VkCommandBuffer,CommandState> commands;
    std::unordered_map<VkCommandBuffer,VkCommandPool> commandPools;
    std::filesystem::path profile;
};
std::mutex devicesMutex;
std::unordered_map<void*,std::shared_ptr<State>> devices;
std::atomic<uint64_t> deviceGeneration{1};
template<class T>void* dispatchKey(T handle){return handle?*reinterpret_cast<void**>(handle):nullptr;}
template<class T>std::shared_ptr<State> state(T handle){
    struct Cache {void* key{};uint64_t generation{};std::weak_ptr<State> state;};
    thread_local Cache cache;
    const auto generation=deviceGeneration.load(std::memory_order_acquire);
    const auto key=dispatchKey(handle);
    if(cache.key==key&&cache.generation==generation){
        if(auto cached=cache.state.lock()){
            if constexpr(std::is_same_v<T,VkDevice>){if(cached->device==handle)return cached;}
            else return cached;
        }
    }
    std::lock_guard<std::mutex> lock(devicesMutex);
    if constexpr(std::is_same_v<T,VkDevice>){
        // The loader can replace a newly-created device's dispatch table after
        // our CreateDevice returns. Keep identity by the device handle as well.
        std::shared_ptr<State> found;
        for(const auto& entry:devices)if(entry.second->device==handle){found=entry.second;break;}
        if(found){devices[key]=found;cache={key,generation,found};return found;}
        throw std::runtime_error("Unregistered SFS device");
    }
    auto it=devices.find(key);if(it==devices.end())throw std::runtime_error("SFS device not initialized handle="+std::to_string(reinterpret_cast<uintptr_t>(handle))+" dispatch="+std::to_string(reinterpret_cast<uintptr_t>(key)));cache={key,generation,it->second};return it->second;
}
#define FN(name) NativeDispatch::require(s->dispatch.name,#name)
#define RESULT_BEGIN try {auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);
#define RESULT_END }catch(const std::exception& e){note(std::string(__FUNCTION__)+": "+e.what());return VK_ERROR_INITIALIZATION_FAILED;}
template<class F>void forReconstruction(const std::shared_ptr<State>& s,F&& fn){
#ifdef KHARVOX_HAVE_DLSS_STARTUP
    if(s->dlss)fn(*s->dlss);
#endif
#ifdef KHARVOX_HAVE_RECONSTRUCTION
    if(s->reconstruction)fn(*s->reconstruction);
#endif
}
void retireBridges(const std::shared_ptr<State>& s){for(auto& b:s->bridges)b.second->retired();for(auto& b:s->finalBridges)b.second->retired();}
void dlssDiscarded(const std::shared_ptr<State>& s,VkCommandBuffer cb){
    for(auto& entry:s->sceneWriteFrames)if(entry.second.command==cb){if(!entry.second.completed)entry.second={};else entry.second.command={};}
    bool retired=false;std::lock_guard<std::mutex> lock(s->dlssMutex);
    forReconstruction(s,[&](auto& p){const auto old=p.pendingCommand();p.discarded(cb);retired|=old&&!p.pendingCommand();});
    if(retired)retireBridges(s);
}
void dlssIdle(const std::shared_ptr<State>& s){for(auto& entry:s->sceneWriteFrames)if(entry.second.submitted)entry.second.completed=true;std::lock_guard<std::mutex> lock(s->dlssMutex);forReconstruction(s,[](auto& p){p.retiredAfterIdle();});}
bool dlssCanReuse(const std::shared_ptr<State>& s){bool ready=true;std::lock_guard<std::mutex> lock(s->dlssMutex);forReconstruction(s,[&](auto& p){ready&=!p.pendingCommand();});
#ifdef KHARVOX_HAVE_RECONSTRUCTION
    if(s->reconstruction)ready&=!s->reconstruction->pendingGuides();
#endif
    return ready;
}
void dlssShutdown(const std::shared_ptr<State>& s){
    std::lock_guard<std::mutex> lock(s->dlssMutex);bool exists=false;forReconstruction(s,[&](auto&){exists=true;});if(!exists)return;
    const auto d=s->device;
    if(FN(vkDeviceWaitIdle)(d)!=VK_SUCCESS)throw std::runtime_error("Reconstruction shutdown idle failed");
    forReconstruction(s,[&](auto& p){p.retiredAfterIdle();const auto command=p.pendingCommand();
        if(command){const auto pool=s->commandPools.at(command);
            if(FN(vkResetCommandPool)(d,pool,0)!=VK_SUCCESS)throw std::runtime_error("Reconstruction shutdown discard failed");p.discarded(command);}});
#ifdef KHARVOX_HAVE_DLSS_STARTUP
    s->dlss.reset();
#endif
#ifdef KHARVOX_HAVE_RECONSTRUCTION
    s->reconstruction.reset();
#endif
}
VKAPI_ATTR VkResult VKAPI_CALL createCommandPool(VkDevice d,const VkCommandPoolCreateInfo* i,const VkAllocationCallbacks* a,VkCommandPool* out){RESULT_BEGIN
    auto create=FN(vkCreateCommandPool);
    const auto r=create(d,i,a,out);if(r==VK_SUCCESS&&!i->pNext&&!(i->flags&VK_COMMAND_POOL_CREATE_PROTECTED_BIT))s->poolFamilies[*out]=i->queueFamilyIndex;return r;
RESULT_END}
// Command hooks cannot return VkResult. Fail the owned diagnostic process on an
// unsupported command rather than record an invalid command or wait forever.
[[noreturn]] void commandFailure(const char* message){note(message);RaiseFailFastException(nullptr,nullptr,0);std::terminate();}
#define COMMAND_BEGIN try {CommandCpuTiming timing;auto s=state(cb);std::shared_lock<std::shared_mutex> lock(s->mutex);timing.acquired();
#define COMMAND_END }catch(const std::exception& e){commandFailure(e.what());}
// One hash lookup per command buffer per thread instead of one to three per
// command. Every erase of s->commands below calls CommandLookupCache::invalidate().
CommandState& commandOf(const std::shared_ptr<State>& s,VkCommandBuffer cb){return CommandLookupCache::find(s.get(),s->commands,cb);}

#include "NativeTemporalContext.inc"

VKAPI_ATTR VkResult VKAPI_CALL parameterAllocate(VkDevice d,const VkMemoryAllocateInfo* i,const VkAllocationCallbacks* a,VkDeviceMemory* out){RESULT_BEGIN
    const auto r=FN(vkAllocateMemory)(d,i,a,out);if(r==VK_SUCCESS&&s->temporalUniforms)
        s->temporalUniforms->memory.allocate(*out,i->allocationSize,i->memoryTypeIndex<s->bridgeMemory.memoryTypeCount?s->bridgeMemory.memoryTypes[i->memoryTypeIndex].propertyFlags:0);return r;
RESULT_END}
VKAPI_ATTR void VKAPI_CALL parameterFree(VkDevice d,VkDeviceMemory memory,const VkAllocationCallbacks* a){auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);if(s->temporalUniforms)s->temporalUniforms->memory.free(memory);FN(vkFreeMemory)(d,memory,a);}
VKAPI_ATTR VkResult VKAPI_CALL parameterBuffer(VkDevice d,const VkBufferCreateInfo* i,const VkAllocationCallbacks* a,VkBuffer* out){RESULT_BEGIN
    const auto r=FN(vkCreateBuffer)(d,i,a,out);if(r==VK_SUCCESS&&s->temporalUniforms)s->temporalUniforms->memory.create(*out,i->size,i->usage,!i->pNext&&!i->flags);return r;
RESULT_END}
VKAPI_ATTR void VKAPI_CALL parameterDestroyBuffer(VkDevice d,VkBuffer buffer,const VkAllocationCallbacks* a){auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);if(s->temporalUniforms)s->temporalUniforms->memory.destroy(buffer);FN(vkDestroyBuffer)(d,buffer,a);}
VKAPI_ATTR VkResult VKAPI_CALL parameterBind(VkDevice d,VkBuffer buffer,VkDeviceMemory memory,VkDeviceSize offset){RESULT_BEGIN
    const auto r=FN(vkBindBufferMemory)(d,buffer,memory,offset);if(r==VK_SUCCESS&&s->temporalUniforms)s->temporalUniforms->memory.bind(buffer,memory,offset);return r;
RESULT_END}
VKAPI_ATTR VkResult VKAPI_CALL parameterMap(VkDevice d,VkDeviceMemory memory,VkDeviceSize offset,VkDeviceSize size,VkMemoryMapFlags flags,void** out){RESULT_BEGIN
    const auto r=FN(vkMapMemory)(d,memory,offset,size,flags,out);if(r==VK_SUCCESS&&s->temporalUniforms)s->temporalUniforms->memory.map(memory,offset,size,*out);return r;
RESULT_END}
VKAPI_ATTR void VKAPI_CALL parameterUnmap(VkDevice d,VkDeviceMemory memory){auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);if(s->temporalUniforms)s->temporalUniforms->memory.unmap(memory);FN(vkUnmapMemory)(d,memory);}

VkShaderModule compiledModule(const std::shared_ptr<State>& s,VkShaderModule original,uint64_t variant,bool& stereoCompute,bool shadowProjection=false,int indirectEye=-1,bool monoView=false,bool auditLayout=false,int bridgeStage=0){
    const auto found=s->shaders.find(original);if(found==s->shaders.end())throw std::runtime_error("Untracked game shader module");
    const auto& words=found->second;const auto primary=profileHash(words.data(),uint32_t(words.size()*4));
    VkShaderStageFlagBits stage{};
    for(size_t i=5;i<words.size();){auto n=words[i]>>16;if(!n||n>words.size()-i)throw std::runtime_error("Invalid game SPIR-V");if((words[i]&65535)==15){stage=words[i+1]==0?VK_SHADER_STAGE_VERTEX_BIT:words[i+1]==4?VK_SHADER_STAGE_FRAGMENT_BIT:words[i+1]==5?VK_SHADER_STAGE_COMPUTE_BIT:VkShaderStageFlagBits(0);break;}i+=n;}
    if(!stage)throw std::runtime_error("Unsupported SFS shader execution model");
    const auto replacement=monoView?ProfileShader{}:loadProfileShader(s->profile,primary,variant,stage);
    const auto& input=replacement?replacement.words:words;
    stereoCompute=hasStereoStorageOutput(input);
    const bool sharedShadow=shadowProjection&&!replacement&&stage==VK_SHADER_STAGE_VERTEX_BIT;
    const bool audit=s->audit&&auditLayout&&!s->audit->geometryMode()&&!monoView&&stage==VK_SHADER_STAGE_FRAGMENT_BIT&&replacement&&primary==0x61d26e466ae9911aULL;
    const bool geometry=s->audit&&auditLayout&&s->audit->geometryMode()&&!monoView&&stage==VK_SHADER_STAGE_VERTEX_BIT&&primary==doomGeometryAuditHash;
    const auto key=shaderKey(primary)+"_"+shaderKey(variant)+(sharedShadow?"_shadow":"")+(indirectEye>=0?"_indirect"+std::to_string(indirectEye):"")+(monoView?"_mono":"")+(audit?"_audit":"")+(geometry?"_geometry_audit":"")+(bridgeStage?"_bridge"+std::to_string(bridgeStage):"");
    auto cached=s->compiled.find(key);if(cached!=s->compiled.end())return cached->second;
    ShaderCompileOptions options;options.computeStereo=stereoCompute&&!replacement;
    options.profileReplacement=bool(replacement);
    options.screenSpaceUi=doomUiShader(primary);
    options.indirectEye=indirectEye;
    options.monoscopicView=monoView;
    options.vertexProjection=needsHeadsetProjection(input,bool(replacement),shadowProjection);
    options.temporalAudit=audit;options.geometryAudit=geometry;
    if(bridgeStage==1||bridgeStage==2){if(!replacement||primary!=0x61d26e466ae9911aULL)throw std::runtime_error("Unrecognized bridge shader");options.temporalGuides=options.temporalColor=bridgeStage==1;options.temporalResolve=bridgeStage==2;}
    if(bridgeStage==3||bridgeStage==4){if(!s->finalShaderHashes.count(primary))throw std::runtime_error("Unrecognized final scene shader");options.finalSceneCopy=bridgeStage==3;options.finalSceneResolve=bridgeStage==4;}
    CompiledShader shader;
    try{shader=compileStereoShader(input,options);}
    catch(const std::exception& e){if(!audit&&!geometry)throw;note(std::string("temporal audit shader disabled: ")+e.what());options.temporalAudit=false;options.geometryAudit=false;shader=compileStereoShader(input,options);}
    if(!monoView&&!bridgeStage&&stage==VK_SHADER_STAGE_FRAGMENT_BIT&&isDoomFinalScene(shader.glsl))s->finalShaderHashes.insert(primary);
    if(shader.temporalAuditApplied)note("temporal audit enabled for shader="+key);
    if(shader.geometryAuditApplied)note("geometry audit enabled for shader="+key);
    note("shader="+key+" stage="+std::to_string(stage)+" profile="+(replacement?"matched":"generic")+
         " projection="+std::to_string(shader.vertexProjectionApplied)+" screenUi="+std::to_string(shader.screenSpaceUiApplied)+" sharedShadow="+std::to_string(sharedShadow)+" shadowPass="+std::to_string(shadowProjection)+" stereoCompute="+std::to_string(stereoCompute)+
         " clusters="+std::to_string(shader.clusterCorrections)+" world="+std::to_string(shader.worldCorrections)+" refraction="+std::to_string(shader.refractionCorrections)+" temporal="+std::to_string(shader.temporalCorrections)+" ssdo="+std::to_string(shader.ssdoCorrections)+" ssr="+std::to_string(shader.ssrCorrections)+" particleCollision="+std::to_string(shader.particleCollisionApplied)+" objectMotion="+std::to_string(shader.objectMotionApplied));
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};info.codeSize=shader.words.size()*4;info.pCode=shader.words.data();
    VkShaderModule result{};auto r=FN(vkCreateShaderModule)(s->device,&info,nullptr,&result);
    if(r!=VK_SUCCESS)throw std::runtime_error("Stereo shader module creation failed");
    if(shader.geometryAuditApplied)s->geometryAuditModules.insert(result);
    s->compiled.emplace(key,result);return result;
}
VKAPI_ATTR VkResult VKAPI_CALL createShader(VkDevice d,const VkShaderModuleCreateInfo* i,const VkAllocationCallbacks* a,VkShaderModule* out){RESULT_BEGIN
    auto r=FN(vkCreateShaderModule)(d,i,a,out);if(r==VK_SUCCESS)s->shaders[*out]={i->pCode,i->pCode+i->codeSize/4};return r;
RESULT_END}
VKAPI_ATTR void VKAPI_CALL destroyShader(VkDevice d,VkShaderModule shader,const VkAllocationCallbacks* a){auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);s->shaders.erase(shader);FN(vkDestroyShaderModule)(d,shader,a);}
VKAPI_ATTR VkResult VKAPI_CALL createImage(VkDevice d,const VkImageCreateInfo* i,const VkAllocationCallbacks* a,VkImage* out){RESULT_BEGIN
    auto info=*i;
    if(stereoImage(info)&&(info.usage&VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))info.usage|=VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    const auto r=s->images.create(d,info,a,out,FN(vkCreateImage));if(r==VK_SUCCESS&&(s->audit||s->bridgeEnabled)){auto actual=imageInfo(info);actual.pNext=nullptr;actual.pQueueFamilyIndices=nullptr;s->auditImages[*out]=actual;}if(r==VK_SUCCESS)contextImage(s,*out,imageInfo(info));return r;
RESULT_END}
VKAPI_ATTR void VKAPI_CALL destroyImage(VkDevice d,VkImage image,const VkAllocationCallbacks* a){auto s=state(d);if(s->audit||s->bridgeEnabled){std::unique_lock<std::shared_mutex> lock(s->mutex);s->auditImages.erase(image);s->sceneWriteFrames.erase(image);observe(s,[&](auto& c,auto&){c.forget("image",tc::handle(image));});}s->images.destroy(d,image,a,FN(vkDestroyImage));}
VKAPI_ATTR VkResult VKAPI_CALL createView(VkDevice d,const VkImageViewCreateInfo* i,const VkAllocationCallbacks* a,VkImageView* out){RESULT_BEGIN
    auto info=s->images.shaderViewInfo(*i);auto r=FN(vkCreateImageView)(d,&info,a,out);if(r==VK_SUCCESS){s->viewLayers[*out]=(s->images.layers(i->image)==2&&info.subresourceRange.baseArrayLayer==0&&info.subresourceRange.layerCount>=2)?2:1;if(!info.pNext)s->viewInfos[*out]=info;contextView(s,*out,info);}return r;
RESULT_END}
VKAPI_ATTR void VKAPI_CALL destroyView(VkDevice d,VkImageView view,const VkAllocationCallbacks* a){auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);auto eyes=s->eyeViews.find(view);if(eyes!=s->eyeViews.end()){for(auto eye:eyes->second)if(eye)FN(vkDestroyImageView)(d,eye,nullptr);s->eyeViews.erase(eyes);}observe(s,[&](auto& c,auto&){c.forget("view",tc::handle(view));});s->viewInfos.erase(view);s->viewLayers.erase(view);FN(vkDestroyImageView)(d,view,a);}
VKAPI_ATTR VkResult VKAPI_CALL createPass(VkDevice d,const VkRenderPassCreateInfo* i,const VkAllocationCallbacks* a,VkRenderPass* out){RESULT_BEGIN
    auto input=*i;std::vector<VkAttachmentDescription> attachments;
    if(s->sources&&i->attachmentCount){attachments.assign(i->pAttachments,i->pAttachments+i->attachmentCount);for(auto& attachment:attachments){
        if(attachment.initialLayout==VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)attachment.initialLayout=VK_IMAGE_LAYOUT_GENERAL;
        if(attachment.finalLayout==VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)attachment.finalLayout=VK_IMAGE_LAYOUT_GENERAL;
    }input.pAttachments=attachments.data();}
    RenderPassPlan plan(input,true);if(!plan.valid())return VK_ERROR_FEATURE_NOT_PRESENT;
    auto r=FN(vkCreateRenderPass)(d,&input,a,out);if(r!=VK_SUCCESS)return r;VkRenderPass stereo{};r=FN(vkCreateRenderPass)(d,&plan.info(),a,&stereo);
    if(r!=VK_SUCCESS){FN(vkDestroyRenderPass)(d,*out,a);*out=VK_NULL_HANDLE;return r;}s->passes[*out]=stereo;
    if(s->bridgeEnabled){bool valid=!i->pNext&&!i->flags&&i->subpassCount==1&&i->attachmentCount==2;
        if(valid){const auto& sub=i->pSubpasses[0];valid=!sub.flags&&sub.colorAttachmentCount==2&&!sub.inputAttachmentCount&&!sub.pResolveAttachments&&!sub.pDepthStencilAttachment&&!sub.preserveAttachmentCount;
            if(valid)for(unsigned n=0;n<2;++n){const auto& a=i->pAttachments[n];valid &= sub.pColorAttachments[n].attachment==n&&sub.pColorAttachments[n].layout==VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                &&a.format==(n?VK_FORMAT_R16G16_SFLOAT:VK_FORMAT_B10G11R11_UFLOAT_PACK32)&&a.samples==VK_SAMPLE_COUNT_1_BIT&&a.storeOp==VK_ATTACHMENT_STORE_OP_STORE
                &&((a.initialLayout==a.finalLayout&&a.initialLayout==VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL&&a.loadOp==VK_ATTACHMENT_LOAD_OP_LOAD)
                    ||(a.initialLayout==VK_IMAGE_LAYOUT_UNDEFINED&&a.loadOp==VK_ATTACHMENT_LOAD_OP_DONT_CARE));}}
        s->bridgePasses[*out]=valid;if(valid)note("[TEMPORAL-BRIDGE] compatible two-target pass registered");}
    if(s->bridgeEnabled)s->finalPasses[*out]=doomFinalPass(input);
    observe(s,[&](auto& c,auto&){c.define("pass",tc::handle(*out),tc::pass(input));c.define("pass",tc::handle(stereo),tc::pass(plan.info()));});return VK_SUCCESS;
RESULT_END}
VKAPI_ATTR void VKAPI_CALL destroyPass(VkDevice d,VkRenderPass pass,const VkAllocationCallbacks* a){auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);s->bridgePasses.erase(pass);s->finalPasses.erase(pass);auto it=s->passes.find(pass);if(it!=s->passes.end()){FN(vkDestroyRenderPass)(d,it->second,a);observe(s,[&](auto& c,auto&){c.forget("pass",tc::handle(it->second));});s->passes.erase(it);}observe(s,[&](auto& c,auto&){c.forget("pass",tc::handle(pass));});FN(vkDestroyRenderPass)(d,pass,a);}
VKAPI_ATTR VkResult VKAPI_CALL createFramebuffer(VkDevice d,const VkFramebufferCreateInfo* i,const VkAllocationCallbacks* a,VkFramebuffer* out){RESULT_BEGIN
    auto info=*i;bool stereo=i->attachmentCount!=0;
    if(i->flags&VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT)return VK_ERROR_FEATURE_NOT_PRESENT;
    for(uint32_t j=0;j<i->attachmentCount;++j){auto v=s->viewLayers.find(i->pAttachments[j]);if(v==s->viewLayers.end()||v->second<2)stereo=false;}
    uint32_t stereoAttachments{};for(uint32_t j=0;j<i->attachmentCount;++j){auto v=s->viewLayers.find(i->pAttachments[j]);if(v!=s->viewLayers.end()&&v->second>=2)++stereoAttachments;}
    const bool mixed=stereoAttachments&&stereoAttachments<i->attachmentCount;
    auto pass=s->passes.find(i->renderPass);if(pass==s->passes.end())return VK_ERROR_INITIALIZATION_FAILED;
    if(stereo)info.renderPass=pass->second;auto r=FN(vkCreateFramebuffer)(d,&info,a,out);if(r==VK_SUCCESS){
        s->framebufferStereo[*out]=stereo;s->mixedFramebuffers[*out]=mixed;
        observe(s,[&](auto& c,auto&){std::vector<uint64_t> views;for(uint32_t n=0;n<info.attachmentCount;++n)views.push_back(tc::handle(info.pAttachments[n]));c.define("framebuffer",tc::handle(*out),"{\"pass\":"+std::to_string(tc::handle(info.renderPass))+",\"width\":"+std::to_string(info.width)+",\"height\":"+std::to_string(info.height)+",\"layers\":"+std::to_string(info.layers)+",\"views\":"+tc::numbers(views.data(),views.size())+"}");if(i->pNext)c.gap("framebuffer-extension");});
        if(s->bridgeEnabled&&stereo&&!i->pNext&&i->layers==1)s->bridgeExtents[*out]={i->width,i->height};
        if((s->audit||s->bridgeEnabled)&&i->attachmentCount)s->auditFramebufferViews[*out].assign(i->pAttachments,i->pAttachments+i->attachmentCount);
        if(mixed&&s->mixedDiagnostics++<24){
            note("[SFS-MIXED] framebuffer="+std::to_string(reinterpret_cast<uintptr_t>(*out))+" size="+std::to_string(i->width)+"x"+std::to_string(i->height)+" stereoAttachments="+std::to_string(stereoAttachments)+" total="+std::to_string(i->attachmentCount)+" mode=mono");
            for(uint32_t j=0;j<i->attachmentCount;++j){auto v=s->viewInfos.find(i->pAttachments[j]);if(v!=s->viewInfos.end())note("[SFS-MIXED] attachment="+std::to_string(j)+" image="+std::to_string(reinterpret_cast<uintptr_t>(v->second.image))+" format="+std::to_string(v->second.format)+" baseLayer="+std::to_string(v->second.subresourceRange.baseArrayLayer)+" viewLayers="+std::to_string(v->second.subresourceRange.layerCount));}
        }
    }return r;
RESULT_END}
VKAPI_ATTR void VKAPI_CALL destroyFramebuffer(VkDevice d,VkFramebuffer fb,const VkAllocationCallbacks* a){auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);s->bridgeExtents.erase(fb);s->framebufferStereo.erase(fb);s->mixedFramebuffers.erase(fb);s->auditFramebufferViews.erase(fb);observe(s,[&](auto& c,auto&){c.forget("framebuffer",tc::handle(fb));});FN(vkDestroyFramebuffer)(d,fb,a);}
VKAPI_ATTR VkResult VKAPI_CALL createLayout(VkDevice d,const VkDescriptorSetLayoutCreateInfo* i,const VkAllocationCallbacks* a,VkDescriptorSetLayout* out){RESULT_BEGIN
    std::vector<VkDescriptorSetLayoutBinding> bindings;if(i->bindingCount)bindings.assign(i->pBindings,i->pBindings+i->bindingCount);uint32_t dynamic=0;
    for(const auto& b:bindings){if(b.binding==30||b.binding==31)throw std::runtime_error("Game descriptor binding 30/31 conflicts with SFS");if(b.descriptorType==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC||b.descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)dynamic+=b.descriptorCount;}
    if(i->pNext)throw std::runtime_error("SFS descriptor layout extension chain not supported by probe");
    const bool audit=s->audit&&(s->audit->geometryMode()?TemporalAuditCapture::supportsGeometry(*i):TemporalAuditCapture::supports(*i));
    if(s->audit&&i->bindingCount==(s->audit->geometryMode()?24u:7u)&&s->auditLayoutDiagnostics++<16){
        std::ostringstream detail;detail<<"temporal audit layout accepted="<<audit<<" bindings=";
        for(const auto& b:bindings)detail<<b.binding<<':'<<b.descriptorType<<':'<<b.descriptorCount<<':'<<b.stageFlags<<',';
        note(detail.str());
    }
    std::array<uint32_t,2> auditDynamic{};
    if(audit){for(const auto& b:bindings)if(b.binding<2)auditDynamic[b.binding]=b.descriptorType==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;bindings.push_back({32,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,1,VkShaderStageFlags(s->audit->geometryMode()?VK_SHADER_STAGE_VERTEX_BIT:VK_SHADER_STAGE_FRAGMENT_BIT),nullptr});}
    for(uint32_t binding:{30u,31u})bindings.push_back({binding,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT|VK_SHADER_STAGE_COMPUTE_BIT,nullptr});auto info=*i;info.bindingCount=uint32_t(bindings.size());info.pBindings=bindings.data();auto r=FN(vkCreateDescriptorSetLayout)(d,&info,a,out);if(r==VK_SUCCESS){if(s->temporalUniforms)s->temporalUniforms->layout(*out,*i);s->dynamicCounts[*out]=dynamic;s->auditLayouts[*out]=audit;if(audit)s->auditLayoutDynamic[*out]=auditDynamic;observe(s,[&](auto&,auto& m){m.layout(*out,*i);});}return r;
RESULT_END}
VKAPI_ATTR void VKAPI_CALL destroyLayout(VkDevice d,VkDescriptorSetLayout layout,const VkAllocationCallbacks* a){auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);observe(s,[&](auto&,auto& m){m.forgetLayout(layout);});if(s->temporalUniforms)s->temporalUniforms->forgetLayout(layout);s->dynamicCounts.erase(layout);s->auditLayouts.erase(layout);s->auditLayoutDynamic.erase(layout);FN(vkDestroyDescriptorSetLayout)(d,layout,a);}
VKAPI_ATTR VkResult VKAPI_CALL createPool(VkDevice d,const VkDescriptorPoolCreateInfo* i,const VkAllocationCallbacks* a,VkDescriptorPool* out){RESULT_BEGIN
    if(i->maxSets>UINT32_MAX/2)return VK_ERROR_OUT_OF_HOST_MEMORY;
    std::vector<VkDescriptorPoolSize> sizes;if(i->poolSizeCount)sizes.assign(i->pPoolSizes,i->pPoolSizes+i->poolSizeCount);sizes.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,i->maxSets*2});if(s->audit)sizes.push_back({VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,i->maxSets});auto info=*i;info.poolSizeCount=uint32_t(sizes.size());info.pPoolSizes=sizes.data();return FN(vkCreateDescriptorPool)(d,&info,a,out);
RESULT_END}
VKAPI_ATTR VkResult VKAPI_CALL allocateSets(VkDevice d,const VkDescriptorSetAllocateInfo* i,VkDescriptorSet* out){RESULT_BEGIN
    auto r=FN(vkAllocateDescriptorSets)(d,i,out);if(r!=VK_SUCCESS)return r;
    VkDescriptorBufferInfo buffer{s->params,0,sizeof(FrameUniforms)};
    std::vector<VkWriteDescriptorSet> writes(size_t(i->descriptorSetCount)*2);
    for(uint32_t j=0;j<i->descriptorSetCount;++j){s->setPools[out[j]]=i->descriptorPool;if(s->temporalUniforms)s->temporalUniforms->allocate(out[j],i->pSetLayouts[j],i->descriptorPool);observe(s,[&](auto&,auto& m){m.allocate(out[j],i->pSetLayouts[j],i->descriptorPool);});s->setDynamicCounts[out[j]]=s->dynamicCounts.at(i->pSetLayouts[j]);for(uint32_t k=0;k<2;++k){auto& w=writes[size_t(j)*2+k];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=out[j];w.dstBinding=30+k;w.descriptorCount=1;w.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;w.pBufferInfo=&buffer;}}
    FN(vkUpdateDescriptorSets)(d,uint32_t(writes.size()),writes.data(),0,nullptr);
    if(s->audit)for(uint32_t j=0;j<i->descriptorSetCount;++j)if(s->auditLayouts.at(i->pSetLayouts[j])){
        s->auditSetDynamic[out[j]]=s->auditLayoutDynamic.at(i->pSetLayouts[j]);
        auto auditBuffer=s->audit->allocate(out[j]);VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=out[j];w.dstBinding=32;w.descriptorCount=1;w.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;w.pBufferInfo=&auditBuffer;FN(vkUpdateDescriptorSets)(d,1,&w,0,nullptr);
    }return VK_SUCCESS;
RESULT_END}
void forgetPool(const std::shared_ptr<State>& s,VkDescriptorPool pool){
    if(s->temporalUniforms)s->temporalUniforms->pool(pool);
    observe(s,[&](auto&,auto& m){m.pool(pool);});
    for(auto it=s->setPools.begin();it!=s->setPools.end();)if(it->second==pool){s->bridgeColors.erase(it->first);s->finalColors.erase(it->first);s->setDynamicCounts.erase(it->first);s->auditSetDynamic.erase(it->first);if(s->audit)s->audit->forget(it->first);it=s->setPools.erase(it);}else ++it;
}
VKAPI_ATTR VkResult VKAPI_CALL freeSets(VkDevice d,VkDescriptorPool pool,uint32_t count,const VkDescriptorSet* sets){RESULT_BEGIN
    auto r=FN(vkFreeDescriptorSets)(d,pool,count,sets);if(r==VK_SUCCESS)for(uint32_t i=0;i<count;++i){observe(s,[&](auto&,auto& m){m.forget(sets[i]);});if(s->temporalUniforms)s->temporalUniforms->forget(sets[i]);s->bridgeColors.erase(sets[i]);s->finalColors.erase(sets[i]);s->setDynamicCounts.erase(sets[i]);s->auditSetDynamic.erase(sets[i]);s->setPools.erase(sets[i]);if(s->audit)s->audit->forget(sets[i]);}return r;
RESULT_END}
VKAPI_ATTR VkResult VKAPI_CALL resetPool(VkDevice d,VkDescriptorPool pool,VkDescriptorPoolResetFlags flags){RESULT_BEGIN
    auto r=FN(vkResetDescriptorPool)(d,pool,flags);if(r==VK_SUCCESS)forgetPool(s,pool);return r;
RESULT_END}
VKAPI_ATTR void VKAPI_CALL destroyPool(VkDevice d,VkDescriptorPool pool,const VkAllocationCallbacks* allocator){
    auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);forgetPool(s,pool);FN(vkDestroyDescriptorPool)(d,pool,allocator);
}
VKAPI_ATTR VkResult VKAPI_CALL createPipelineLayout(VkDevice d,const VkPipelineLayoutCreateInfo* i,const VkAllocationCallbacks* a,VkPipelineLayout* out){RESULT_BEGIN
    auto r=FN(vkCreatePipelineLayout)(d,i,a,out);
    if(r==VK_SUCCESS){s->auditPipelineLayouts[*out]=s->audit&&i->setLayoutCount==1&&s->auditLayouts[i->pSetLayouts[0]];
        if(s->bridgeEnabled&&!i->pNext&&!i->flags&&i->setLayoutCount==1&&!i->pushConstantRangeCount)s->bridgeLayouts[*out]=i->pSetLayouts[0];
        observe(s,[&](auto& c,auto&){std::vector<uint64_t> layouts;for(uint32_t n=0;n<i->setLayoutCount;++n)layouts.push_back(tc::handle(i->pSetLayouts[n]));c.define("pipelineLayout",tc::handle(*out),"{\"sets\":"+tc::numbers(layouts.data(),layouts.size())+"}");if(i->pNext)c.gap("pipeline-layout-extension");});}
    return r;
RESULT_END}
VKAPI_ATTR void VKAPI_CALL destroyPipelineLayout(VkDevice d,VkPipelineLayout layout,const VkAllocationCallbacks* a){
    auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);s->bridgeLayouts.erase(layout);s->auditPipelineLayouts.erase(layout);observe(s,[&](auto& c,auto&){c.forget("pipelineLayout",tc::handle(layout));});FN(vkDestroyPipelineLayout)(d,layout,a);
}
std::string auditResource(const std::shared_ptr<State>& s,const VkDescriptorImageInfo& info){
    std::ostringstream out;out<<"{\"view\":\""<<reinterpret_cast<uintptr_t>(info.imageView)<<"\",\"sampler\":\""<<reinterpret_cast<uintptr_t>(info.sampler)<<"\",\"declaredLayout\":"<<info.imageLayout;
    const auto found=s->viewInfos.find(info.imageView);
    if(found!=s->viewInfos.end()){
        const auto& v=found->second;const auto& r=v.subresourceRange;
        out<<",\"image\":\""<<reinterpret_cast<uintptr_t>(v.image)<<"\",\"viewType\":"<<v.viewType<<",\"format\":"<<v.format
            <<",\"range\":["<<r.aspectMask<<','<<r.baseMipLevel<<','<<r.levelCount<<','<<r.baseArrayLayer<<','<<r.layerCount<<']';
        const auto image=s->auditImages.find(v.image);
        if(image!=s->auditImages.end()){const auto& i=image->second;out<<",\"extent\":["<<i.extent.width<<','<<i.extent.height<<','<<i.extent.depth<<"],\"layers\":"<<i.arrayLayers<<",\"mips\":"<<i.mipLevels<<",\"usage\":"<<i.usage<<",\"samples\":"<<i.samples;}
    }out<<'}';return out.str();
}
VKAPI_ATTR void VKAPI_CALL updateSets(VkDevice d,uint32_t count,const VkWriteDescriptorSet* writes,uint32_t copies,const VkCopyDescriptorSet* copy){
    auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);
    // Only bookkeeping for the strict seven-binding layout. Other layouts and
    // extension write formats retain their original behavior.
    if(s->audit){
        for(uint32_t j=0;j<count;++j){const auto& w=writes[j];if(!s->audit->tracks(w.dstSet))continue;
            for(uint32_t k=0;k<w.descriptorCount&&w.dstBinding+k<7;++k){std::string resource="null";
                if(!w.pNext&&w.dstArrayElement==0){
                    if(w.descriptorType==VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER&&w.pImageInfo)resource=auditResource(s,w.pImageInfo[k]);
                    else if((w.descriptorType==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER||w.descriptorType==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)&&w.pBufferInfo){
                        const auto& b=w.pBufferInfo[k];const bool dynamic=w.descriptorType==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                        resource="{\"buffer\":\""+std::to_string(reinterpret_cast<uintptr_t>(b.buffer))+"\",\"offset\":\""+std::to_string(b.offset)+"\",\"range\":\""+std::to_string(b.range)+"\",\"dynamic\":"+(dynamic?"true":"false")+",\"effectiveOffsetQualified\":"+(dynamic?"false":"true")+"}";
                    }
                }s->audit->update(w.dstSet,w.dstBinding+k,resource);
            }
        }
        for(uint32_t j=0;j<copies;++j){const auto& c=copy[j];for(uint32_t k=0;k<c.descriptorCount&&c.dstBinding+k<7;++k){
            if(!c.pNext&&!c.srcArrayElement&&!c.dstArrayElement)s->audit->copy(c.srcSet,c.srcBinding+k,c.dstSet,c.dstBinding+k);
            else s->audit->update(c.dstSet,c.dstBinding+k,"null");
        }}
    }
    if(s->bridgeEnabled){
        for(uint32_t n=0;n<count;++n){const auto& w=writes[n];if(w.dstBinding<=4&&uint64_t(w.dstBinding)+w.descriptorCount>4){
            s->bridgeColors.erase(w.dstSet);
            if(!w.pNext&&!w.dstArrayElement&&w.descriptorCount==1&&w.dstBinding==4&&w.descriptorType==VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER&&w.pImageInfo)s->bridgeColors[w.dstSet]=*w.pImageInfo;
        }}
        for(uint32_t n=0;n<copies;++n){const auto& c=copy[n];if(c.dstBinding<=4&&uint64_t(c.dstBinding)+c.descriptorCount>4){
            const auto src=s->bridgeColors.find(c.srcSet);VkDescriptorImageInfo image{};if(src!=s->bridgeColors.end())image=src->second;s->bridgeColors.erase(c.dstSet);
            if(!c.pNext&&!c.srcArrayElement&&!c.dstArrayElement&&c.srcBinding==4&&c.dstBinding==4&&c.descriptorCount==1&&image.imageView)s->bridgeColors[c.dstSet]=image;
        }}
    }
    if(s->bridgeEnabled){
        for(uint32_t n=0;n<count;++n){const auto& w=writes[n];if(w.dstBinding<=2&&uint64_t(w.dstBinding)+w.descriptorCount>2){
            s->finalColors.erase(w.dstSet);
            if(!w.pNext&&!w.dstArrayElement&&w.descriptorCount==1&&w.dstBinding==2&&w.descriptorType==VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER&&w.pImageInfo)s->finalColors[w.dstSet]=*w.pImageInfo;
        }}
        for(uint32_t n=0;n<copies;++n){const auto& c=copy[n];if(c.dstBinding<=2&&uint64_t(c.dstBinding)+c.descriptorCount>2){
            const auto src=s->finalColors.find(c.srcSet);VkDescriptorImageInfo image{};if(src!=s->finalColors.end())image=src->second;s->finalColors.erase(c.dstSet);
            if(!c.pNext&&!c.srcArrayElement&&!c.dstArrayElement&&c.srcBinding==2&&c.dstBinding==2&&c.descriptorCount==1&&image.imageView)s->finalColors[c.dstSet]=image;
        }}
    }
    if(s->temporalUniforms)s->temporalUniforms->update(count,writes,copies,copy);
    observe(s,[&](auto&,auto& m){m.update(count,writes,copies,copy);});
    FN(vkUpdateDescriptorSets)(d,count,writes,copies,copy);
}
VKAPI_ATTR VkResult VKAPI_CALL graphics(VkDevice d,VkPipelineCache cache,uint32_t count,const VkGraphicsPipelineCreateInfo* infos,const VkAllocationCallbacks* a,VkPipeline* out){RESULT_BEGIN
    for(uint32_t j=0;j<count;++j)out[j]=VK_NULL_HANDLE;
    for(uint32_t j=0;j<count;++j){auto info=infos[j];std::vector<VkPipelineShaderStageCreateInfo> stages(info.pStages,info.pStages+info.stageCount);const auto seed=pipelineSeed(info);
        TemporalAuditOperation auditPipeline;
        if(s->audit&&s->auditPipelineLayouts[info.layout]){
            for(const auto& stage:stages){const auto& code=s->shaders.at(stage.module);const auto hash=profileHash(code.data(),uint32_t(code.size()*4));
                if(stage.stage==VK_SHADER_STAGE_VERTEX_BIT)auditPipeline.vertexHash=hash;
                if(stage.stage==VK_SHADER_STAGE_FRAGMENT_BIT)auditPipeline.fragmentHash=hash;}
            auditPipeline.layout=reinterpret_cast<uintptr_t>(info.layout);auditPipeline.subpass=info.subpass;
            if(info.pDynamicState)for(uint32_t k=0;k<info.pDynamicState->dynamicStateCount;++k){
                auditPipeline.dynamicViewport|=info.pDynamicState->pDynamicStates[k]==VK_DYNAMIC_STATE_VIEWPORT;
                auditPipeline.dynamicScissor|=info.pDynamicState->pDynamicStates[k]==VK_DYNAMIC_STATE_SCISSOR;}
            if(info.pViewportState){const auto& v=*info.pViewportState;
                if(!auditPipeline.dynamicViewport&&v.viewportCount==1&&v.pViewports){auditPipeline.viewport=*v.pViewports;auditPipeline.viewportKnown=true;}
                if(!auditPipeline.dynamicScissor&&v.scissorCount==1&&v.pScissors){auditPipeline.scissor=*v.pScissors;auditPipeline.scissorKnown=true;}}
        }
        // Correlate transparent material state with exact profile variants.
        // Creation-only and bounded: no string formatting on draw/record paths.
        if(kharvox::extendedDiagnosticsEnabled()&&s->materialDiagnostics<512
            &&info.pColorBlendState&&info.pColorBlendState->attachmentCount){
            bool blended=false;
            for(uint32_t k=0;k<info.pColorBlendState->attachmentCount;++k)
                blended|=info.pColorBlendState->pAttachments[k].blendEnable!=0;
            if(blended){
                ++s->materialDiagnostics;
                std::string signature;
                for(const auto& stage:stages){const auto& code=s->shaders.at(stage.module);
                    signature+=" stage"+std::to_string(stage.stage)+"="+shaderKey(profileHash(code.data(),uint32_t(code.size()*4)))
                        +"_"+shaderKey(profileHash(code.data(),uint32_t(code.size()*4),seed));}
                const auto* depth=info.pDepthStencilState;
                note("[SFS-MATERIAL] translucent subpass="+std::to_string(info.subpass)
                    +" depthTest="+std::to_string(depth?depth->depthTestEnable:0)
                    +" depthWrite="+std::to_string(depth?depth->depthWriteEnable:0)
                    +" depthCompare="+std::to_string(depth?depth->depthCompareOp:0)+signature);
                for(uint32_t k=0;k<info.pColorBlendState->attachmentCount;++k){const auto& b=info.pColorBlendState->pAttachments[k];
                    note("[SFS-MATERIAL] attachment="+std::to_string(k)+" blend="+std::to_string(b.blendEnable)
                        +" src="+std::to_string(b.srcColorBlendFactor)+" dst="+std::to_string(b.dstColorBlendFactor)
                        +" op="+std::to_string(b.colorBlendOp)+" mask="+std::to_string(b.colorWriteMask));}
            }
        }
        auto monoStages=stages;bool geometryApplied=false;
        for(size_t k=0;k<stages.size();++k){auto& stage=stages[k];const auto original=stage.module;
            const auto& code=s->shaders.at(original);const auto variant=profileHash(code.data(),uint32_t(code.size()*4),seed);bool compute{};
            stage.module=compiledModule(s,original,variant,compute,doomShadowProjection(info),-1,false,s->audit&&s->auditPipelineLayouts[info.layout]);
            geometryApplied|=s->geometryAuditModules.count(stage.module)!=0;
            monoStages[k].module=compiledModule(s,original,variant,compute,doomShadowProjection(info),-1,true);
        }
        VkShaderModule bridgeGuide{},bridgeResolve{};
        // Bound diagnostic residency to two private stereo image groups.
        if(s->bridgeEnabled&&s->bridges.size()<2&&s->bridgePasses[info.renderPass]&&s->bridgeLayouts.count(info.layout))try{
            for(uint32_t stageIndex=0;stageIndex<infos[j].stageCount;++stageIndex){const auto& original=infos[j].pStages[stageIndex];
                const auto& code=s->shaders.at(original.module);if(original.stage!=VK_SHADER_STAGE_FRAGMENT_BIT||profileHash(code.data(),uint32_t(code.size()*4))!=0x61d26e466ae9911aULL)continue;
                bool unused{};const auto variant=profileHash(code.data(),uint32_t(code.size()*4),seed);
                bridgeGuide=compiledModule(s,original.module,variant,unused,false,-1,false,false,1);bridgeResolve=compiledModule(s,original.module,variant,unused,false,-1,false,false,2);
            }
        }catch(const std::exception& e){note(std::string("[TEMPORAL-BRIDGE] shader rejected: ")+e.what());bridgeGuide=bridgeResolve={};}
        VkShaderModule finalCopy{},finalResolve{};
#ifdef KHARVOX_HAVE_RECONSTRUCTION
        // Pipeline creation may use a compatibility-only pass, later destroyed
        // by DOOM. Admission of the actual LOAD/STORE pass happens at the draw.
        if((s->reconstruction||overlay::enabled())&&s->finalBridges.size()<4&&s->bridgeLayouts.count(info.layout))try{
            for(const auto& original:std::vector<VkPipelineShaderStageCreateInfo>(infos[j].pStages,infos[j].pStages+infos[j].stageCount)){
                const auto& code=s->shaders.at(original.module);if(original.stage!=VK_SHADER_STAGE_FRAGMENT_BIT||!s->finalShaderHashes.count(profileHash(code.data(),uint32_t(code.size()*4))))continue;
                bool unused{};const auto variant=profileHash(code.data(),uint32_t(code.size()*4),seed);
                finalCopy=compiledModule(s,original.module,variant,unused,false,-1,false,false,3);finalResolve=compiledModule(s,original.module,variant,unused,false,-1,false,false,4);
            }
        }catch(const std::exception& e){note(std::string("[FINAL-BRIDGE] shader rejected: ")+e.what());finalCopy=finalResolve={};}
#endif
        info.pStages=monoStages.data();
        // Derivative batch indices must not escape their original batch.
        if(info.flags&VK_PIPELINE_CREATE_DERIVATIVE_BIT)throw std::runtime_error("SFS derivative pipelines need batch remapping");
        VkResult r;{UnlockedDriverScope unlocked(lock);r=FN(vkCreateGraphicsPipelines)(d,cache,1,&info,a,&out[j]);}if(r!=VK_SUCCESS)return r;
        info.pStages=stages.data();info.renderPass=s->passes.at(info.renderPass);VkPipeline stereo{};{UnlockedDriverScope unlocked(lock);r=FN(vkCreateGraphicsPipelines)(d,cache,1,&info,a,&stereo);}if(r!=VK_SUCCESS){FN(vkDestroyPipeline)(d,out[j],a);out[j]=VK_NULL_HANDLE;return r;}s->stereoPipelines[out[j]]=stereo;contextPipeline(s,out[j],stereo,infos[j]);
        if(bridgeGuide&&bridgeResolve)try{
            auto bridge=std::make_unique<DoomTemporalBridge>(d,s->bridgeDispatch,s->bridgeMemory);
            VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layout.setLayoutCount=1;layout.pSetLayouts=&s->bridgeLayouts.at(info.layout);
            bridge->initialize(info,layout,bridgeGuide,bridgeResolve);s->bridges[out[j]]=std::move(bridge);
            note("[TEMPORAL-BRIDGE] private color/depth/motion and stereo resolve ready; NGX evaluations OFF");
        }catch(const std::exception& e){note(std::string("[TEMPORAL-BRIDGE] pipeline rejected: ")+e.what());}
        if(finalCopy&&finalResolve)try{
            auto bridge=std::make_unique<DoomFinalBridge>(d,s->bridgeDispatch,s->bridgeMemory);
            VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layout.setLayoutCount=1;layout.pSetLayouts=&s->bridgeLayouts.at(info.layout);
            bridge->initialize(info,layout,finalCopy,finalResolve);s->finalBridges[out[j]]=std::move(bridge);
            note("[FINAL-BRIDGE] post-tonemap reconstruction with separate GUI ready");
        }catch(const std::exception& e){note(std::string("[FINAL-BRIDGE] pipeline rejected: ")+e.what());}
        if(s->audit&&(s->audit->geometryMode()?geometryApplied:auditPipeline.fragmentHash==0x61d26e466ae9911aULL)){auditPipeline.pipeline=reinterpret_cast<uintptr_t>(out[j]);s->auditPipelines[out[j]]=std::move(auditPipeline);}
    }return VK_SUCCESS;
RESULT_END}
VKAPI_ATTR VkResult VKAPI_CALL compute(VkDevice d,VkPipelineCache cache,uint32_t count,const VkComputePipelineCreateInfo* infos,const VkAllocationCallbacks* a,VkPipeline* out){RESULT_BEGIN
    for(uint32_t j=0;j<count;++j)out[j]=VK_NULL_HANDLE;
    for(uint32_t j=0;j<count;++j){
        auto info=infos[j];const auto original=info.stage.module;bool stereo{};
        if(info.flags&VK_PIPELINE_CREATE_DERIVATIVE_BIT)return VK_ERROR_FEATURE_NOT_PRESENT;
        info.stage.module=compiledModule(s,original,0,stereo);
        const auto& code=s->shaders.at(original);
        const bool indirect=stereo&&!loadProfileShader(s->profile,profileHash(code.data(),uint32_t(code.size()*4)),0,VK_SHADER_STAGE_COMPUTE_BIT);
        std::array<VkShaderModule,2> modules{};
        if(indirect)for(int eye=0;eye<2;++eye){bool ignored{};modules[eye]=compiledModule(s,original,0,ignored,false,eye);}
        VkResult r;{UnlockedDriverScope unlocked(lock);r=FN(vkCreateComputePipelines)(d,cache,1,&info,a,&out[j]);}if(r!=VK_SUCCESS)return r;
        std::array<VkPipeline,2> eyes{};
        if(indirect)for(int eye=0;eye<2;++eye){info.stage.module=modules[eye];{UnlockedDriverScope unlocked(lock);r=FN(vkCreateComputePipelines)(d,cache,1,&info,a,&eyes[eye]);}
            if(r!=VK_SUCCESS){for(auto p:eyes)if(p)FN(vkDestroyPipeline)(d,p,a);FN(vkDestroyPipeline)(d,out[j],a);out[j]=VK_NULL_HANDLE;return r;}}
        if(indirect)s->indirectPipelines[out[j]]=eyes;
        s->computeStereo[out[j]]=stereo;
        observe(s,[&](auto& c,auto&){c.define("pipeline",tc::handle(out[j]),"{\"point\":1,\"layout\":"+std::to_string(tc::handle(infos[j].layout))+",\"hash\":"+tc::quote(shaderKey(profileHash(code.data(),uint32_t(code.size()*4))))+",\"stereo\":"+(stereo?"true":"false")+"}");if(infos[j].pNext||infos[j].stage.pNext||infos[j].stage.pSpecializationInfo)c.gap("compute-pipeline-extension-or-specialization");});
    }return VK_SUCCESS;
RESULT_END}
VKAPI_ATTR void VKAPI_CALL destroyPipeline(VkDevice d,VkPipeline pipeline,const VkAllocationCallbacks* a){auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);if(s->bridges.count(pipeline)||s->finalBridges.count(pipeline)){if(FN(vkDeviceWaitIdle)(d)!=VK_SUCCESS)commandFailure("Temporal bridge retirement failed");dlssIdle(s);s->bridges.erase(pipeline);s->finalBridges.erase(pipeline);}auto it=s->stereoPipelines.find(pipeline);if(it!=s->stereoPipelines.end()){FN(vkDestroyPipeline)(d,it->second,a);s->stereoPipelines.erase(it);}auto indirect=s->indirectPipelines.find(pipeline);if(indirect!=s->indirectPipelines.end()){for(auto eye:indirect->second)FN(vkDestroyPipeline)(d,eye,a);s->indirectPipelines.erase(indirect);}s->computeStereo.erase(pipeline);s->auditPipelines.erase(pipeline);observe(s,[&](auto& c,auto&){c.forget("pipeline",tc::handle(pipeline));});FN(vkDestroyPipeline)(d,pipeline,a);}
VKAPI_ATTR VkResult VKAPI_CALL beginCommand(VkCommandBuffer cb,const VkCommandBufferBeginInfo* i){try{auto s=state(cb);std::shared_lock<std::shared_mutex> lock(s->mutex);if(i->pInheritanceInfo&&i->pInheritanceInfo->renderPass)return VK_ERROR_FEATURE_NOT_PRESENT;
    const auto result=FN(vkBeginCommandBuffer)(cb,i);if(result!=VK_SUCCESS)return result;dlssDiscarded(s,cb);
    auto& command=commandOf(s,cb);command.recordingSupported=!i->pNext&&!(i->flags&~VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);if(s->audit)s->audit->releaseOperations(command.auditOperations);++command.auditGeneration;command.auditDraw=0;command.auditPass=VK_NULL_HANDLE;command.auditFramebuffer=VK_NULL_HANDLE;
    observe(s,[&](auto& c,auto&){c.define("command",tc::handle(cb),"{"+contextCommand(command,cb)+"}");contextEvent(s,cb,"beginCommand","\"flags\":"+std::to_string(i->flags));if(i->pNext||i->pInheritanceInfo)c.gap("command-inheritance-or-extension");});
    command.stereo=false;command.compute=VK_NULL_HANDLE;command.graphics=VK_NULL_HANDLE;for(auto& descriptor:command.descriptors)descriptor.set=VK_NULL_HANDLE;command.computeDescriptors.clear();command.bindings.clear();command.pushes.clear();return result;}catch(const std::exception& e){note(e.what());return VK_ERROR_INITIALIZATION_FAILED;}}
VKAPI_ATTR VkResult VKAPI_CALL endCommand(VkCommandBuffer cb){try{auto s=state(cb);std::shared_lock<std::shared_mutex> lock(s->mutex);
    const auto result=FN(vkEndCommandBuffer)(cb);observe(s,[&](auto&,auto&){contextEvent(s,cb,"endCommand","\"result\":"+std::to_string(result));});return result;
}catch(const std::exception& e){note(e.what());return VK_ERROR_INITIALIZATION_FAILED;}}
VKAPI_ATTR VkResult VKAPI_CALL allocateCommands(VkDevice d,const VkCommandBufferAllocateInfo* i,VkCommandBuffer* out){RESULT_BEGIN
    auto r=FN(vkAllocateCommandBuffers)(d,i,out);if(r==VK_SUCCESS)for(uint32_t j=0;j<i->commandBufferCount;++j){s->commandPools[out[j]]=i->commandPool;s->commands.try_emplace(out[j]);s->commands.at(out[j]).primary=i->level==VK_COMMAND_BUFFER_LEVEL_PRIMARY;s->commands.at(out[j]).family=s->poolFamilies.count(i->commandPool)?s->poolFamilies.at(i->commandPool):UINT32_MAX;observe(s,[&](auto& c,auto&){auto& command=s->commands.at(out[j]);command.contextAllocation=c.define("command",tc::handle(out[j]),"{\"pool\":"+std::to_string(tc::handle(i->commandPool))+",\"level\":"+std::to_string(i->level)+"}");});}return r;
RESULT_END}
VKAPI_ATTR void VKAPI_CALL freeCommands(VkDevice d,VkCommandPool pool,uint32_t count,const VkCommandBuffer* commands){auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);for(uint32_t j=0;j<count;++j){if(s->audit)s->audit->releaseOperations(s->commands.at(commands[j]).auditOperations);observe(s,[&](auto& c,auto&){c.forget("command",tc::handle(commands[j]));});s->commands.erase(commands[j]);s->commandPools.erase(commands[j]);}CommandLookupCache::invalidate();FN(vkFreeCommandBuffers)(d,pool,count,commands);for(uint32_t j=0;j<count;++j)dlssDiscarded(s,commands[j]);}
VKAPI_ATTR void VKAPI_CALL destroyCommandPool(VkDevice d,VkCommandPool pool,const VkAllocationCallbacks* a){auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);std::vector<VkCommandBuffer> discarded;for(auto it=s->commandPools.begin();it!=s->commandPools.end();)if(it->second==pool){if(s->audit)s->audit->releaseOperations(s->commands.at(it->first).auditOperations);observe(s,[&](auto& c,auto&){c.forget("command",tc::handle(it->first));});discarded.push_back(it->first);s->commands.erase(it->first);it=s->commandPools.erase(it);}else ++it;CommandLookupCache::invalidate();FN(vkDestroyCommandPool)(d,pool,a);s->poolFamilies.erase(pool);for(auto cb:discarded)dlssDiscarded(s,cb);}
VKAPI_ATTR VkResult VKAPI_CALL resetCommands(VkDevice d,VkCommandPool pool,VkCommandPoolResetFlags flags){RESULT_BEGIN
    const auto result=FN(vkResetCommandPool)(d,pool,flags);if(result==VK_SUCCESS)for(const auto& entry:s->commandPools)if(entry.second==pool)dlssDiscarded(s,entry.first);if(result==VK_SUCCESS&&s->audit)
        for(const auto& entry:s->commandPools)if(entry.second==pool){s->audit->releaseOperations(s->commands.at(entry.first).auditOperations);contextEvent(s,entry.first,"resetCommand","");}
    return result;
RESULT_END}
VKAPI_ATTR VkResult VKAPI_CALL resetCommand(VkCommandBuffer cb,VkCommandBufferResetFlags flags){
    auto s=state(cb);std::shared_lock<std::shared_mutex> lock(s->mutex);
    const auto result=FN(vkResetCommandBuffer)(cb,flags);if(result==VK_SUCCESS)dlssDiscarded(s,cb);if(result==VK_SUCCESS&&s->audit){s->audit->releaseOperations(commandOf(s,cb).auditOperations);contextEvent(s,cb,"resetCommand","");}return result;
}
void bindGraphics(const std::shared_ptr<State>& s,VkCommandBuffer cb){
    auto& command=commandOf(s,cb);if(!command.graphics)return;
    auto found=s->stereoPipelines.find(command.graphics);if(found==s->stereoPipelines.end())throw std::runtime_error("Untracked SFS graphics pipeline");
    FN(vkCmdBindPipeline)(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,command.stereo?found->second:command.graphics);
}
VKAPI_ATTR void VKAPI_CALL bindPipeline(VkCommandBuffer cb,VkPipelineBindPoint point,VkPipeline pipeline){COMMAND_BEGIN
    auto& command=commandOf(s,cb);if(point==VK_PIPELINE_BIND_POINT_COMPUTE){observe(s,[&](auto&,auto&){contextEvent(s,cb,"bindCompute","\"pipeline\":"+std::to_string(tc::handle(pipeline)));});command.compute=pipeline;FN(vkCmdBindPipeline)(cb,point,pipeline);return;}
    command.graphics=pipeline;bindGraphics(s,cb);
COMMAND_END}
VKAPI_ATTR void VKAPI_CALL bindSets(VkCommandBuffer cb,VkPipelineBindPoint point,VkPipelineLayout layout,uint32_t first,uint32_t count,const VkDescriptorSet* sets,uint32_t dynamicCount,const uint32_t* dynamic){COMMAND_BEGIN
    auto& command=commandOf(s,cb);auto& descriptors=point==VK_PIPELINE_BIND_POINT_COMPUTE?command.computeDescriptors:command.descriptors;
    if(descriptors.size()<size_t(first)+count)descriptors.resize(size_t(first)+count);
    std::vector<uint32_t> forwarded;
    uint32_t offset=0;for(uint32_t j=0;j<count;++j){auto set=sets[j];const auto n=s->setDynamicCounts.at(set);if(n>dynamicCount-offset)throw std::runtime_error("SFS dynamic descriptor offset mismatch");
        {auto& cached=descriptors[first+j];cached.layout=layout;cached.set=set;cached.dynamic.clear();if(n)cached.dynamic.assign(dynamic+offset,dynamic+offset+n);}
        if(s->audit){if(n)forwarded.insert(forwarded.end(),dynamic+offset,dynamic+offset+n);if(s->auditSetDynamic.count(set))forwarded.push_back(0);}offset+=n;}
    if(offset!=dynamicCount)throw std::runtime_error("SFS unexpected dynamic descriptor offsets");
    observe(s,[&](auto& c,auto&){if(!c.active())return;std::vector<uint64_t> ids;for(uint32_t n=0;n<count;++n)ids.push_back(tc::handle(sets[n]));contextEvent(s,cb,"bindSets","\"point\":"+std::to_string(point)+",\"layout\":"+std::to_string(tc::handle(layout))+",\"first\":"+std::to_string(first)+",\"sets\":"+tc::numbers(ids.data(),ids.size())+",\"offsets\":"+tc::numbers(dynamic,dynamicCount));});
    FN(vkCmdBindDescriptorSets)(cb,point,layout,first,count,sets,s->audit?uint32_t(forwarded.size()):dynamicCount,s->audit?forwarded.data():dynamic);
COMMAND_END}
VKAPI_ATTR void VKAPI_CALL bindVertices(VkCommandBuffer cb,uint32_t first,uint32_t count,const VkBuffer* buffers,const VkDeviceSize* offsets){COMMAND_BEGIN
    auto& bindings=commandOf(s,cb).bindings;for(uint32_t j=0;j<count;++j)CommandBindings::set(bindings.vertices,first+j,CommandBindings::Vertex{buffers[j],offsets[j]});FN(vkCmdBindVertexBuffers)(cb,first,count,buffers,offsets);
COMMAND_END}
VKAPI_ATTR void VKAPI_CALL bindIndex(VkCommandBuffer cb,VkBuffer buffer,VkDeviceSize offset,VkIndexType type){COMMAND_BEGIN
    commandOf(s,cb).bindings.index={buffer,offset,type};FN(vkCmdBindIndexBuffer)(cb,buffer,offset,type);
COMMAND_END}
VKAPI_ATTR void VKAPI_CALL viewport(VkCommandBuffer cb,uint32_t first,uint32_t count,const VkViewport* values){COMMAND_BEGIN
    auto& bindings=commandOf(s,cb).bindings;for(uint32_t j=0;j<count;++j)CommandBindings::set(bindings.viewports,first+j,values[j]);FN(vkCmdSetViewport)(cb,first,count,values);
COMMAND_END}
VKAPI_ATTR void VKAPI_CALL scissor(VkCommandBuffer cb,uint32_t first,uint32_t count,const VkRect2D* values){COMMAND_BEGIN
    auto& bindings=commandOf(s,cb).bindings;for(uint32_t j=0;j<count;++j)CommandBindings::set(bindings.scissors,first+j,values[j]);FN(vkCmdSetScissor)(cb,first,count,values);
COMMAND_END}
VKAPI_ATTR void VKAPI_CALL push(VkCommandBuffer cb,VkPipelineLayout layout,VkShaderStageFlags flags,uint32_t offset,uint32_t size,const void* values){COMMAND_BEGIN
    observe(s,[&](auto& c,auto&){if(!c.active())return;std::vector<uint32_t> words(size/4);if(size)std::memcpy(words.data(),values,size);contextEvent(s,cb,"pushConstants","\"layout\":"+std::to_string(tc::handle(layout))+",\"stages\":"+std::to_string(flags)+",\"offset\":"+std::to_string(offset)+",\"words\":"+tc::numbers(words.data(),words.size()));});commandOf(s,cb).pushes.write(layout,flags,offset,size,values);FN(vkCmdPushConstants)(cb,layout,flags,offset,size,values);
COMMAND_END}
void replayBindings(const std::shared_ptr<State>& s,VkCommandBuffer cb){
    bindGraphics(s,cb);auto& command=commandOf(s,cb);
    for(uint32_t index=0;index<command.descriptors.size();++index){const auto& binding=command.descriptors[index];if(binding.set){
        if(s->auditSetDynamic.count(binding.set)){auto offsets=binding.dynamic;offsets.push_back(0);
            FN(vkCmdBindDescriptorSets)(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,binding.layout,index,1,&binding.set,uint32_t(offsets.size()),offsets.data());}
        else FN(vkCmdBindDescriptorSets)(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,binding.layout,index,1,&binding.set,uint32_t(binding.dynamic.size()),binding.dynamic.data());}}
    command.bindings.replay(s->dispatch,cb);
    command.pushes.replay([&](VkPipelineLayout layout,VkShaderStageFlags flags,uint32_t offset,uint32_t size,const void* values){FN(vkCmdPushConstants)(cb,layout,flags,offset,size,values);});
}
void replayComputeBindings(const std::shared_ptr<State>& s,VkCommandBuffer cb){
    auto& command=commandOf(s,cb);if(command.compute)FN(vkCmdBindPipeline)(cb,VK_PIPELINE_BIND_POINT_COMPUTE,command.compute);
    for(uint32_t n=0;n<command.computeDescriptors.size();++n){const auto& b=command.computeDescriptors[n];if(b.set)
        FN(vkCmdBindDescriptorSets)(cb,VK_PIPELINE_BIND_POINT_COMPUTE,b.layout,n,1,&b.set,uint32_t(b.dynamic.size()),b.dynamic.data());}
    command.pushes.replay([&](VkPipelineLayout layout,VkShaderStageFlags flags,uint32_t offset,uint32_t size,const void* values){if(flags&VK_SHADER_STAGE_COMPUTE_BIT)FN(vkCmdPushConstants)(cb,layout,flags,offset,size,values);});
}
VKAPI_ATTR void VKAPI_CALL beginPass(VkCommandBuffer cb,const VkRenderPassBeginInfo* i,VkSubpassContents contents){COMMAND_BEGIN
    auto info=*i;auto& command=commandOf(s,cb);command.passDraws=0;command.bridgeInline=contents==VK_SUBPASS_CONTENTS_INLINE;command.bridgeBegin=*i;command.stereo=s->framebufferStereo.at(i->framebuffer);if(s->mixedFramebuffers.at(i->framebuffer))s->mixedPasses.fetch_add(1,std::memory_order_relaxed);if(command.stereo)info.renderPass=s->passes.at(i->renderPass);FN(vkCmdBeginRenderPass)(cb,&info,contents);replayBindings(s,cb);
    // Qualified bridge passes have LOAD/DONT_CARE attachments, never CLEAR.
    // DOOM still supplies unused clear values; don't retain their caller-owned
    // pointer or reject this otherwise compatible pass at the later draw.
    if(s->bridgeEnabled&&(s->bridgePasses.at(i->renderPass)||s->finalPasses.at(i->renderPass))){command.bridgeBegin.clearValueCount=0;command.bridgeBegin.pClearValues=nullptr;}
    command.bridgeBegin.renderPass=info.renderPass;command.auditPass=i->renderPass;command.auditFramebuffer=i->framebuffer;command.auditArea=i->renderArea;command.auditSubpass=0;observe(s,[&](auto&,auto&){contextEvent(s,cb,"beginPass","\"pass\":"+std::to_string(tc::handle(i->renderPass))+",\"actualPass\":"+std::to_string(tc::handle(info.renderPass))+",\"framebuffer\":"+std::to_string(tc::handle(i->framebuffer))+",\"area\":"+tc::rect(i->renderArea));});
COMMAND_END}
VKAPI_ATTR void VKAPI_CALL endPass(VkCommandBuffer cb){COMMAND_BEGIN contextEvent(s,cb,"endPass","");FN(vkCmdEndRenderPass)(cb);if(s->audit)s->audit->hostBarrier(cb);commandOf(s,cb).stereo=false;commandOf(s,cb).auditPass=VK_NULL_HANDLE;COMMAND_END}
VKAPI_ATTR void VKAPI_CALL nextPass(VkCommandBuffer cb,VkSubpassContents contents){COMMAND_BEGIN contextEvent(s,cb,"nextPass","");FN(vkCmdNextSubpass)(cb,contents);++commandOf(s,cb).auditSubpass;replayBindings(s,cb);COMMAND_END}
void auditDraw(const std::shared_ptr<State>& s,VkCommandBuffer cb,const char* kind){
    auto& c=commandOf(s,cb);++c.auditDraw;contextDraw(s,cb,kind);
    if(!s->audit||s->audit->finished()||!c.stereo||!c.auditPass||c.descriptors.empty()||!c.descriptors[0].set)return;
    const auto pipeline=s->auditPipelines.find(c.graphics);if(pipeline==s->auditPipelines.end())return;
    const auto& binding=c.descriptors[0];const auto dynamic=s->auditSetDynamic.find(binding.set);if(dynamic==s->auditSetDynamic.end())return;
    auto operation=pipeline->second;operation.kind=kind;operation.recordedFrame=s->renderPose.serial;
    operation.command=reinterpret_cast<uintptr_t>(cb);operation.generation=c.auditGeneration;operation.draw=c.auditDraw;
    operation.set=reinterpret_cast<uintptr_t>(binding.set);operation.dynamicBindings=dynamic->second;operation.dynamicOffsets=binding.dynamic;
    operation.renderPass=reinterpret_cast<uintptr_t>(c.auditPass);operation.framebuffer=reinterpret_cast<uintptr_t>(c.auditFramebuffer);
    operation.subpass=c.auditSubpass;operation.renderArea=c.auditArea;
    if(operation.dynamicViewport&&c.bindings.viewports.size()==1&&c.bindings.viewports[0].valid){operation.viewport=c.bindings.viewports[0].value;operation.viewportKnown=true;}
    if(operation.dynamicScissor&&c.bindings.scissors.size()==1&&c.bindings.scissors[0].valid){operation.scissor=c.bindings.scissors[0].value;operation.scissorKnown=true;}
    const auto views=s->auditFramebufferViews.find(c.auditFramebuffer);
    if(views!=s->auditFramebufferViews.end()){std::ostringstream attachments;attachments<<'[';
        for(size_t i=0;i<views->second.size();++i){VkDescriptorImageInfo image{};image.imageView=views->second[i];attachments<<(i?",":"")<<auditResource(s,image);}attachments<<']';operation.attachments=attachments.str();}
    auto offsets=binding.dynamic;offsets.push_back(s->audit->geometryMode()&&std::strcmp(kind,"draw")&&std::strcmp(kind,"drawIndexed")?0:s->audit->operation(binding.set,operation,c.auditOperations));
    // Binding 32 is last in the recognized layout. Original UBO offsets are
    // preserved. The pipeline layout and game push constants are unchanged.
    FN(vkCmdBindDescriptorSets)(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,binding.layout,0,1,&binding.set,uint32_t(offsets.size()),offsets.data());
}
template<class Draw> bool bridgeDraw(const std::shared_ptr<State>& s,VkCommandBuffer cb,bool direct,Draw&& draw){
    auto& c=commandOf(s,cb);const bool first=c.passDraws++==0;
    if(s->controlsDraining)return false;
#ifdef KHARVOX_HAVE_RECONSTRUCTION
    // A final-scene consumer can still own the guide textures from an earlier
    // primary buffer. Do not overwrite them while its work is outstanding.
    {std::lock_guard<std::mutex> lock(s->dlssMutex);
        if(s->reconstruction&&overlay::reconstruction()&&s->reconstruction->pendingCommand())return false;}
#endif
    if(!s->bridgeEnabled||(overlay::enabled()&&overlay::mode()<=1))return false;
    const auto bridge=s->bridges.find(c.graphics);if(bridge==s->bridges.end())return false;
#ifdef KHARVOX_HAVE_RECONSTRUCTION
    {std::lock_guard<std::mutex> lock(s->dlssMutex);
        if(s->reconstruction&&overlay::reconstruction()&&s->reconstruction->pendingGuides())return false;}
#endif
    const auto skip=[&](unsigned bit,const char* reason){{std::lock_guard<std::mutex> status(s->statusMutex);s->statusError=reason;}const auto mask=1u<<bit;if(!(s->bridgeSkipReasons.fetch_or(mask)&mask))note(std::string("[TEMPORAL-BRIDGE] skipped: ")+reason);return false;};
    if(!first||!direct||!c.stereo||!c.bridgeInline||c.auditSubpass||c.descriptors.size()!=1||!c.descriptors[0].set||!s->frameValid)return skip(0,"draw/frame shape not qualified");
    const auto extent=s->bridgeExtents.find(c.auditFramebuffer);if(extent==s->bridgeExtents.end()||!s->bridgePasses.at(c.auditPass))return skip(1,"framebuffer/pass not qualified");
    if(c.bindings.viewports.size()!=1||!c.bindings.viewports[0].valid||c.bindings.scissors.size()!=1||!c.bindings.scissors[0].valid)return skip(2,"viewport/scissor state missing");
    const auto color=s->bridgeColors.find(c.descriptors[0].set);if(color==s->bridgeColors.end()||color->second.imageLayout!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)return skip(3,"HDR descriptor missing or layout unqualified");
    const auto view=s->viewInfos.find(color->second.imageView);if(view==s->viewInfos.end())return skip(4,"HDR view metadata missing");
    const auto& v=view->second;const auto image=s->auditImages.find(v.image);if(image==s->auditImages.end())return skip(5,"HDR image metadata missing");
    if(v.format!=VK_FORMAT_B10G11R11_UFLOAT_PACK32||v.viewType!=VK_IMAGE_VIEW_TYPE_2D_ARRAY||v.subresourceRange.baseArrayLayer||v.subresourceRange.layerCount!=2||v.subresourceRange.baseMipLevel||v.subresourceRange.levelCount!=1
        ||image->second.extent.width!=extent->second.width||image->second.extent.height!=extent->second.height||image->second.samples!=VK_SAMPLE_COUNT_1_BIT)return skip(6,"HDR format/range/extent mismatch");
    const auto outputs=s->auditFramebufferViews.find(c.auditFramebuffer);if(outputs==s->auditFramebufferViews.end()||outputs->second.size()!=2)return skip(7,"output attachments missing");
    for(auto out:outputs->second){const auto ov=s->viewInfos.find(out);if(ov==s->viewInfos.end()||ov->second.image==v.image)return skip(8,"output metadata missing or HDR alias");}
    bool scaledRoute=false;
#ifdef KHARVOX_HAVE_RECONSTRUCTION
    scaledRoute=bool(s->reconstruction)&&overlay::reconstruction();
#endif
    const auto& activeViewport=c.bindings.viewports[0].value;
    VkExtent2D activeExtent=extent->second;
    if(scaledRoute){
        if(!std::isfinite(activeViewport.width)||!std::isfinite(activeViewport.height)||activeViewport.width<32||activeViewport.height<32||activeViewport.width>extent->second.width||activeViewport.height>extent->second.height||std::floor(activeViewport.width)!=activeViewport.width||std::floor(activeViewport.height)!=activeViewport.height)return skip(10,"active rectangle invalid");
        activeExtent={uint32_t(activeViewport.width),uint32_t(activeViewport.height)};
    }
    std::optional<DoomTemporalParameters> parameters;uint64_t parameterTime{};
    if(s->temporalUniforms){DoomTemporalUniformSnapshot snapshot;
        const bool read=s->temporalUniforms->read(c.descriptors[0].set,c.descriptors[0].dynamic,snapshot);
        const auto n=read?++s->temporalReads:++s->temporalReadMisses;
        if(read&&scaledRoute){
            const auto raster=DoomTemporalParameters::renderExtent(snapshot,extent->second);
            if(!raster)return skip(10,"engine scene resolution scale invalid");
            activeExtent=*raster;
            // Respect the engine's actual clipped raster when the fractional
            // scene dimensions round down at the render-area/scissor boundary.
            const auto& sc=c.bindings.scissors[0].value;
            if(sc.offset.x||sc.offset.y||c.bridgeBegin.renderArea.offset.x||c.bridgeBegin.renderArea.offset.y)return skip(10,"scaled scene origin unsupported");
            activeExtent.width=std::min({activeExtent.width,sc.extent.width,c.bridgeBegin.renderArea.extent.width});
            activeExtent.height=std::min({activeExtent.height,sc.extent.height,c.bridgeBegin.renderArea.extent.height});
        }
        {std::lock_guard<std::mutex> guard(s->temporalSnapshotMutex);s->temporalSnapshotSerial=read?s->renderPose.serial:0;if(read)s->temporalSnapshot=snapshot;
            s->temporalParameters.reset();doom_temporal::ViewSnapshot matched;
            if(read&&s->temporalViews&&s->temporalViews->find([&](const auto& candidate){return DoomTemporalParameters::currentMatches(candidate,snapshot,activeExtent,extent->second);},matched)){
                const auto& vp=c.bindings.viewports[0].value;
                s->temporalParameters=DoomTemporalParameters::derive(matched,s->temporalPrevious?&*s->temporalPrevious:nullptr,snapshot,s->renderUniforms,activeExtent,{vp.x,vp.y,float(activeExtent.width),float(activeExtent.height),vp.minDepth,vp.maxDepth},extent->second);
                if(s->temporalParameters){s->temporalPrevious=matched;parameters=s->temporalParameters;parameterTime=matched.timeNs;}else s->temporalPrevious.reset();
            }else s->temporalPrevious.reset();
            // Capture the failing draw itself. Sampling only n%120 aliases the
            // eight-phase jitter cycle and misses a persistent phase-7 failure.
            if(read&&!parameters&&overlay::enabled()&&n>=s->temporalFailureNextRead&&s->temporalFailureFiles<12){
                s->temporalFailureNextRead=n+120;
                const auto number=++s->temporalFailureFiles;
                const auto path=overlay::root()/(L"temporal-failure-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(number)+L".json");
                const auto candidates=s->temporalViews?s->temporalViews->recent():std::vector<doom_temporal::ViewSnapshot>{};
                std::ofstream file(path,std::ios::binary|std::ios::trunc);
                file<<temporalFailureJson(s->renderPose.serial,activeExtent,extent->second,snapshot,s->renderUniforms,candidates);
                note("[TEMPORAL-FAILURE] bound uniforms/camera evidence="+path.string()+" candidates="+std::to_string(candidates.size()));
            }
            if(n<=3||n%120==0)note(std::string("[TEMPORAL-PARAMETERS] count=")+std::to_string(n)+" mapped="+std::to_string(read)+" current="+std::to_string(bool(s->temporalParameters))+" previous="+std::to_string(s->temporalParameters&&s->temporalParameters->previousMatches)+" phase="+(s->temporalParameters?std::to_string(s->temporalParameters->phase):"unavailable")+"; coordinate observation only");
        }
    }
    DoomTemporalBridge::Processor processor;
#ifdef KHARVOX_HAVE_DLSS_STARTUP
    if(s->dlss&&(!overlay::enabled()||overlay::dlaa()))processor=[&](VkCommandBuffer command,const DoomTemporalBridge::Inputs& inputs){
        std::lock_guard<std::mutex> lock(s->dlssMutex);
        if(!parameters||!c.primary||!c.recordingSupported||c.family!=s->dlss->family()){s->dlss->gap();return std::array<VkImageView,2>{};}
        const auto before=s->dlss->evaluations();std::array<VkImageView,2> output;
        try{output=s->dlss->record(command,inputs,*parameters,s->renderPose.serial,parameterTime,1.f);}
        catch(...){replayComputeBindings(s,command);throw;}
        replayComputeBindings(s,command);
        if(s->dlss->pendingCommand()==command)s->dlssPipeline=c.graphics;
        const auto after=s->dlss->evaluations();
        if(after!=before&&(after<=24||after%240==0))note("[DLSS-LIVE] recorded eye evaluations="+std::to_string(after)+" stereo="+std::to_string(bool(output[0]&&output[1]))+" frame="+std::to_string(s->renderPose.serial)+" resetEvaluations="+std::to_string(s->dlss->resetEvaluations())+"; experimental raw HDR preExposure=1");
        if(!s->dlss->error().empty()&&(s->bridgeDraws.load()<3||s->bridgeDraws.load()%120==0))note("[DLSS-LIVE] fallback: "+s->dlss->error());return output;
    };
#endif
#ifdef KHARVOX_HAVE_RECONSTRUCTION
    if(s->reconstruction&&overlay::reconstruction())processor=[&](VkCommandBuffer command,const DoomTemporalBridge::Inputs& inputs){
        std::lock_guard<std::mutex> lock(s->dlssMutex);
        if(!parameters||!c.primary||!c.recordingSupported||c.family!=s->reconstruction->family())s->reconstruction->gap();
        else s->reconstruction->capture(command,inputs,*parameters,s->renderPose.serial,parameterTime,extent->second,s->renderPose.views,s->renderPose.displayPeriod);
        return std::array<VkImageView,2>{};
    };
    // A scaled guide needs the engine's validated active-rectangle contract.
    if(scaledRoute&&!parameters)return skip(11,"scaled temporal parameters unavailable");
#endif
    const bool recorded=bridge->second->record(cb,c.bridgeBegin,activeExtent,c.bindings.viewports[0].value,c.bindings.scissors[0].value,[&]{replayBindings(s,cb);},draw,processor,scaledRoute);
    if(recorded){const auto n=++s->bridgeDraws;if(n<=3||n%120==0)note("[TEMPORAL-BRIDGE] transported stereo pair="+std::to_string(n)+" size="+std::to_string(extent->second.width)+"x"+std::to_string(extent->second.height)+"; color/depth/motion private; original packed output retained");}
    else{const auto failure=bridge->second->takeFailure();if(!failure.empty())note("[TEMPORAL-BRIDGE] disabled, original TAA retained: "+failure);else skip(9,"private resource already used or render area outside qualified bounds");}
    return recorded;
}
template<class Draw>bool finalDraw(const std::shared_ptr<State>& s,VkCommandBuffer cb,bool direct,Draw&& draw){
    if(s->controlsDraining)return false;
#ifdef KHARVOX_HAVE_RECONSTRUCTION
    if(!s->reconstruction||!overlay::reconstruction())return false;
    auto& c=commandOf(s,cb);const auto bridge=s->finalBridges.find(c.graphics);if(bridge==s->finalBridges.end())return false;
    const auto skip=[&](const char* why){
        bool first;{std::lock_guard<std::mutex> status(s->statusMutex);s->statusError=why;first=s->finalSkipReasons.insert(why).second;}
        if(first)note(std::string("[FINAL-BRIDGE] fallback: ")+why);return false;
    };
    if(c.passDraws!=1||!direct||!c.stereo||!c.primary||!c.recordingSupported||!c.bridgeInline||c.auditSubpass||c.descriptors.size()!=1||!c.descriptors[0].set||!s->frameValid||c.family!=s->reconstruction->family())return skip("draw not qualified");
    const auto extent=s->bridgeExtents.find(c.auditFramebuffer);
    if(extent==s->bridgeExtents.end()||!s->finalPasses.at(c.auditPass))return skip("pass/framebuffer unavailable");
    if(c.bindings.viewports.size()!=1||!c.bindings.viewports[0].valid||c.bindings.scissors.size()!=1||!c.bindings.scissors[0].valid)return skip("viewport missing");
    std::lock_guard<std::mutex> lock(s->dlssMutex);
    auto& reconstruction=*s->reconstruction;
    const auto color=s->finalColors.find(c.descriptors[0].set);
    if(color==s->finalColors.end()||color->second.imageLayout!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)return skip("post-tonemap descriptor missing");
    const auto view=s->viewInfos.find(color->second.imageView);if(view==s->viewInfos.end())return skip("scene view missing");
    const auto& v=view->second;const auto image=s->auditImages.find(v.image);const auto allocation=reconstruction.sourceAllocation();
    State::SceneWrite written{};
    {std::lock_guard<std::mutex> writes(s->sceneWritesMutex);const auto it=s->sceneWriteFrames.find(v.image);if(it!=s->sceneWriteFrames.end())written=it->second;}
    const uint64_t sceneFrame=written.frame;
    if(!sceneFrame||sceneFrame>s->renderPose.serial||s->renderPose.serial-sceneFrame>1||
       !reconstruction.hasGuides(cb,sceneFrame,written.command))return skip("temporal guides do not match scene producer");
    // Pending cross-buffer work is admitted only when scene and guides were
    // recorded by the SAME producer. Submission validates its order before the
    // final consumer; no CPU idle wait can prove an unsubmitted producer done.
    if(written.command!=cb&&!written.completed&&
       written.command!=reconstruction.guideProducer())return skip("scene producer is unrelated to pending guides");
    if(image==s->auditImages.end()||v.format!=VK_FORMAT_B10G11R11_UFLOAT_PACK32||v.viewType!=VK_IMAGE_VIEW_TYPE_2D_ARRAY||v.subresourceRange.baseArrayLayer||v.subresourceRange.layerCount!=2||v.subresourceRange.baseMipLevel||v.subresourceRange.levelCount!=1||image->second.samples!=VK_SAMPLE_COUNT_1_BIT||image->second.extent.width!=allocation.width||image->second.extent.height!=allocation.height)return skip("scene format/range/size mismatch");
    const auto targets=s->auditFramebufferViews.find(c.auditFramebuffer);if(targets==s->auditFramebufferViews.end()||targets->second.empty()||targets->second.size()>2)return skip("target attachment missing");
    const auto target=s->viewInfos.find(targets->second[0]);if(target==s->viewInfos.end()||target->second.image==v.image)return skip("scene aliases target");
    DoomFinalUniformSnapshot values;
    if(!s->temporalUniforms->read(c.descriptors[0].set,c.descriptors[0].dynamic,values)||!doomFinalMapping(values.high,values.low,allocation,reconstruction.inputExtent(),extent->second,c.bindings.viewports[0].value,c.bindings.scissors[0].value))return skip("engine final mapping unqualified");
    const auto processor=[&](VkCommandBuffer command,const DoomFinalBridge::Inputs& input){
        std::array<VkImageView,2> result{};
        try{result=reconstruction.record(command,input,extent->second,sceneFrame,written.command);}catch(...){replayComputeBindings(s,command);throw;}
        replayComputeBindings(s,command);
        const auto count=reconstruction.evaluations();if(count<=24||count%240==0)note("[RECONSTRUCTION] eyes="+std::to_string(count)+" neural="+std::to_string(reconstruction.neuralEvaluations())+" reset="+std::to_string(reconstruction.resetEvaluations())+" resolve="+std::to_string(bool(result[0]&&result[1]))+" error="+reconstruction.error());
        if((!result[0]||!result[1])&&!reconstruction.error().empty())skip(reconstruction.error().c_str());
        return result;
    };
    const bool recorded=bridge->second->record(cb,c.bridgeBegin,reconstruction.inputExtent(),extent->second,[&]{replayBindings(s,cb);},draw,processor);
    if(!recorded){const auto error=bridge->second->takeFailure();if(!error.empty())note("[FINAL-BRIDGE] "+error);else skip("private final resource busy or render area unqualified");}
    return recorded;
#else
    return false;
#endif
}
void trackSceneWrite(const std::shared_ptr<State>& s,VkCommandBuffer cb){
    if(!s->bridgeEnabled||!s->frameValid)return;const auto& c=commandOf(s,cb);if(!c.primary||!c.stereo)return;const auto targets=s->auditFramebufferViews.find(c.auditFramebuffer);if(targets==s->auditFramebufferViews.end())return;
    std::lock_guard<std::mutex> writes(s->sceneWritesMutex);
    for(auto id:targets->second){const auto v=s->viewInfos.find(id);if(v!=s->viewInfos.end())s->sceneWriteFrames[v->second.image]={s->renderPose.serial,cb,false,false};}
}
VKAPI_ATTR void VKAPI_CALL draw(VkCommandBuffer cb,uint32_t vertices,uint32_t instances,uint32_t firstVertex,uint32_t firstInstance){COMMAND_BEGIN auditDraw(s,cb,"draw");auto emit=[&]{FN(vkCmdDraw)(cb,vertices,instances,firstVertex,firstInstance);};if(!bridgeDraw(s,cb,(vertices==3||vertices==6)&&instances==1&&!firstInstance,emit)&&!finalDraw(s,cb,(vertices==3||vertices==6)&&instances==1&&!firstInstance,emit))emit();trackSceneWrite(s,cb);COMMAND_END}
VKAPI_ATTR void VKAPI_CALL drawIndexed(VkCommandBuffer cb,uint32_t indices,uint32_t instances,uint32_t firstIndex,int32_t vertexOffset,uint32_t firstInstance){COMMAND_BEGIN auditDraw(s,cb,"drawIndexed");auto emit=[&]{FN(vkCmdDrawIndexed)(cb,indices,instances,firstIndex,vertexOffset,firstInstance);};if(!bridgeDraw(s,cb,(indices==3||indices==6)&&instances==1&&!firstInstance,emit)&&!finalDraw(s,cb,(indices==3||indices==6)&&instances==1&&!firstInstance,emit))emit();trackSceneWrite(s,cb);COMMAND_END}
VKAPI_ATTR void VKAPI_CALL drawIndirect(VkCommandBuffer cb,VkBuffer buffer,VkDeviceSize offset,uint32_t count,uint32_t stride){COMMAND_BEGIN ++commandOf(s,cb).passDraws;auditDraw(s,cb,"drawIndirect");FN(vkCmdDrawIndirect)(cb,buffer,offset,count,stride);COMMAND_END}
VKAPI_ATTR void VKAPI_CALL drawIndexedIndirect(VkCommandBuffer cb,VkBuffer buffer,VkDeviceSize offset,uint32_t count,uint32_t stride){COMMAND_BEGIN ++commandOf(s,cb).passDraws;auditDraw(s,cb,"drawIndexedIndirect");FN(vkCmdDrawIndexedIndirect)(cb,buffer,offset,count,stride);COMMAND_END}
VKAPI_ATTR void VKAPI_CALL lineWidth(VkCommandBuffer cb,float width){COMMAND_BEGIN
    commandOf(s,cb).bindings.line=width;FN(vkCmdSetLineWidth)(cb,width);
COMMAND_END}
VKAPI_ATTR void VKAPI_CALL depthBias(VkCommandBuffer cb,float constant,float clamp,float slope){COMMAND_BEGIN
    commandOf(s,cb).bindings.bias={constant,clamp,slope};FN(vkCmdSetDepthBias)(cb,constant,clamp,slope);
COMMAND_END}
VKAPI_ATTR void VKAPI_CALL blendConstants(VkCommandBuffer cb,const float* values){COMMAND_BEGIN
    commandOf(s,cb).bindings.blend={values[0],values[1],values[2],values[3]};FN(vkCmdSetBlendConstants)(cb,values);
COMMAND_END}
VKAPI_ATTR void VKAPI_CALL depthBounds(VkCommandBuffer cb,float min,float max){COMMAND_BEGIN
    commandOf(s,cb).bindings.bounds={min,max};FN(vkCmdSetDepthBounds)(cb,min,max);
COMMAND_END}
#define STENCIL_WRAPPER(handler,api,slot) \
VKAPI_ATTR void VKAPI_CALL handler(VkCommandBuffer cb,VkStencilFaceFlags faces,uint32_t value){COMMAND_BEGIN \
    commandOf(s,cb).bindings.setStencil(slot,faces,value);FN(api)(cb,faces,value); \
COMMAND_END}
STENCIL_WRAPPER(stencilCompare,vkCmdSetStencilCompareMask,0)
STENCIL_WRAPPER(stencilWrite,vkCmdSetStencilWriteMask,1)
STENCIL_WRAPPER(stencilReference,vkCmdSetStencilReference,2)
#undef STENCIL_WRAPPER
VKAPI_ATTR void VKAPI_CALL dispatch(VkCommandBuffer cb,uint32_t x,uint32_t y,uint32_t z){COMMAND_BEGIN
    uint32_t depth{};if(!dispatchDepth(z,s->computeStereo.at(commandOf(s,cb).compute),65535,depth))throw std::runtime_error("Stereo dispatch exceeds limit");observe(s,[&](auto&,auto&){contextEvent(s,cb,"dispatch","\"pipeline\":"+std::to_string(tc::handle(commandOf(s,cb).compute))+",\"groups\":["+std::to_string(x)+","+std::to_string(y)+","+std::to_string(depth)+"]");});FN(vkCmdDispatch)(cb,x,y,depth);
COMMAND_END}
VKAPI_ATTR void VKAPI_CALL dispatchIndirect(VkCommandBuffer cb,VkBuffer buffer,VkDeviceSize offset){COMMAND_BEGIN
    const auto pipeline=commandOf(s,cb).compute;observe(s,[&](auto&,auto&){contextEvent(s,cb,"dispatchIndirect","\"pipeline\":"+std::to_string(tc::handle(pipeline))+",\"buffer\":"+std::to_string(tc::handle(buffer))+",\"offset\":"+std::to_string(offset));});
    if(!s->computeStereo.at(pipeline)){
        s->indirectMono.fetch_add(1,std::memory_order_relaxed);
        FN(vkCmdDispatchIndirect)(cb,buffer,offset);return;
    }
    const auto found=s->indirectPipelines.find(pipeline);
    if(found==s->indirectPipelines.end())throw std::runtime_error("SFS indirect compute uses an unsupported profile replacement; refusing left-eye-only output");
    s->indirectStereo.fetch_add(1,std::memory_order_relaxed);
    // Counts remain GPU-owned and unchanged. Each variant uses the full original
    // workgroup grid and writes its own eye layer; shared-buffer-only work stays mono.
    for(auto eye:found->second){FN(vkCmdBindPipeline)(cb,VK_PIPELINE_BIND_POINT_COMPUTE,eye);FN(vkCmdDispatchIndirect)(cb,buffer,offset);}
    FN(vkCmdBindPipeline)(cb,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
COMMAND_END}
VkImageSubresourceRange range(const std::shared_ptr<State>& s,VkImage image,VkImageSubresourceRange value){if(s->images.layers(image)==2&&value.baseArrayLayer==0&&value.layerCount==1)value.layerCount=2;return value;}
VKAPI_ATTR void VKAPI_CALL barriers(VkCommandBuffer cb,VkPipelineStageFlags src,VkPipelineStageFlags dst,VkDependencyFlags deps,uint32_t nm,const VkMemoryBarrier* m,uint32_t nb,const VkBufferMemoryBarrier* b,uint32_t ni,const VkImageMemoryBarrier* i){
    if(!ni){try{auto s=state(cb);if(s->context){std::shared_lock<std::shared_mutex> lock(s->mutex);contextBarriers(s,cb,src,dst,deps,nm,m,nb,b,0,i);}FN(vkCmdPipelineBarrier)(cb,src,dst,deps,nm,m,nb,b,0,i);return;}catch(const std::exception& e){commandFailure(e.what());}}
    COMMAND_BEGIN
    auto& images=commandOf(s,cb).imageBarriers;images.clear();if(ni)images.assign(i,i+ni);for(auto& image:images){image.subresourceRange=range(s,image.image,image.subresourceRange);
        if(s->sources&&s->sources->ownsImage(image.image)){
            if(image.oldLayout==VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)image.oldLayout=VK_IMAGE_LAYOUT_GENERAL;
            if(image.newLayout==VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)image.newLayout=VK_IMAGE_LAYOUT_GENERAL;
        }
    }contextBarriers(s,cb,src,dst,deps,nm,m,nb,b,ni,images.data());FN(vkCmdPipelineBarrier)(cb,src,dst,deps,nm,m,nb,b,ni,images.data());
COMMAND_END}
VKAPI_ATTR void VKAPI_CALL clearColor(VkCommandBuffer cb,VkImage image,VkImageLayout layout,const VkClearColorValue* value,uint32_t count,const VkImageSubresourceRange* ranges){COMMAND_BEGIN
    auto& copies=commandOf(s,cb).clearRanges;copies.clear();if(count)copies.assign(ranges,ranges+count);for(auto& r:copies)r=range(s,image,r);contextTransfer(s,cb,"clearColor",VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED,image,layout);FN(vkCmdClearColorImage)(cb,image,layout,value,count,copies.data());
COMMAND_END}
VKAPI_ATTR void VKAPI_CALL clearDepth(VkCommandBuffer cb,VkImage image,VkImageLayout layout,const VkClearDepthStencilValue* value,uint32_t count,const VkImageSubresourceRange* ranges){COMMAND_BEGIN
    auto& copies=commandOf(s,cb).clearRanges;copies.clear();if(count)copies.assign(ranges,ranges+count);for(auto& r:copies)r=range(s,image,r);contextTransfer(s,cb,"clearDepth",VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED,image,layout);FN(vkCmdClearDepthStencilImage)(cb,image,layout,value,count,copies.data());
COMMAND_END}
template<class T>std::vector<T> copyRegions(const std::shared_ptr<State>& s,VkImage src,VkImage dst,uint32_t count,const T* regions){
    std::vector<T> result;for(uint32_t j=0;j<count;++j){auto region=regions[j];result.push_back(region);if(s->images.layers(dst)==2&&region.dstSubresource.baseArrayLayer==0&&region.dstSubresource.layerCount==1){region.dstSubresource.baseArrayLayer=1;if(s->images.layers(src)==2)region.srcSubresource.baseArrayLayer=1;result.push_back(region);}}return result;
}
VKAPI_ATTR void VKAPI_CALL copyImage(VkCommandBuffer cb,VkImage src,VkImageLayout sl,VkImage dst,VkImageLayout dl,uint32_t count,const VkImageCopy* regions){COMMAND_BEGIN auto r=copyRegions(s,src,dst,count,regions);contextTransfer(s,cb,"copyImage",src,sl,dst,dl);FN(vkCmdCopyImage)(cb,src,sl,dst,dl,uint32_t(r.size()),r.data());COMMAND_END}
VKAPI_ATTR void VKAPI_CALL blitImage(VkCommandBuffer cb,VkImage src,VkImageLayout sl,VkImage dst,VkImageLayout dl,uint32_t count,const VkImageBlit* regions,VkFilter filter){COMMAND_BEGIN auto r=copyRegions(s,src,dst,count,regions);contextTransfer(s,cb,"blitImage",src,sl,dst,dl);FN(vkCmdBlitImage)(cb,src,sl,dst,dl,uint32_t(r.size()),r.data(),filter);COMMAND_END}
VKAPI_ATTR void VKAPI_CALL resolveImage(VkCommandBuffer cb,VkImage src,VkImageLayout sl,VkImage dst,VkImageLayout dl,uint32_t count,const VkImageResolve* regions){COMMAND_BEGIN auto r=copyRegions(s,src,dst,count,regions);contextTransfer(s,cb,"resolveImage",src,sl,dst,dl);FN(vkCmdResolveImage)(cb,src,sl,dst,dl,uint32_t(r.size()),r.data());COMMAND_END}
VKAPI_ATTR void VKAPI_CALL uploadImage(VkCommandBuffer cb,VkBuffer buffer,VkImage image,VkImageLayout layout,uint32_t count,const VkBufferImageCopy* regions){COMMAND_BEGIN
    std::vector<VkBufferImageCopy> copies;for(uint32_t j=0;j<count;++j){auto r=regions[j];copies.push_back(r);if(s->images.layers(image)==2&&r.imageSubresource.baseArrayLayer==0&&r.imageSubresource.layerCount==1){r.imageSubresource.baseArrayLayer=1;copies.push_back(r);}}contextTransfer(s,cb,"uploadImage",VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED,image,layout);FN(vkCmdCopyBufferToImage)(cb,buffer,image,layout,uint32_t(copies.size()),copies.data());
COMMAND_END}
}
bool nativeProbeEnabled(){static const bool enabled=[] {char value[8]{};return GetEnvironmentVariableA("KHARVOX_SFS_NATIVE_PROBE",value,8)==1&&value[0]=='1';}();return enabled;}
bool geometryAuditRequested(){char flag[8]{};return GetEnvironmentVariableA("KHARVOX_GEOMETRY_AUDIT",flag,8)==1&&flag[0]=='1';}
bool temporalAuditRequested(){wchar_t path[32768]{};const auto n=GetEnvironmentVariableW(L"KHARVOX_TEMPORAL_AUDIT_PATH",path,32768);return n>0&&n<32768;}
#ifdef KHARVOX_DLSS_TEST_READBACK
#ifdef KHARVOX_HAVE_RECONSTRUCTION
bool dlssTestGuidesFor(VkDevice d,VkCommandBuffer cb,uint64_t frame){auto s=state(d);std::shared_lock<std::shared_mutex> lock(s->mutex);std::lock_guard<std::mutex> guard(s->dlssMutex);return s->reconstruction&&s->reconstruction->hasGuides(cb,frame);}
uint64_t dlssTestResetEvaluations(VkDevice d){auto s=state(d);std::shared_lock<std::shared_mutex> lock(s->mutex);std::lock_guard<std::mutex> guard(s->dlssMutex);return s->reconstruction?s->reconstruction->resetEvaluations():0;}
bool dlssTestInactiveReleased(VkDevice d){auto s=state(d);std::shared_lock<std::shared_mutex> lock(s->mutex);std::lock_guard<std::mutex> guard(s->dlssMutex);
    return (overlay::mode()==2||!s->dlss||!s->dlss->output().eyes[0].view)&&
        (overlay::mode()>=3||!s->reconstruction||!s->reconstruction->output().eyes[0].view);
}
#endif
bool dlssTestControlsDraining(VkDevice d){auto s=state(d);std::shared_lock<std::shared_mutex> lock(s->mutex);return s->controlsDraining;}
bool dlssTestOutput(VkDevice d,kharvox::dlss::Output& output,uint64_t* neuralCount){
    if(neuralCount)*neuralCount=0;
    auto s=state(d);std::lock_guard<std::mutex> guard(s->dlssMutex);
#ifdef KHARVOX_HAVE_RECONSTRUCTION
    if(s->reconstruction&&overlay::reconstruction()){if(neuralCount)*neuralCount=s->reconstruction->neuralEvaluations();output=s->reconstruction->output();return s->reconstruction->pendingCommand()&&output.eyes[0].view&&output.eyes[1].view;}
#endif
    if(!s->dlss||!s->dlss->pendingCommand())return false;
    output=s->dlss->output();return output.eyes[0].view&&output.eyes[1].view;
}
#endif
bool outputRenderedAt(VkDevice d,VkExtent2D extent,uint64_t frame){
    if(overlay::mode()<2)return true;
#ifdef KHARVOX_HAVE_DLSS_STARTUP
    auto s=state(d);std::lock_guard<std::mutex> guard(s->dlssMutex);
    const kharvox::dlss::Output* output{};
#ifdef KHARVOX_HAVE_RECONSTRUCTION
    if(s->reconstruction&&overlay::reconstruction())output=&s->reconstruction->output();
#endif
    if(s->dlss&&overlay::dlaa())output=&s->dlss->output();
    if(!output)return false;
    for(const auto& eye:output->eyes)if(!eye.view||eye.identity.frame!=frame||eye.extent.width!=extent.width||eye.extent.height!=extent.height)return false;
    return true;
#else
    return false;
#endif
}
uint64_t dlssEvaluations(VkDevice d){
#ifdef KHARVOX_HAVE_DLSS_STARTUP
    auto s=state(d);std::lock_guard<std::mutex> guard(s->dlssMutex);
#ifdef KHARVOX_HAVE_RECONSTRUCTION
    if(s->reconstruction&&overlay::reconstruction())return s->reconstruction->evaluations();
#endif
    return s->dlss?s->dlss->evaluations():0;
#else
    return 0;
#endif
}
uint64_t dlssCopyToken(VkDevice d,VkQueue queue){
#ifdef KHARVOX_HAVE_DLSS_STARTUP
    auto s=state(d);std::shared_lock<std::shared_mutex> lock(s->mutex);std::lock_guard<std::mutex> guard(s->dlssMutex);uint64_t token{};forReconstruction(s,[&](auto& p){if(const auto value=p.completionToken(queue))token=value;});return token;
#else
    return 0;
#endif
}
void dlssCopyCompleted(VkDevice d,uint64_t token){
#ifdef KHARVOX_HAVE_DLSS_STARTUP
    if(!token)return;auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);
    std::lock_guard<std::mutex> guard(s->dlssMutex);
#ifdef KHARVOX_HAVE_RECONSTRUCTION
    if(s->reconstruction&&s->reconstruction->completed(token)){
        retireBridges(s);const auto& p=*s->reconstruction;const auto extent=p.output().eyes[0].extent;
        if(extent.width){std::lock_guard<std::mutex> status(s->statusMutex);s->completedFrame=p.output().eyes[0].identity.frame;s->statusError.clear();}
    }
#endif
    if(s->dlss&&s->dlss->completed(token)){
        const auto bridge=s->bridges.find(s->dlssPipeline);if(bridge!=s->bridges.end())bridge->second->retired();
        const auto count=s->dlss->evaluations();{std::lock_guard<std::mutex> status(s->statusMutex);s->completedFrame=s->dlss->output().eyes[0].identity.frame;s->statusError.clear();}if(count<=24||count%240==0)note("[DLSS-LIVE] XR copy completed eye evaluations="+std::to_string(count));
    }
#endif
}
bool configureDlss(VkDevice d,const kharvox::dlss::Options& options){
#ifdef KHARVOX_HAVE_DLSS_STARTUP
    {auto s=state(d);std::unique_lock<std::shared_mutex> guard(s->mutex);s->liveOptions=options;}
    if(overlay::enabled()&&overlay::mode()<=1)return true;
#ifdef KHARVOX_HAVE_RECONSTRUCTION
    if(overlay::reconstruction())try{
        auto s=state(d);std::unique_lock<std::shared_mutex> guard(s->mutex);
        if(!s->temporalUniforms||!s->temporalViews||s->dlss||s->reconstruction)return false;
        auto config=options;config.neuralLibrary=overlay::root()/L"ngx/nvngx_dlssnr.dll";
        s->reconstruction=std::make_unique<DoomReconstructionSession>(config);
        note("[RECONSTRUCTION] SR/Neural/foveation prepared; awaiting qualified engine final scene");return true;
    }catch(const std::exception& e){note(std::string("[RECONSTRUCTION] initialization unavailable: ")+e.what());return false;}
#endif
    char enabled[8]{},raw[8]{};
    if(GetEnvironmentVariableA("KHARVOX_DLSS_DLAA",enabled,8)!=1||enabled[0]!='1')return false;
    // Explicit experimental raw-HDR policy. It is not a claim that every DOOM
    // material has been proved free of an upstream exposure factor.
    if(GetEnvironmentVariableA("KHARVOX_DLSS_RAW_HDR",raw,8)!=1||raw[0]!='1'){note("[DLSS-LIVE] disabled: input exposure policy missing");return false;}
    try{auto s=state(d);std::unique_lock<std::shared_mutex> guard(s->mutex);
        if(!s->temporalUniforms||!s->temporalViews||s->dlss)return false;
        s->dlss=std::make_unique<DoomDlssSession>(options);
        note("[DLSS-LIVE] experimental DLAA prepared; raw HDR preExposure=1 policy, NGX auto-exposure; awaits qualified engine draw");return true;
    }catch(const std::exception& e){note(std::string("[DLSS-LIVE] initialization unavailable: ")+e.what());}
#endif
    return false;
}
template<class Append>bool validateReconstructionSubmission(VkDevice d,VkQueue q,Append&& append){
#ifdef KHARVOX_HAVE_RECONSTRUCTION
    if(!nativeProbeEnabled())return true;
    try{
        auto s=state(d);std::shared_lock<std::shared_mutex> guard(s->mutex);std::lock_guard<std::mutex> lock(s->dlssMutex);
        if(!s->reconstruction||!s->reconstruction->pendingCommand())return true;
        std::vector<VkCommandBuffer> commands;append(commands);
        if(s->reconstruction->validateSubmission(q,commands))return true;
        const char* error="reconstruction consumer submitted before its scene/guide producer";
        {std::lock_guard<std::mutex> status(s->statusMutex);s->statusError=error;}
        note(std::string("[RECONSTRUCTION] submission rejected: ")+error);return false;
    }catch(const std::exception& e){note(std::string("[RECONSTRUCTION] submission check failed: ")+e.what());return false;}
#else
    return true;
#endif
}
bool dlssValidateSubmission(VkDevice d,VkQueue q,uint32_t count,const VkSubmitInfo* infos){
    return validateReconstructionSubmission(d,q,[&](auto& commands){for(uint32_t n=0;n<count;++n)for(uint32_t k=0;k<infos[n].commandBufferCount;++k)commands.push_back(infos[n].pCommandBuffers[k]);});
}
bool dlssValidateSubmission2(VkDevice d,VkQueue q,uint32_t count,const VkSubmitInfo2* infos){
    return validateReconstructionSubmission(d,q,[&](auto& commands){for(uint32_t n=0;n<count;++n)for(uint32_t k=0;k<infos[n].commandBufferInfoCount;++k)commands.push_back(infos[n].pCommandBufferInfos[k].commandBuffer);});
}
void dlssSubmitted(VkDevice d,VkQueue q,uint32_t count,const VkSubmitInfo* infos,VkResult result){
#ifdef KHARVOX_HAVE_DLSS_STARTUP
    if(!nativeProbeEnabled()||result!=VK_SUCCESS)return;
    try{auto s=state(d);std::unique_lock<std::shared_mutex> guard(s->mutex);std::lock_guard<std::mutex> lock(s->dlssMutex);forReconstruction(s,[&](auto& p){
        for(uint32_t n=0;n<count;++n)for(uint32_t k=0;k<infos[n].commandBufferCount;++k)p.observeSubmission(q,infos[n].pCommandBuffers[k],result);});
        for(uint32_t n=0;n<count;++n)for(uint32_t k=0;k<infos[n].commandBufferCount;++k)for(auto& entry:s->sceneWriteFrames)if(entry.second.command==infos[n].pCommandBuffers[k])entry.second.submitted=s->liveOptions&&q==s->liveOptions->queue;
    }catch(const std::exception& e){note(e.what());}
#endif
}
void dlssSubmitted2(VkDevice d,VkQueue q,uint32_t count,const VkSubmitInfo2* infos,VkResult result){
#ifdef KHARVOX_HAVE_DLSS_STARTUP
    if(!nativeProbeEnabled()||result!=VK_SUCCESS)return;
    try{auto s=state(d);std::unique_lock<std::shared_mutex> guard(s->mutex);std::lock_guard<std::mutex> lock(s->dlssMutex);forReconstruction(s,[&](auto& p){
        for(uint32_t n=0;n<count;++n)for(uint32_t k=0;k<infos[n].commandBufferInfoCount;++k)p.observeSubmission(q,infos[n].pCommandBufferInfos[k].commandBuffer,result);});
        for(uint32_t n=0;n<count;++n)for(uint32_t k=0;k<infos[n].commandBufferInfoCount;++k)for(auto& entry:s->sceneWriteFrames)if(entry.second.command==infos[n].pCommandBufferInfos[k].commandBuffer)entry.second.submitted=s->liveOptions&&q==s->liveOptions->queue;
    }catch(const std::exception& e){note(e.what());}
#endif
}
bool temporalUniformSnapshot(VkDevice d,uint64_t& serial,DoomTemporalUniformSnapshot& output){
    auto s=state(d);std::lock_guard<std::mutex> guard(s->temporalSnapshotMutex);serial=s->temporalSnapshotSerial;
    if(!serial)return false;output=s->temporalSnapshot;return true;
}
bool initialize(VkDevice d,VkPhysicalDevice,PFN_vkGetDeviceProcAddr gdpa,const VkPhysicalDeviceMemoryProperties& memory,const TemporalAuditDevice& auditDevice){
    if(!nativeProbeEnabled())return true;
    auto s=std::make_shared<State>();
    try{s->device=d;s->dispatch.load(d,gdpa);
        char bridge[8]{};s->bridgeEnabled=!temporalAuditRequested()&&GetEnvironmentVariableA("KHARVOX_TEMPORAL_BRIDGE",bridge,8)==1&&bridge[0]=='1';s->bridgeDispatch=gdpa;s->bridgeMemory=memory;
        char parameters[8]{};if(s->bridgeEnabled&&GetEnvironmentVariableA("KHARVOX_TEMPORAL_PARAMETERS",parameters,8)==1&&parameters[0]=='1'){
            s->temporalUniforms=std::make_unique<DoomTemporalUniforms>();
            s->temporalViews=std::make_shared<doom_temporal::ViewSnapshots>();
        }
        if(s->bridgeEnabled)note("[TEMPORAL-BRIDGE] transport test enabled; original temporal blend bypassed only at qualified TAA draw; NGX images OFF");
        char timing[8]{};s->profileTiming=GetEnvironmentVariableA("KHARVOX_SFS_PROFILE_TIMING",timing,8)==1&&timing[0]=='1';wchar_t path[32768]{};auto n=GetEnvironmentVariableW(L"KHARVOX_SFS_PROFILE",path,32768);if(n&&n<32768)s->profile=path;
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bi.size=sizeof(FrameUniforms);bi.usage=VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;if(FN(vkCreateBuffer)(d,&bi,nullptr,&s->params)!=VK_SUCCESS)return false;
        VkMemoryRequirements r{};FN(vkGetBufferMemoryRequirements)(d,s->params,&r);uint32_t index=UINT32_MAX;for(uint32_t j=0;j<memory.memoryTypeCount;++j)if((r.memoryTypeBits&(1u<<j))&&(memory.memoryTypes[j].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){index=j;break;}
        if(index==UINT32_MAX)throw std::runtime_error("No coherent SFS parameter memory");VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=r.size;ai.memoryTypeIndex=index;if(FN(vkAllocateMemory)(d,&ai,nullptr,&s->paramsMemory)!=VK_SUCCESS)throw std::runtime_error("SFS parameter allocation failed");if(FN(vkBindBufferMemory)(d,s->params,s->paramsMemory,0)!=VK_SUCCESS)throw std::runtime_error("SFS parameter bind failed");
        void* mapped{};if(FN(vkMapMemory)(d,s->paramsMemory,0,sizeof(FrameUniforms),0,&mapped)!=VK_SUCCESS)throw std::runtime_error("SFS parameter map failed");FrameUniforms initial;std::memcpy(mapped,&initial,sizeof(initial));FN(vkUnmapMemory)(d,s->paramsMemory);
        if(auditDevice.enabled&&temporalAuditRequested())try{
            wchar_t auditPath[32768]{};GetEnvironmentVariableW(L"KHARVOX_TEMPORAL_AUDIT_PATH",auditPath,32768);
            auto audit=std::make_unique<TemporalAuditCapture>(d,s->dispatch);audit->initialize(memory,auditDevice.limits,auditPath,std::wstring(auditPath)+L".arm",true,geometryAuditRequested());s->audit=std::move(audit);
            char contextFlag[8]{};if(GetEnvironmentVariableA("KHARVOX_TEMPORAL_CONTEXT",contextFlag,8)==1&&contextFlag[0]=='1')try{
                auto context=std::make_shared<tc::Capture>(tc::handle(d),std::wstring(auditPath)+L".context.jsonl");
                auto metadata=std::make_unique<tc::VulkanMetadata>(context);
                if(!tc::publish(context))throw std::runtime_error("Only one temporal context device is supported");
                s->context=std::move(metadata);note("CPU temporal context enabled; shares audit arm; bounded to 12 retired intervals");
            }catch(const std::exception& e){note(std::string("CPU temporal context disabled: ")+e.what());}
            note(s->audit->geometryMode()?"bounded geometry audit waiting for .arm marker; first 64 direct draws per recording boundary; 32 sampled batches maximum":"bounded temporal operation audit waiting for .arm marker; 32 sampled frame batches maximum");
        }catch(const std::exception& e){note(std::string("temporal audit disabled: ")+e.what());}
        if(s->temporalViews&&!doom_temporal::publish(s->temporalViews))s->temporalViews.reset();
        {std::lock_guard<std::mutex> lock(devicesMutex);devices[dispatchKey(d)]=s;deviceGeneration.fetch_add(1,std::memory_order_release);}note(vrEnabled()?"native SFS experimental OpenXR producer initialized":"native multiview probe initialized; fixed identity projection; NOT VR");return true;
    }catch(const std::exception& e){note(e.what());if(s->params)FN(vkDestroyBuffer)(d,s->params,nullptr);if(s->paramsMemory)FN(vkFreeMemory)(d,s->paramsMemory,nullptr);return false;}
}
void shutdown(VkDevice d){if(!nativeProbeEnabled())return;std::shared_ptr<State> s;try{s=state(d);}catch(const std::exception&){return;}std::unique_lock<std::shared_mutex> lock(s->mutex);dlssShutdown(s);if(s->temporalViews)doom_temporal::unpublish(s->temporalViews);s->commands.clear();CommandLookupCache::invalidate();if(!s->bridges.empty()||!s->finalBridges.empty()){if(FN(vkDeviceWaitIdle)(d)!=VK_SUCCESS)commandFailure("Temporal bridge shutdown retirement failed");s->bridges.clear();s->finalBridges.clear();}if(s->audit){const auto retirement=FN(vkDeviceWaitIdle)(d);if(retirement!=VK_SUCCESS&&retirement!=VK_ERROR_DEVICE_LOST)commandFailure("Temporal audit shutdown retirement failed");if(retirement==VK_SUCCESS)s->audit->retire(0);s->audit.reset();}if(s->context){s->context->capture->finish("shutdown");tc::unpublish(s->context->capture);s->context.reset();}if(s->sources){if(FN(vkDeviceWaitIdle)(d)!=VK_SUCCESS)commandFailure("SFS source shutdown retirement failed");s->sources->clearAfterDeviceIdle();}for(auto& entry:s->eyeViews)for(auto eye:entry.second)if(eye)FN(vkDestroyImageView)(d,eye,nullptr);s->eyeViews.clear();for(auto& module:s->compiled)FN(vkDestroyShaderModule)(d,module.second,nullptr);FN(vkDestroyBuffer)(d,s->params,nullptr);FN(vkFreeMemory)(d,s->paramsMemory,nullptr);std::lock_guard<std::mutex> devicesLock(devicesMutex);for(auto it=devices.begin();it!=devices.end();)if(it->second==s)it=devices.erase(it);else ++it;deviceGeneration.fetch_add(1,std::memory_order_release);}
bool vrEnabled(){static const bool enabled=[] {char value[8]{};return GetEnvironmentVariableA("KHARVOX_SFS_NATIVE_VR",value,8)==1&&value[0]=='1';}();return nativeProbeEnabled()&&enabled;}
bool eyeAttachmentView(VkDevice d,VkImageView original,uint32_t eye,VkImageView& result){
    result=VK_NULL_HANDLE;if(!nativeProbeEnabled()||eye>1)return false;
    auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);
    auto found=s->viewInfos.find(original);if(found==s->viewInfos.end())return false;
    auto info=found->second;
    if(info.viewType!=VK_IMAGE_VIEW_TYPE_2D_ARRAY||info.subresourceRange.layerCount<2)return false;
    auto& cached=s->eyeViews[original][eye];
    if(!cached){info.viewType=VK_IMAGE_VIEW_TYPE_2D;info.subresourceRange.baseArrayLayer+=eye;info.subresourceRange.layerCount=1;
        if(FN(vkCreateImageView)(d,&info,nullptr,&cached)!=VK_SUCCESS)return false;}
    result=cached;return true;
}
void prepare(VkDevice d,const native::FramePose& pose,const XrFovf& source){
    if(!vrEnabled())return;auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);
    FrameUniforms uniforms;
    if(!frameProjection(pose.head,source,pose.views,pose.worldScale,pose.gameplay||pose.cinematic||pose.scripted,uniforms))commandFailure("SFS headset projection unsupported: requires parallel eye cameras");
    s->pendingUniforms=uniforms;s->pendingPose=pose;s->pending=true;
    s->sourcePoseHistory.remember(pose,uniforms);
}
void copyCompleted(VkDevice d){if(!vrEnabled())return;auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);s->completed=true;}
void beginFrame(VkDevice d,VkSwapchainKHR chain,uint32_t imageIndex){
    if(!vrEnabled())return;auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);
    if(s->pending&&s->completed){
    // The prototype shares a uniform buffer across recorded command buffers.
    // Retire all previous readers before writing; a frame ring can replace this
    // conservative wait once multiple queued game frames have explicit ownership.
    const auto clockNow=[] {return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());};
    const auto retireStart=s->profileTiming?clockNow():0;
    if(FN(vkDeviceWaitIdle)(d)!=VK_SUCCESS)commandFailure("SFS frame parameter retirement failed");dlssIdle(s);
    if(dlssCanReuse(s))retireBridges(s);
#ifdef KHARVOX_HAVE_RECONSTRUCTION
    // Process requests only after the existing GPU retirement and only when no
    // recorded-but-unsubmitted work still owns provider resources.
    const auto controlNow=GetTickCount64();
    if(overlay::enabled()&&(controlNow-s->controlPoll>=100||s->controlsDraining)){
        s->controlPoll=controlNow;
        const auto next=overlay::requestedSelection();
        const bool waiting=next&&next->revision>s->controlAttempt&&next->revision>overlay::current().revision;
        const bool reusable=dlssCanReuse(s);
        // Acquire may precede submission of the last recorded command buffer.
        // Stop creating new provider work until that command submits/retires;
        // never cancel it or reconfigure resources while it still owns them.
        s->controlsDraining=(waiting||bool(s->outputSelection))&&!reusable;
        if(reusable){
        if(s->outputSelection){
            const auto result=overlay::outputResize.snapshot();
            if(result.revision==s->outputSelection->revision&&(result.phase==overlay::OutputResize::Phase::Accepted||result.phase==overlay::OutputResize::Phase::Rejected)){
                const bool accepted=result.phase==overlay::OutputResize::Phase::Accepted;
                if(accepted){
                    // Geometry/NGX feature recreation already reset history on
                    // the first new-sized image. This receipt changes no render
                    // controls; do not reset that working history a second time.
                    overlay::commit(*s->outputSelection);
                }
                {std::lock_guard<std::mutex> status(s->statusMutex);s->statusError=result.error;}
                overlay::acknowledge(result.revision,accepted,result.error);s->outputSelection.reset();
                note("[LAYER-OUTPUT] revision="+std::to_string(result.revision)+(accepted?" confirmed by engine/XR/frame":" rejected: "+result.error));
            }
        }
        if(!s->outputSelection&&waiting){
            s->controlAttempt=next->revision;const auto before=overlay::current();bool engineTouched=false;
            try{
                if(next->outputScale!=before.outputScale){
                    // Output requests use their own receipt, after engine and
                    // XR recreation and a completed presentation. Other controls
                    // are unchanged by the shared output UI.
                    if(overlay::rendererIdentity(*next)!=overlay::rendererIdentity(before))
                        throw std::runtime_error("Apply renderer controls before changing output resolution");
                    if(!overlay::outputResize.request(next->revision,next->outputScale,controlNow))
                        throw std::runtime_error("Output resize unavailable, busy or outside runtime limits");
                    s->outputSelection=*next;
                }else{
                if(!s->liveOptions||!overlay::applyEngineSelection)throw std::runtime_error("Engine configuration adapter unavailable");
                std::lock_guard<std::mutex> providerGuard(s->dlssMutex);
                if(next->mode==2&&!s->dlss)s->dlss=std::make_unique<DoomDlssSession>(*s->liveOptions);
                if(next->mode>=3&&!s->reconstruction){auto options=*s->liveOptions;options.neuralLibrary=overlay::root()/L"ngx/nvngx_dlssnr.dll";s->reconstruction=std::make_unique<DoomReconstructionSession>(options);}
                engineTouched=true;
                if(!overlay::applyEngineSelection(*next)){
                    throw std::runtime_error("Engine rejected rendering controls");
                }
                if(s->reconstruction)s->reconstruction->configure(*next);
                if(s->dlss)s->dlss->gap();
                // Only at this proved retired boundary. Keeping a 4K DLAA
                // feature alive while SR renders (or vice versa) retains its
                // output images, private histories and branch scratch buffers.
                if(next->mode!=before.mode){
                    for(auto& b:s->bridges)b.second->invalidateResolve();
                    for(auto& b:s->finalBridges)b.second->invalidateResolve();
                    if(next->mode!=2&&s->dlss&&!s->dlss->releaseResources())throw std::runtime_error("Inactive DLAA resources still owned");
                    if(next->mode<3){
                        if(s->reconstruction&&!s->reconstruction->releaseResources())throw std::runtime_error("Inactive SR resources still owned");
                        for(auto& b:s->finalBridges)if(!b.second->releaseResources())throw std::runtime_error("Inactive final bridge still owned");
                    }
                    if(next->mode<2)for(auto& b:s->bridges)if(!b.second->releaseResources())throw std::runtime_error("Inactive temporal bridge still owned");
                }
                overlay::commit(*next);s->temporalPrevious.reset();s->completedFrame=0;
                {std::lock_guard<std::mutex> status(s->statusMutex);s->statusError.clear();}
                overlay::acknowledge(next->revision,true);
                note("[LAYER-APPLY] committed revision="+std::to_string(next->revision)+" mode="+std::to_string(next->mode));
                }
            }catch(const std::exception& error){
                std::string failure=error.what();
                if(engineTouched){
                    try{
                        if(!overlay::applyEngineSelection(before))failure+="; engine rollback failed";
                        if(s->reconstruction)s->reconstruction->configure(before);
                    }catch(const std::exception& rollback){failure+="; rollback: ";failure+=rollback.what();}
                }
                std::lock_guard<std::mutex> status(s->statusMutex);s->statusError=failure;
                overlay::acknowledge(next->revision,false,failure);
                note(std::string("[LAYER-APPLY] rejected: ")+failure);
            }
        }
        }
    }
#endif

    if(s->audit){s->audit->retire(s->pendingPose.serial);if(s->context)s->context->capture->boundary(s->pendingPose.serial,s->audit->collecting());}
    s->pendingUniforms.previousClip=s->frameValid?s->renderUniforms.clip:s->pendingUniforms.clip;
    s->pendingUniforms.previousTranslation=s->frameValid?s->renderUniforms.translation:s->pendingUniforms.translation;
    const auto uploadStart=s->profileTiming?clockNow():0;
    void* mapped{};if(FN(vkMapMemory)(d,s->paramsMemory,0,sizeof(FrameUniforms),0,&mapped)!=VK_SUCCESS)commandFailure("SFS frame parameter map failed");
    std::memcpy(mapped,&s->pendingUniforms,sizeof(FrameUniforms));FN(vkUnmapMemory)(d,s->paramsMemory);
    if(s->profileTiming){
        const auto retire=uploadStart-retireStart;
        s->retireNs+=retire;s->uploadNs+=clockNow()-uploadStart;
        if(retire>s->maxRetireNs)s->maxRetireNs=retire;
        if(++s->profiledFrames==120){
            const auto indirectMono=s->indirectMono.exchange(0,std::memory_order_relaxed),indirectStereo=s->indirectStereo.exchange(0,std::memory_order_relaxed),mixed=s->mixedPasses.exchange(0,std::memory_order_relaxed);
            note("[SFS-PATHS] frames=120 indirectShared="+std::to_string(indirectMono)+" indirectStereo="+std::to_string(indirectStereo)+" mixedMonoPasses="+std::to_string(mixed));
            const auto samples=CommandCpuTiming::samples.exchange(0,std::memory_order_relaxed);
            const auto wait=CommandCpuTiming::waitNs.exchange(0,std::memory_order_relaxed);
            const auto body=CommandCpuTiming::bodyNs.exchange(0,std::memory_order_relaxed);
            if(samples)note("command CPU timing frames=120 sampleStride=64 samples="+std::to_string(samples)
                +" sampledLookupLockMeanUs="+std::to_string(double(wait)/double(samples)/1000.0)
                +" sampledHookBodyMeanUs="+std::to_string(double(body)/double(samples)/1000.0)
                +" estimatedAggregateHookMsPerFrame="+std::to_string(double(wait+body)*64.0/120000000.0));
            note("parameter timing frames=120 deviceIdleMeanMs="+std::to_string(double(s->retireNs)/120000000.0)
                +" deviceIdleMaxMs="+std::to_string(double(s->maxRetireNs)/1000000.0)
                +" uploadMeanMs="+std::to_string(double(s->uploadNs)/120000000.0));
            s->profiledFrames=s->retireNs=s->uploadNs=s->maxRetireNs=0;
        }
    }
    s->renderPose=s->pendingPose;s->frameValid=true;s->pending=false;s->completed=false;
    s->renderUniforms=s->pendingUniforms;
    }
    // An acquired image uses the uniforms actually installed above, not the
    // newest pending XR prediction. Other swapchain images retain their pose.
    const auto found=s->swapchains.find(chain);
    if(found!=s->swapchains.end()&&imageIndex<found->second.size()){
        const auto image=found->second[imageIndex];
        if(s->frameValid){s->imagePoses[image]=s->renderPose;s->imageUniforms[image]=s->renderUniforms;}
        else {s->imagePoses.erase(image);s->imageUniforms.erase(image);}
    }
}
bool pair(VkDevice d,VkImage image,VkExtent2D extent,VkFormat format,native::StereoFrame& result,const AerSourceObservation* observed){
    if(!vrEnabled())return false;auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);
    const auto found=s->imagePoses.find(image);
    if(found==s->imagePoses.end()||s->images.layers(image)!=2)return false;
    auto pose=found->second;
    if(observed&&pose.gameplay){
        const bool qualified=s->sourcePoseHistory.resolve(*observed,pose,s->imageUniforms.at(image),pose);
        if(++s->sourcePoseSamples<=8||s->sourcePoseSamples%120==0)
            note("[SFS-SOURCE-POSE] acquired="+std::to_string(found->second.source.poseId)
                +" observed="+std::to_string(observed->key.poseId)+" qualified="+std::to_string(qualified)
                +" count="+std::to_string(observed->count)+" ambiguous="+std::to_string(observed->ambiguous));
        if(observed->count&&!qualified)return false;
    }
    result={};result.pose=pose;result.generation=pose.serial;
    for(uint32_t e=0;e<2;++e)result.eyes[e]={image,extent,format,sourceLayout(d,image,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR),pose.views[e].pose,pose.views[e].fov,e,pose.serial};
    if(overlay::enabled()){
        std::lock_guard<std::mutex> providerGuard(s->dlssMutex);std::lock_guard<std::mutex> status(s->statusMutex);
        const auto now=GetTickCount64();if(pose.serial!=s->fpsFrame){s->fpsFrame=pose.serial;if(!s->fpsStart)s->fpsStart=now;else ++s->fpsCount;if(now-s->fpsStart>=500){s->renderFps=s->fpsCount*1000./double(now-s->fpsStart);s->fpsStart=now;s->fpsCount=0;}}
        overlay::Metrics metrics;metrics.fps=s->renderFps;metrics.error=s->statusError;metrics.framePeriodMs=pose.displayPeriod/1e6;uint64_t evaluations{},resets{};
        bool active=s->completedFrame&&s->completedFrame+2>=pose.serial&&s->statusError.empty();
#ifdef KHARVOX_HAVE_DLSS_STARTUP
        if(s->dlss&&(!overlay::enabled()||overlay::dlaa())){evaluations=s->dlss->evaluations();resets=s->dlss->resetEvaluations();metrics.gpuMs=s->dlss->gpuIntervalMs();metrics.processingMs=s->dlss->processingMs();metrics.inputWidth=extent.width;metrics.inputHeight=extent.height;if(!s->dlss->error().empty())metrics.error=s->dlss->error();}
#endif
#ifdef KHARVOX_HAVE_RECONSTRUCTION
        if(s->reconstruction&&overlay::reconstruction()){const auto& p=*s->reconstruction;const auto& selection=p.selection();evaluations=p.evaluations();resets=p.resetEvaluations();metrics.neuralEvaluations=p.neuralEvaluations();metrics.inputWidth=p.inputExtent().width;metrics.inputHeight=p.inputExtent().height;metrics.foveated=active&&selection.foveation.enabled;metrics.gpuMs=p.gpuIntervalMs();metrics.processingMs=p.processingMs();metrics.changeCpuMs=p.changeCpuMs();metrics.targetMs=p.targetMs();metrics.dynamicStatus=p.dynamicStatus();metrics.cuts=dlss_foveado::experimental::Read(selection.foveation.central);for(unsigned e=0;e<2;++e)metrics.maps[e]=selection.geometry[e].binocular.outputToCommon;if(!p.error().empty())metrics.error=p.error();}
#endif
        active=active&&metrics.error.empty();metrics.foveated=metrics.foveated&&active;
        const auto selectedMode=overlay::mode();
        overlay::publish(selectedMode<2?selectedMode:active?selectedMode:1,evaluations,resets,extent.width,extent.height,active||selectedMode<2,metrics);
    }
    return true;
}
void swapchainImages(VkDevice d,VkSwapchainKHR chain,uint32_t count,const VkImage* images){
    if(!nativeProbeEnabled())return;
    auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);
    auto& tracked=s->swapchains[chain];
    // Re-enumerating the same swapchain must not discard a valid acquisition.
    if(tracked==std::vector<VkImage>(images,images+count))return;
    for(auto image:tracked){s->imagePoses.erase(image);s->imageUniforms.erase(image);s->images.destroy(d,image,nullptr,nullptr);}
    tracked.assign(images,images+count);
    for(auto image:tracked)s->images.track(image,2);
}
void swapchainDestroyed(VkDevice d,VkSwapchainKHR chain){
    if(!nativeProbeEnabled())return;
    auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);
    auto found=s->swapchains.find(chain);if(found==s->swapchains.end())return;
    for(auto image:found->second){s->imagePoses.erase(image);s->imageUniforms.erase(image);s->images.destroy(d,image,nullptr,nullptr);}
    s->swapchains.erase(found);
}
bool sourceRingRequested(){static const bool enabled=[] {char value[8]{};return GetEnvironmentVariableA("KHARVOX_SFS_SOURCE_RING",value,8)==1&&value[0]=='1';}();return vrEnabled()&&enabled;}
bool configureSourceRing(VkDevice d,PFN_vkGetDeviceProcAddr resolver,const VkPhysicalDeviceMemoryProperties& memory,VkQueue queue,void(*lock)(),void(*unlock)()){
    auto s=state(d);std::unique_lock<std::shared_mutex> guard(s->mutex);
    auto sources=std::make_unique<SourceRing>();if(!sources->initialize(d,queue,resolver,memory,lock,unlock))return false;
    s->sources=std::move(sources);s->profileTiming=true;CommandCpuTiming::enabled.store(true,std::memory_order_relaxed);note("source ring ACTIVE: application-owned stereo images (engine-requested count), GENERAL layout, same-device OpenXR, desktop WSI bypass");return true;
}
bool sourceRingActive(VkDevice d){if(!nativeProbeEnabled())return false;try{return bool(state(d)->sources);}catch(const std::exception&){return false;}}
bool sourceSwapchain(VkDevice d,VkSwapchainKHR chain){return sourceRingActive(d)&&state(d)->sources->owns(chain);}
bool sourceOutOfDate(VkDevice d,VkSwapchainKHR chain,bool value){return sourceRingActive(d)&&state(d)->sources->outOfDate(chain,value);}
VkResult createSourceSwapchain(VkDevice d,const VkSwapchainCreateInfoKHR& info,VkSwapchainKHR* output){return state(d)->sources->create(info,output);}
VkResult sourceImages(VkDevice d,VkSwapchainKHR chain,uint32_t* count,VkImage* images){return state(d)->sources->enumerate(chain,count,images);}
VkResult acquireSource(VkDevice d,VkSwapchainKHR chain,uint64_t timeout,VkSemaphore sem,VkFence fence,uint32_t* index){return state(d)->sources->acquire(chain,timeout,sem,fence,index);}
VkResult presentSource(VkDevice d,VkQueue queue,const VkPresentInfoKHR& info,bool consumed){return state(d)->sources->present(queue,info,consumed);}
void destroySourceSwapchain(VkDevice d,VkSwapchainKHR chain){if(state(d)->sources->destroy(chain)!=VK_SUCCESS)commandFailure("SFS source destruction retirement failed");}
VkImageLayout sourceLayout(VkDevice d,VkImage image,VkImageLayout layout){
    if(layout!=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR||!sourceRingActive(d))return layout;
    return state(d)->sources->ownsImage(image)?VK_IMAGE_LAYOUT_GENERAL:layout;
}
PFN_vkVoidFunction wrapProc(VkDevice d,const char* name,PFN_vkVoidFunction next){if(!nativeProbeEnabled()||!next)return next;
    bool tracking{},parameters{};try{const auto s=state(d);tracking=bool(s->audit)||s->bridgeEnabled;parameters=bool(s->temporalUniforms);}catch(const std::exception&){return next;}
#define HOOK(api,handler) if(!std::strcmp(name,#api))return reinterpret_cast<PFN_vkVoidFunction>(&handler)
    if(parameters){HOOK(vkAllocateMemory,parameterAllocate);HOOK(vkFreeMemory,parameterFree);HOOK(vkCreateBuffer,parameterBuffer);HOOK(vkDestroyBuffer,parameterDestroyBuffer);HOOK(vkBindBufferMemory,parameterBind);HOOK(vkMapMemory,parameterMap);HOOK(vkUnmapMemory,parameterUnmap);}
    HOOK(vkCreateShaderModule,createShader);HOOK(vkDestroyShaderModule,destroyShader);
    HOOK(vkCreateImage,createImage);HOOK(vkDestroyImage,destroyImage);HOOK(vkCreateImageView,createView);HOOK(vkDestroyImageView,destroyView);
    HOOK(vkCreateRenderPass,createPass);HOOK(vkDestroyRenderPass,destroyPass);HOOK(vkCreateFramebuffer,createFramebuffer);HOOK(vkDestroyFramebuffer,destroyFramebuffer);
    HOOK(vkCreateDescriptorSetLayout,createLayout);HOOK(vkDestroyDescriptorSetLayout,destroyLayout);HOOK(vkCreateDescriptorPool,createPool);HOOK(vkAllocateDescriptorSets,allocateSets);
    HOOK(vkFreeDescriptorSets,freeSets);HOOK(vkResetDescriptorPool,resetPool);HOOK(vkDestroyDescriptorPool,destroyPool);
    if(tracking){HOOK(vkCreatePipelineLayout,createPipelineLayout);HOOK(vkDestroyPipelineLayout,destroyPipelineLayout);HOOK(vkUpdateDescriptorSets,updateSets);}
    if(tracking){HOOK(vkEndCommandBuffer,endCommand);HOOK(vkCmdDraw,draw);HOOK(vkCmdDrawIndexed,drawIndexed);HOOK(vkCmdDrawIndirect,drawIndirect);HOOK(vkCmdDrawIndexedIndirect,drawIndexedIndirect);HOOK(vkResetCommandPool,resetCommands);HOOK(vkResetCommandBuffer,resetCommand);}
    HOOK(vkCreateGraphicsPipelines,graphics);HOOK(vkCreateComputePipelines,compute);HOOK(vkDestroyPipeline,destroyPipeline);
    HOOK(vkBeginCommandBuffer,beginCommand);HOOK(vkCmdBindPipeline,bindPipeline);HOOK(vkCmdBindDescriptorSets,bindSets);HOOK(vkCmdBindVertexBuffers,bindVertices);HOOK(vkCmdBindIndexBuffer,bindIndex);
    HOOK(vkCreateCommandPool,createCommandPool);HOOK(vkAllocateCommandBuffers,allocateCommands);HOOK(vkFreeCommandBuffers,freeCommands);HOOK(vkDestroyCommandPool,destroyCommandPool);
    HOOK(vkCmdSetViewport,viewport);HOOK(vkCmdSetScissor,scissor);HOOK(vkCmdPushConstants,push);HOOK(vkCmdBeginRenderPass,beginPass);HOOK(vkCmdEndRenderPass,endPass);HOOK(vkCmdDispatch,dispatch);HOOK(vkCmdDispatchIndirect,dispatchIndirect);
    HOOK(vkCmdNextSubpass,nextPass);HOOK(vkCmdSetLineWidth,lineWidth);HOOK(vkCmdSetDepthBias,depthBias);HOOK(vkCmdSetBlendConstants,blendConstants);HOOK(vkCmdSetDepthBounds,depthBounds);
    HOOK(vkCmdSetStencilCompareMask,stencilCompare);HOOK(vkCmdSetStencilWriteMask,stencilWrite);HOOK(vkCmdSetStencilReference,stencilReference);
    HOOK(vkCmdPipelineBarrier,barriers);HOOK(vkCmdClearColorImage,clearColor);HOOK(vkCmdClearDepthStencilImage,clearDepth);
    HOOK(vkCmdCopyImage,copyImage);HOOK(vkCmdBlitImage,blitImage);HOOK(vkCmdResolveImage,resolveImage);HOOK(vkCmdCopyBufferToImage,uploadImage);
#undef HOOK
    return next;
}
}
