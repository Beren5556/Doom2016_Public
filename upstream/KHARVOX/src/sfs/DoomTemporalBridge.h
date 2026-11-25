#pragma once
#include <vulkan/vulkan.h>
#include <array>
#include <vector>
#include <stdexcept>
#include <mutex>
#include <cmath>
#include <string>
#include <functional>

namespace kharvox::sfs {
// Opt-in transport stage for native DLAA. The host supplies the already
// transformed TAA vertex shader and set 0, then restores ALL graphics bindings.
// Private images are reused only after the host's existing device-idle boundary.
// With no processor it resolves copied HDR, without NGX or temporal blend.
class DoomTemporalBridge {
    VkDevice device;VkPhysicalDeviceMemoryProperties memory;
#define KH_BRIDGE_FUNCTIONS(F) \
    F(CreateImage) F(DestroyImage) F(GetImageMemoryRequirements) F(AllocateMemory) F(FreeMemory) F(BindImageMemory) \
    F(CreateImageView) F(DestroyImageView) F(CreateRenderPass) F(DestroyRenderPass) F(CreateFramebuffer) F(DestroyFramebuffer) \
    F(CreateSampler) F(DestroySampler) F(CreateDescriptorSetLayout) F(DestroyDescriptorSetLayout) \
    F(CreateDescriptorPool) F(DestroyDescriptorPool) F(AllocateDescriptorSets) F(UpdateDescriptorSets) \
    F(CreatePipelineLayout) F(DestroyPipelineLayout) F(CreateGraphicsPipelines) F(DestroyPipeline) \
    F(CmdSetViewport) F(CmdSetScissor) F(CmdBeginRenderPass) F(CmdEndRenderPass) F(CmdBindPipeline) F(CmdBindDescriptorSets) F(CmdPipelineBarrier)
#define KH_DECLARE(n) PFN_vk##n vk##n{};
    KH_BRIDGE_FUNCTIONS(KH_DECLARE)
#undef KH_DECLARE
    struct Texture {VkImage image{};VkDeviceMemory memory{};VkImageView array{};std::array<VkImageView,2> eyes{};};
    std::array<Texture,3> textures{};
    std::array<VkImageView,2> resolveViews{};
    VkRenderPass privatePass{};VkFramebuffer framebuffer{};
    VkDescriptorSetLayout eyeLayout{};VkDescriptorPool pool{};VkDescriptorSet eyes{};VkSampler sampler{};
    VkPipelineLayout resolveLayout{};VkPipeline guidePipeline{},resolvePipeline{};
    VkExtent2D size{};bool used{},disabled{};std::string failure;std::mutex mutex;
    static void check(VkResult r){if(r!=VK_SUCCESS)throw std::runtime_error("Temporal bridge Vulkan failure: "+std::to_string(r));}
    void clearImages(){
        if(framebuffer)vkDestroyFramebuffer(device,framebuffer,nullptr);framebuffer={};
        for(auto& t:textures){for(auto v:t.eyes)if(v)vkDestroyImageView(device,v,nullptr);
            if(t.array)vkDestroyImageView(device,t.array,nullptr);if(t.image)vkDestroyImage(device,t.image,nullptr);
            if(t.memory)vkFreeMemory(device,t.memory,nullptr);t={};}size={};resolveViews={};
    }
    void resize(VkExtent2D extent){
        if(size.width==extent.width&&size.height==extent.height)return;
        clearImages();
        const VkFormat formats[]{VK_FORMAT_R32G32_SFLOAT,VK_FORMAT_R32_SFLOAT,VK_FORMAT_R16G16B16A16_SFLOAT};
        try{
            for(unsigned n=0;n<3;++n){auto& t=textures[n];
                VkImageCreateInfo i{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};i.imageType=VK_IMAGE_TYPE_2D;i.format=formats[n];i.extent={extent.width,extent.height,1};
                i.mipLevels=1;i.arrayLayers=2;i.samples=VK_SAMPLE_COUNT_1_BIT;i.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
                check(vkCreateImage(device,&i,nullptr,&t.image));VkMemoryRequirements req{};vkGetImageMemoryRequirements(device,t.image,&req);
                uint32_t type=UINT32_MAX;for(uint32_t k=0;k<memory.memoryTypeCount;++k)if((req.memoryTypeBits&(1u<<k))&&(memory.memoryTypes[k].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)){type=k;break;}
                if(type==UINT32_MAX)throw std::runtime_error("Temporal bridge device memory unavailable");
                VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};a.allocationSize=req.size;a.memoryTypeIndex=type;check(vkAllocateMemory(device,&a,nullptr,&t.memory));check(vkBindImageMemory(device,t.image,t.memory,0));
                VkImageViewCreateInfo v{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};v.image=t.image;v.format=i.format;v.viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY;v.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,2};
                check(vkCreateImageView(device,&v,nullptr,&t.array));v.viewType=VK_IMAGE_VIEW_TYPE_2D;v.subresourceRange.layerCount=1;
                for(unsigned e=0;e<2;++e){v.subresourceRange.baseArrayLayer=e;check(vkCreateImageView(device,&v,nullptr,&t.eyes[e]));}
            }
            VkImageView views[]{textures[0].array,textures[1].array,textures[2].array};
            VkFramebufferCreateInfo f{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};f.renderPass=privatePass;f.attachmentCount=3;f.pAttachments=views;f.width=extent.width;f.height=extent.height;f.layers=1;check(vkCreateFramebuffer(device,&f,nullptr,&framebuffer));
            size=extent;
        }catch(...){clearImages();throw;}
    }
public:
    struct Inputs {
        VkExtent2D extent{};
        // Order: RG32F motion, R32F depth, RGBA16F current HDR.
        // Array layers are eyes 0/1; all views are shader-readable.
        std::array<VkImage,3> images{};
        std::array<std::array<VkImageView,2>,3> eyeViews{};
    };
    // Called outside render passes in the SAME command buffer. The processor
    // owns synchronization of its outputs and all recording/retirement tickets,
    // including failed work. Return two shader-readable, full-size eye views,
    // or null views to preserve current HDR. Never submit/wait in this callback.
    using Processor=std::function<std::array<VkImageView,2>(VkCommandBuffer,const Inputs&)>;
    DoomTemporalBridge(VkDevice d,PFN_vkGetDeviceProcAddr get,const VkPhysicalDeviceMemoryProperties& m):device(d),memory(m){
#define KH_LOAD(n) vk##n=reinterpret_cast<PFN_vk##n>(get(d,"vk" #n));if(!vk##n)throw std::runtime_error("Missing bridge dispatch vk" #n);
        KH_BRIDGE_FUNCTIONS(KH_LOAD)
#undef KH_LOAD
    }
    // Pipeline creation is outside command recording. Failure leaves the host
    // TAA pipeline available and all partially allocated objects destructible.
    void initialize(const VkGraphicsPipelineCreateInfo& original,const VkPipelineLayoutCreateInfo& layout,VkShaderModule guide,VkShaderModule resolve){
        if(original.subpass||original.pNext||layout.pNext||layout.setLayoutCount!=1||layout.pushConstantRangeCount)
            throw std::runtime_error("Unsupported temporal bridge pipeline layout");
        if(!original.pDynamicState||!original.pViewportState||original.pViewportState->viewportCount!=1||original.pViewportState->scissorCount!=1)
            throw std::runtime_error("Temporal bridge requires one dynamic viewport/scissor");
        bool vp=false,sc=false;for(uint32_t n=0;n<original.pDynamicState->dynamicStateCount;++n){vp|=original.pDynamicState->pDynamicStates[n]==VK_DYNAMIC_STATE_VIEWPORT;sc|=original.pDynamicState->pDynamicStates[n]==VK_DYNAMIC_STATE_SCISSOR;}
        if(!vp||!sc)throw std::runtime_error("Temporal bridge viewport/scissor must be dynamic");
        const VkFormat formats[]{VK_FORMAT_R32G32_SFLOAT,VK_FORMAT_R32_SFLOAT,VK_FORMAT_R16G16B16A16_SFLOAT};
        VkAttachmentDescription attachments[3]{};VkAttachmentReference refs[3]{};
        for(unsigned n=0;n<3;++n){auto& a=attachments[n];a.format=formats[n];a.samples=VK_SAMPLE_COUNT_1_BIT;a.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;a.storeOp=VK_ATTACHMENT_STORE_OP_STORE;a.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;a.finalLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;refs[n]={n,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};}
        VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=3;sub.pColorAttachments=refs;
        uint32_t mask=3;VkRenderPassMultiviewCreateInfo mv{VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO};mv.subpassCount=1;mv.pViewMasks=&mask;mv.correlationMaskCount=1;mv.pCorrelationMasks=&mask;
        VkSubpassDependency deps[2]{};deps[0]={VK_SUBPASS_EXTERNAL,0,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,0};
        deps[1]={0,VK_SUBPASS_EXTERNAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,0};
        VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};rp.pNext=&mv;rp.attachmentCount=3;rp.pAttachments=attachments;rp.subpassCount=1;rp.pSubpasses=&sub;rp.dependencyCount=2;rp.pDependencies=deps;check(vkCreateRenderPass(device,&rp,nullptr,&privatePass));
        VkDescriptorSetLayoutBinding bindings[2]{};for(unsigned e=0;e<2;++e)bindings[e]={e,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
        VkDescriptorSetLayoutCreateInfo sl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};sl.bindingCount=2;sl.pBindings=bindings;check(vkCreateDescriptorSetLayout(device,&sl,nullptr,&eyeLayout));
        VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,2};VkDescriptorPoolCreateInfo pc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pc.maxSets=1;pc.poolSizeCount=1;pc.pPoolSizes=&ps;check(vkCreateDescriptorPool(device,&pc,nullptr,&pool));
        VkDescriptorSetAllocateInfo sa{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};sa.descriptorPool=pool;sa.descriptorSetCount=1;sa.pSetLayouts=&eyeLayout;check(vkAllocateDescriptorSets(device,&sa,&eyes));
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};si.maxLod=0;check(vkCreateSampler(device,&si,nullptr,&sampler));
        VkDescriptorSetLayout sets[]{layout.pSetLayouts[0],eyeLayout};auto li=layout;li.setLayoutCount=2;li.pSetLayouts=sets;check(vkCreatePipelineLayout(device,&li,nullptr,&resolveLayout));
        std::vector<VkPipelineShaderStageCreateInfo> stages(original.pStages,original.pStages+original.stageCount);bool found=false;
        for(auto& stage:stages)if(stage.stage==VK_SHADER_STAGE_FRAGMENT_BIT){stage.module=guide;found=true;}
        if(!found)throw std::runtime_error("Temporal bridge fragment stage missing");
        auto ci=original;ci.flags=0;ci.basePipelineHandle={};ci.basePipelineIndex=-1;ci.pStages=stages.data();ci.renderPass=privatePass;
        VkPipelineColorBlendAttachmentState colors[3]{};for(auto& c:colors)c.colorWriteMask=15;
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blend.attachmentCount=3;blend.pAttachments=colors;ci.pColorBlendState=&blend;
        VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};ci.pDepthStencilState=&depth;check(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&ci,nullptr,&guidePipeline));
        ci=original;ci.flags=0;ci.basePipelineHandle={};ci.basePipelineIndex=-1;ci.pStages=stages.data();ci.layout=resolveLayout;
        for(auto& stage:stages)if(stage.stage==VK_SHADER_STAGE_FRAGMENT_BIT)stage.module=resolve;
        check(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&ci,nullptr,&resolvePipeline));
    }
    void retired(){std::lock_guard<std::mutex> lock(mutex);used=false;}
    // Call at the same retired boundary as provider reconfiguration. Cleared
    // caches prevent a recycled VkImageView handle from hiding a stale binding.
    void invalidateResolve(){std::lock_guard<std::mutex> lock(mutex);resolveViews={};}
    bool releaseResources(){std::lock_guard<std::mutex> lock(mutex);if(used)return false;clearImages();return true;}
    std::string takeFailure(){std::lock_guard<std::mutex> lock(mutex);std::string result;result.swap(failure);return result;}
    template<class Bind,class Draw> bool record(VkCommandBuffer cb,const VkRenderPassBeginInfo& host,VkExtent2D extent,const VkViewport& vp,const VkRect2D& sc,Bind&& bind,Draw&& draw,const Processor& process={},bool qualifiedActiveRectangle=false){
        std::lock_guard<std::mutex> lock(mutex);
        if(disabled||used||!guidePipeline||extent.width<32||extent.height<32||extent.width>8192||extent.height>8192||uint64_t(extent.width)*extent.height>32*1024*1024||host.pNext||host.clearValueCount
            ||host.renderArea.offset.x||host.renderArea.offset.y||host.renderArea.extent.width<extent.width||host.renderArea.extent.height<extent.height
            ||(!qualifiedActiveRectangle&&(host.renderArea.extent.width!=extent.width||host.renderArea.extent.height!=extent.height))
            ||vp.x||vp.y||vp.width<float(extent.width)||vp.height<float(extent.height)||vp.minDepth!=0||vp.maxDepth!=1
            ||(!qualifiedActiveRectangle&&(vp.width!=float(extent.width)||vp.height!=float(extent.height)))
            ||sc.offset.x||sc.offset.y||sc.extent.width<extent.width||sc.extent.height<extent.height
            ||(!qualifiedActiveRectangle&&(sc.extent.width!=extent.width||sc.extent.height!=extent.height)))return false;
        // Allocation failure occurs before touching the caller's render pass.
        // Disable this optional bridge and let the original TAA draw continue.
        try{resize(extent);}catch(const std::exception& e){failure=e.what();disabled=true;return false;}used=true;
        vkCmdEndRenderPass(cb);
        VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=privatePass;begin.framebuffer=framebuffer;begin.renderArea={{0,0},extent};
        vkCmdBeginRenderPass(cb,&begin,VK_SUBPASS_CONTENTS_INLINE);bind();
        const VkViewport privateViewport{0,0,float(extent.width),float(extent.height),0,1};const VkRect2D privateScissor{{0,0},extent};
        vkCmdSetViewport(cb,0,1,&privateViewport);vkCmdSetScissor(cb,0,1,&privateScissor);vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,guidePipeline);draw();vkCmdEndRenderPass(cb);
        // Also order the empty host pass's stores before its resumed LOAD.
        // Its original dependencies need not include this inserted pass split.
        VkMemoryBarrier resumed{VK_STRUCTURE_TYPE_MEMORY_BARRIER};resumed.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        resumed.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,1,&resumed,0,nullptr,0,nullptr);
        auto selected=textures[2].eyes;
        if(process){Inputs input;input.extent=size;for(unsigned n=0;n<3;++n){input.images[n]=textures[n].image;input.eyeViews[n]=textures[n].eyes;}
            try{const auto output=process(cb,input);if(output[0]&&output[1])selected=output;}
            catch(const std::exception& e){failure=std::string("Processor failed; current HDR retained: ")+e.what();}}
        // SR consumes color after engine tonemapping, but it still requires the
        // current unfiltered HDR here. Running original TAA would smooth it a
        // second time before NGX. Limit the resolve to the qualified active
        // rectangle so texelFetch never reads beyond the smaller private image.
        // A rejected processor must restore current HDR after earlier success.
        // Avoid rewriting unchanged descriptors: that needlessly invalidates
        // previously recorded command buffers even when their views are stable.
        if(resolveViews!=selected){VkDescriptorImageInfo imageInfo[2]{};VkWriteDescriptorSet writes[2]{};
            for(unsigned e=0;e<2;++e){imageInfo[e]={sampler,selected[e],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};writes[e]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[e].dstSet=eyes;writes[e].dstBinding=e;writes[e].descriptorCount=1;writes[e].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;writes[e].pImageInfo=&imageInfo[e];}
            vkUpdateDescriptorSets(device,2,writes,0,nullptr);resolveViews=selected;}
        vkCmdBeginRenderPass(cb,&host,VK_SUBPASS_CONTENTS_INLINE);bind();vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,resolvePipeline);
        if(qualifiedActiveRectangle){vkCmdSetViewport(cb,0,1,&privateViewport);vkCmdSetScissor(cb,0,1,&privateScissor);}
        vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,resolveLayout,1,1,&eyes,0,nullptr);draw();bind();return true;
    }
    ~DoomTemporalBridge(){
        // Host MUST retire recorded GPU readers before destruction.
        if(guidePipeline)vkDestroyPipeline(device,guidePipeline,nullptr);if(resolvePipeline)vkDestroyPipeline(device,resolvePipeline,nullptr);
        clearImages();if(resolveLayout)vkDestroyPipelineLayout(device,resolveLayout,nullptr);if(pool)vkDestroyDescriptorPool(device,pool,nullptr);
        if(eyeLayout)vkDestroyDescriptorSetLayout(device,eyeLayout,nullptr);if(sampler)vkDestroySampler(device,sampler,nullptr);if(privatePass)vkDestroyRenderPass(device,privatePass,nullptr);
    }
#undef KH_BRIDGE_FUNCTIONS
};
}
