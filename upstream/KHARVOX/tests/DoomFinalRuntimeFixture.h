#pragma once
#include "../src/overlay/DoomLayerControl.h"
#include "../src/sfs/DoomFinalPass.h"
#include "../src/sfs/DoomFinalMapping.h"
// Real engine final fragment shader, native interception, and separate GUI.
// Resources are owned by the existing synchronous GPU test fixture.
struct DoomFinalRuntimeFixture {
    using G=VulkanTestDevice;
    G& g;G::Texture target,gui,depth;G::Buffer high,low,guiUpload;
    void *mappedHigh{},*mappedLow{};
    VkDescriptorSetLayout layout{};VkDescriptorPool pool{};VkDescriptorSet set{};
    VkPipelineLayout pipelineLayout{};VkRenderPass pass{};VkFramebuffer framebuffer{};
    VkShaderModule fragment{};VkPipeline pipeline{};bool initialized{};float renderMode{2.f};
    DoomFinalRuntimeFixture(G& gpu,G::Texture scene,VkSampler sampler,VkGraphicsPipelineCreateInfo original,const char* shader,float scale=.75f):g(gpu){
        depth=g.MakeTexture(VK_FORMAT_D24_UNORM_S8_UINT,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,1,VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT);
        target=g.MakeTexture(VK_FORMAT_B10G11R11_UFLOAT_PACK32,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,1);
        gui=g.MakeTexture(VK_FORMAT_R32G32B32A32_SFLOAT,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,1);
        std::vector<float> transparent(size_t(g.extent.width)*g.extent.height*2*4,0);
        // A fully opaque green HUD patch in one corner must bypass reconstruction.
        for(unsigned e=0;e<2;++e)for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){const auto n=(e*g.extent.width*g.extent.height+y*g.extent.width+x)*4;transparent[n+1]=.25f;transparent[n+3]=1;}
        guiUpload=g.MakeBuffer(transparent.size()*4,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);g.Write(guiUpload,transparent.data());
        char mode[8]{};GetEnvironmentVariableA("KHARVOX_TEST_FINAL_MODE",mode,sizeof(mode));if(*mode)renderMode=std::stof(mode);
        // Actual RenderDoc event 2619: .w is sharpening (3), NOT inverse Y;
        // renderMode is 2. Preserve its letterbox and separate HUD behavior.
        std::array<float,12> h{float(g.extent.width),float(g.extent.height),scale/g.extent.width,scale/g.extent.height,1,1,1,1,0,0,0,0};
        std::array<float,12> l{scale,scale,1,3,renderMode,0,0,0,11725,4294,15528,900};
        const VkExtent2D active{uint32_t(std::round(g.extent.width*scale)),uint32_t(std::round(g.extent.height*scale))};
        const VkViewport vp{0,0,float(g.extent.width),float(g.extent.height),0,1};const VkRect2D sc{{0,0},g.extent};
        need(kharvox::sfs::doomFinalMapping(h,l,g.extent,active,g.extent,vp,sc),"Captured final uniforms rejected");
        auto invalid=l;invalid[4]=3;need(!kharvox::sfs::doomFinalMapping(h,invalid,g.extent,active,g.extent,vp,sc),"Classic quantization route admitted");
        auto shifted=vp;shifted.x=1;need(!kharvox::sfs::doomFinalMapping(h,l,g.extent,active,g.extent,shifted,sc),"Shifted final rectangle admitted");
        high=g.MakeBuffer(sizeof(h),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);low=g.MakeBuffer(sizeof(l),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);g.Write(high,h.data());g.Write(low,l.data());
        G::Check(g.vkMapMemory(g.device,high.allocation,0,high.size,0,&mappedHigh));G::Check(g.vkMapMemory(g.device,low.allocation,0,low.size,0,&mappedLow));
        VkDescriptorSetLayoutBinding bindings[4]{};for(unsigned n=0;n<4;++n)bindings[n]={n,n<2?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
        VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};li.bindingCount=4;li.pBindings=bindings;G::Check(g.vkCreateDescriptorSetLayout(g.device,&li,nullptr,&layout));
        VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,2},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,2}};VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pi.maxSets=1;pi.poolSizeCount=2;pi.pPoolSizes=sizes;G::Check(g.vkCreateDescriptorPool(g.device,&pi,nullptr,&pool));
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};ai.descriptorPool=pool;ai.descriptorSetCount=1;ai.pSetLayouts=&layout;G::Check(g.vkAllocateDescriptorSets(g.device,&ai,&set));
        VkDescriptorBufferInfo buffers[]{{high.handle,0,high.size},{low.handle,0,low.size}};VkDescriptorImageInfo images[]{{sampler,scene.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{sampler,gui.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
        VkWriteDescriptorSet writes[4]{};for(unsigned n=0;n<4;++n){auto& w=writes[n];w={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=set;w.dstBinding=n;w.descriptorCount=1;w.descriptorType=bindings[n].descriptorType;if(n<2)w.pBufferInfo=&buffers[n];else w.pImageInfo=&images[n-2];}g.vkUpdateDescriptorSets(g.device,4,writes,0,nullptr);
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pl.setLayoutCount=1;pl.pSetLayouts=&layout;G::Check(g.vkCreatePipelineLayout(g.device,&pl,nullptr,&pipelineLayout));
        VkAttachmentDescription att{};att.format=target.format;att.samples=VK_SAMPLE_COUNT_1_BIT;att.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;att.storeOp=VK_ATTACHMENT_STORE_OP_STORE;att.initialLayout=att.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkAttachmentReference ref{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=&ref;
        VkAttachmentDescription attachments[]{att,att};auto& ds=attachments[1];ds.format=depth.format;ds.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_LOAD;ds.stencilStoreOp=VK_ATTACHMENT_STORE_OP_STORE;ds.initialLayout=ds.finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;VkAttachmentReference depthRef{1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};sub.pDepthStencilAttachment=&depthRef;VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};rp.attachmentCount=2;rp.pAttachments=attachments;rp.subpassCount=1;rp.pSubpasses=&sub;G::Check(g.vkCreateRenderPass(g.device,&rp,nullptr,&pass));
        VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fb.renderPass=pass;VkImageView views[]{target.view,depth.view};fb.attachmentCount=2;fb.pAttachments=views;fb.width=g.extent.width;fb.height=g.extent.height;fb.layers=1;G::Check(g.vkCreateFramebuffer(g.device,&fb,nullptr,&framebuffer));
        need(kharvox::sfs::doomFinalPass(rp),"Real final pass with read-only depth rejected");
        ds.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;need(!kharvox::sfs::doomFinalPass(rp),"Destructive stencil store accepted");ds.stencilStoreOp=VK_ATTACHMENT_STORE_OP_STORE;
        ds.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;need(!kharvox::sfs::doomFinalPass(rp),"Clearing depth admitted for split");ds.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;
        auto code=words(shader);VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};mi.codeSize=code.size()*4;mi.pCode=code.data();G::Check(g.vkCreateShaderModule(g.device,&mi,nullptr,&fragment));
        std::vector<VkPipelineShaderStageCreateInfo> stages(original.pStages,original.pStages+original.stageCount);for(auto& s:stages)if(s.stage==VK_SHADER_STAGE_FRAGMENT_BIT)s.module=fragment;
        auto blend=*original.pColorBlendState;blend.attachmentCount=1;original.pColorBlendState=&blend;original.pStages=stages.data();original.layout=pipelineLayout;original.renderPass=pass;
        G::Check(g.vkCreateGraphicsPipelines(g.device,VK_NULL_HANDLE,1,&original,nullptr,&pipeline));
    }
    void draw(uint64_t frame,PFN_vkCmdSetViewport vp,PFN_vkCmdSetScissor sc){
        if(!initialized){g.Upload(gui,guiUpload,VK_IMAGE_LAYOUT_UNDEFINED);g.Barrier(depth,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT);}
        g.Barrier(target,!initialized?VK_IMAGE_LAYOUT_UNDEFINED:VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        initialized=true;
        VkClearValue ignored{};VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=pass;begin.framebuffer=framebuffer;begin.renderArea.extent=g.extent;begin.clearValueCount=1;begin.pClearValues=&ignored;
        g.vkCmdBeginRenderPass(g.command,&begin,VK_SUBPASS_CONTENTS_INLINE);g.vkCmdBindPipeline(g.command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);g.vkCmdBindDescriptorSets(g.command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout,0,1,&set,0,nullptr);
        VkViewport viewport{0,0,float(g.extent.width),float(g.extent.height),0,1};VkRect2D scissor{{0,0},g.extent};vp(g.command,0,1,&viewport);sc(g.command,0,1,&scissor);g.vkCmdDraw(g.command,3,1,0,0);g.vkCmdEndRenderPass(g.command);
    }
    void release(){g.vkUnmapMemory(g.device,high.allocation);g.vkUnmapMemory(g.device,low.allocation);g.vkDestroyPipeline(g.device,pipeline,nullptr);g.vkDestroyShaderModule(g.device,fragment,nullptr);g.vkDestroyFramebuffer(g.device,framebuffer,nullptr);g.vkDestroyRenderPass(g.device,pass,nullptr);g.vkDestroyPipelineLayout(g.device,pipelineLayout,nullptr);g.vkDestroyDescriptorPool(g.device,pool,nullptr);g.vkDestroyDescriptorSetLayout(g.device,layout,nullptr);}
};
