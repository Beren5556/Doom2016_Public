#include "VulkanTestDevice.h"
#include "../src/sfs/ShaderCompiler.h"
#include "../src/sfs/TemporalAuditCapture.h"
#include <filesystem>
#include <fstream>
#include <array>
#include <cmath>
#include <algorithm>
#ifdef KHARVOX_AUDIT_RUNTIME
#include "../src/sfs/NativeSfs.h"
#include "../src/sfs/TemporalContextVulkan.h"
#include "../src/native/NativeStereo.h"
#include "../src/sfs/ShaderIdentity.h"
#define RUNTIME_FUNCTIONS(X) \
 X(vkCreateImage) X(vkDestroyImage) X(vkCreateImageView) X(vkDestroyImageView) \
 X(vkCreateShaderModule) X(vkDestroyShaderModule) X(vkCreateRenderPass) X(vkDestroyRenderPass) \
 X(vkCreateFramebuffer) X(vkDestroyFramebuffer) X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) \
 X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) \
 X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) X(vkCreateGraphicsPipelines) X(vkDestroyPipeline) \
 X(vkAllocateCommandBuffers) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkDestroyCommandPool) \
 X(vkCmdBeginRenderPass) X(vkCmdEndRenderPass) X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) X(vkCmdDraw) X(vkResetCommandPool)
#endif

#ifdef KHARVOX_AUDIT_RUNTIME
static PFN_vkQueueSubmit actualQueueSubmit;
static uint64_t contextDevice;
static VKAPI_ATTR VkResult VKAPI_CALL contextQueueSubmit(VkQueue q,uint32_t count,const VkSubmitInfo* info,VkFence fence){
    const auto result=actualQueueSubmit(q,count,info,fence);
    kharvox::temporal_context::submitted(contextDevice,q,count,info,fence,result);return result;
}
#endif
static void need(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
static std::vector<uint32_t> readShader(const char* path){std::ifstream file(path,std::ios::binary|std::ios::ate);need(bool(file),"Shader missing");const auto bytes=file.tellg();need(bytes>=20&&bytes%4==0,"Invalid SPIR-V");std::vector<uint32_t> words(size_t(bytes)/4);file.seekg(0);need(bool(file.read(reinterpret_cast<char*>(words.data()),bytes)),"Shader read failed");return words;}
struct AuditTest {
    VulkanTestDevice g{true};
    VkRenderPass pass{};VkFramebuffer framebuffer{};VkDescriptorSetLayout layout{};VkDescriptorPool pool{};VkDescriptorSet set{};
    VkPipelineLayout pipelineLayout{};VkPipeline pipeline{},audited{};VkShaderModule vertex{},fragment{},auditFragment{};VkSampler sampler{};
    VulkanTestDevice::Texture depth,motion,outMotion,outDepth;
    kharvox::sfs::NativeDispatch dispatch;
    std::unique_ptr<kharvox::sfs::TemporalAuditCapture> capture;
    std::filesystem::path path;
    VkDescriptorBufferInfo auditInfo{};
    VulkanTestDevice::Buffer high,low,projection,depthUpload,motionUpload,motionRead,depthRead;
    std::array<float,56> hi{};std::array<float,28> lo{};std::array<float,96> pr{};
    std::array<float,128> depths{};std::array<float,256> motions{};
    uint32_t uniformStride{},highOffset{},lowOffset{};bool dynamicRaster{};
    PFN_vkCmdSetViewport setViewport{};PFN_vkCmdSetScissor setScissor{};
    AuditTest(const char* vs,const char* fs,const char* original=nullptr){
        using G=VulkanTestDevice;
#ifdef KHARVOX_AUDIT_RUNTIME
        path=std::filesystem::temp_directory_path()/("kharvox-audit-runtime-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+".jsonl");
        SetEnvironmentVariableW(L"KHARVOX_TEMPORAL_AUDIT_PATH",path.c_str());
        SetEnvironmentVariableA("KHARVOX_TEMPORAL_CONTEXT","1");
        SetEnvironmentVariableW(L"KHARVOX_SFS_PROFILE",std::filesystem::path(fs).parent_path().c_str());
        kharvox::sfs::TemporalAuditDevice capability{true,g.properties.limits};
        need(kharvox::sfs::initialize(g.device,VK_NULL_HANDLE,g.resolve,g.memory,capability),"Runtime audit initialization failed");
        actualQueueSubmit=g.vkQueueSubmit;contextDevice=reinterpret_cast<uintptr_t>(g.device);g.vkQueueSubmit=contextQueueSubmit;
#define WRAP(n) g.n=reinterpret_cast<PFN_##n>(kharvox::sfs::wrapProc(g.device,#n,reinterpret_cast<PFN_vkVoidFunction>(g.n)));
        RUNTIME_FUNCTIONS(WRAP)
#undef WRAP
        setViewport=reinterpret_cast<PFN_vkCmdSetViewport>(kharvox::sfs::wrapProc(g.device,"vkCmdSetViewport",g.resolve(g.device,"vkCmdSetViewport")));
        setScissor=reinterpret_cast<PFN_vkCmdSetScissor>(kharvox::sfs::wrapProc(g.device,"vkCmdSetScissor",g.resolve(g.device,"vkCmdSetScissor")));
        reinterpret_cast<PFN_vkFreeCommandBuffers>(g.resolve(g.device,"vkFreeCommandBuffers"))(g.device,g.pool,1,&g.command);
        VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=g.pool;ca.commandBufferCount=1;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;G::Check(g.vkAllocateCommandBuffers(g.device,&ca,&g.command));
#endif
        auto code=readShader(fs);kharvox::sfs::ShaderCompileOptions options;options.profileReplacement=true;options.temporalGuides=false;
        auto compiled=kharvox::sfs::compileStereoShader(code,options);need(!compiled.temporalGuidesApplied&&compiled.temporalCorrections==1,"Original temporal resolve unavailable");
        auto module=[&](const auto& words){VkShaderModuleCreateInfo c{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};c.codeSize=words.size()*4;c.pCode=words.data();VkShaderModule m{};G::Check(g.vkCreateShaderModule(g.device,&c,nullptr,&m));return m;};
        vertex=module(readShader(vs));
#ifdef KHARVOX_AUDIT_RUNTIME
        auto gameCode=readShader(original);need(kharvox::sfs::profileHash(gameCode.data(),uint32_t(gameCode.size()*4))==0x61d26e466ae9911aULL,"Original game shader identity mismatch");fragment=module(gameCode);
#else
        fragment=module(compiled.words);options.temporalAudit=true;auto auditCode=kharvox::sfs::compileStereoShader(code,options);need(auditCode.temporalAuditApplied,"Audit variant missing");auditFragment=module(auditCode.words);
#endif
        // Runtime hooks must see game-style one-layer render targets, then
        // create the two layers themselves; pre-expanded arrays classify mono.
#ifdef KHARVOX_AUDIT_RUNTIME
        constexpr uint32_t inputLayers=1;constexpr auto sampledUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
#else
        constexpr uint32_t inputLayers=2;constexpr VkImageUsageFlags sampledUsage=0;
#endif
        depth=g.MakeTexture(VK_FORMAT_R32_SFLOAT,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|sampledUsage,inputLayers);
        motion=g.MakeTexture(VK_FORMAT_R32G32_SFLOAT,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|sampledUsage,inputLayers);
        outMotion=g.MakeTexture(VK_FORMAT_R32G32B32A32_SFLOAT,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,inputLayers);
        outDepth=g.MakeTexture(VK_FORMAT_R32G32B32A32_SFLOAT,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,inputLayers);
        uniformStride=uint32_t(std::max<VkDeviceSize>(256,g.properties.limits.minUniformBufferOffsetAlignment));
        high=g.MakeBuffer(uniformStride*3,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);low=g.MakeBuffer(uniformStride*3,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);projection=g.MakeBuffer(sizeof(pr),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        depthUpload=g.MakeBuffer(sizeof(depths),VK_BUFFER_USAGE_TRANSFER_SRC_BIT);motionUpload=g.MakeBuffer(sizeof(motions),VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        motionRead=g.MakeBuffer(512*sizeof(float),VK_BUFFER_USAGE_TRANSFER_DST_BIT);depthRead=g.MakeBuffer(512*sizeof(float),VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};si.magFilter=si.minFilter=VK_FILTER_NEAREST;si.addressModeU=si.addressModeV=si.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;G::Check(g.vkCreateSampler(g.device,&si,nullptr,&sampler));
        std::array<VkDescriptorSetLayoutBinding,9> bindings{};const unsigned indices[]{0,1,2,3,4,5,6,31,32};
        for(unsigned n=0;n<9;++n){bindings[n].binding=indices[n];bindings[n].descriptorCount=1;bindings[n].stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;bindings[n].descriptorType=n==8?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:(n>=2&&n<=6)?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;}
        VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};dl.bindingCount=unsigned(bindings.size());dl.pBindings=bindings.data();
#ifdef KHARVOX_AUDIT_RUNTIME
        // Exact desktop-observed DOOM layout: two dynamic UBOs and five samplers.
        dl.bindingCount=7;bindings[0].descriptorType=bindings[1].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        highOffset=uniformStride;lowOffset=uniformStride*2;
#endif
        G::Check(g.vkCreateDescriptorSetLayout(g.device,&dl,nullptr,&layout));
        VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,3},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,5},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}};
        VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dp.maxSets=1;dp.poolSizeCount=3;dp.pPoolSizes=sizes;
#ifdef KHARVOX_AUDIT_RUNTIME
        sizes[0]={VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,2};dp.poolSizeCount=2;
#endif
        G::Check(g.vkCreateDescriptorPool(g.device,&dp,nullptr,&pool));
        VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};da.descriptorPool=pool;da.descriptorSetCount=1;da.pSetLayouts=&layout;G::Check(g.vkAllocateDescriptorSets(g.device,&da,&set));
        dispatch.load(g.device,g.resolve);
#ifndef KHARVOX_AUDIT_RUNTIME
        path=std::filesystem::temp_directory_path()/("kharvox-audit-lab-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+".jsonl");
        capture=std::make_unique<kharvox::sfs::TemporalAuditCapture>(g.device,dispatch);capture->initialize(g.memory,g.properties.limits,path,path.wstring()+L".arm");
        auditInfo=capture->allocate(set);capture->retire(0x100000007ULL);
#endif
        VkDescriptorBufferInfo buffers[]{{high.handle,0,sizeof(hi)},{low.handle,0,sizeof(lo)},{projection.handle,0,projection.size}};
        VkDescriptorImageInfo images[]{{sampler,depth.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{sampler,motion.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
        VkWriteDescriptorSet writes[9]{};for(unsigned n=0;n<dl.bindingCount;++n){auto& w=writes[n];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=set;w.dstBinding=indices[n];w.descriptorCount=1;w.descriptorType=bindings[n].descriptorType;if(n>=2&&n<=6)w.pImageInfo=&images[n==3?1:0];else if(n==8)w.pBufferInfo=&auditInfo;else w.pBufferInfo=&buffers[n==7?2:n];}g.vkUpdateDescriptorSets(g.device,dl.bindingCount,writes,0,nullptr);
        VkAttachmentDescription attachments[2]{};for(unsigned n=0;n<2;++n){attachments[n].format=n?outDepth.format:outMotion.format;attachments[n].samples=VK_SAMPLE_COUNT_1_BIT;attachments[n].loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachments[n].storeOp=VK_ATTACHMENT_STORE_OP_STORE;attachments[n].initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;attachments[n].finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;}
        VkAttachmentReference refs[]{{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},{1,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=2;sub.pColorAttachments=refs;
        uint32_t mask=3;VkRenderPassMultiviewCreateInfo mv{VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO};mv.subpassCount=1;mv.pViewMasks=&mask;mv.correlationMaskCount=1;mv.pCorrelationMasks=&mask;
        VkSubpassDependency dependency{};dependency.srcSubpass=0;dependency.dstSubpass=VK_SUBPASS_EXTERNAL;dependency.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;dependency.dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT;dependency.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;dependency.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};rp.pNext=&mv;rp.attachmentCount=2;rp.pAttachments=attachments;rp.subpassCount=1;rp.pSubpasses=&sub;rp.dependencyCount=1;rp.pDependencies=&dependency;
#ifdef KHARVOX_AUDIT_RUNTIME
        rp.pNext=nullptr; // NativeSfs must create its multiview sibling itself.
#endif
        G::Check(g.vkCreateRenderPass(g.device,&rp,nullptr,&pass));
        VkImageView views[]{outMotion.view,outDepth.view};VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fb.renderPass=pass;fb.attachmentCount=2;fb.pAttachments=views;fb.width=fb.height=8;fb.layers=1;G::Check(g.vkCreateFramebuffer(g.device,&fb,nullptr,&framebuffer));
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pl.setLayoutCount=1;pl.pSetLayouts=&layout;G::Check(g.vkCreatePipelineLayout(g.device,&pl,nullptr,&pipelineLayout));
        VkPipelineShaderStageCreateInfo stages[2]{};for(auto& s:stages){s.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;s.pName="main";}stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=vertex;stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=fragment;
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport viewport{0,0,8,8,0,1};VkRect2D scissor{{0,0},{8,8}};VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};vp.viewportCount=vp.scissorCount=1;vp.pViewports=&viewport;vp.pScissors=&scissor;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.lineWidth=1;VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState blendAttachments[2]{};for(auto& a:blendAttachments)a.colorWriteMask=15;VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blend.attachmentCount=2;blend.pAttachments=blendAttachments;
        VkDynamicState dynamicStates[]{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};ds.dynamicStateCount=2;ds.pDynamicStates=dynamicStates;
        VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};pi.stageCount=2;pi.pStages=stages;pi.pVertexInputState=&vi;pi.pInputAssemblyState=&ia;pi.pViewportState=&vp;pi.pRasterizationState=&raster;pi.pMultisampleState=&ms;pi.pColorBlendState=&blend;pi.layout=pipelineLayout;pi.renderPass=pass;G::Check(g.vkCreateGraphicsPipelines(g.device,VK_NULL_HANDLE,1,&pi,nullptr,&pipeline));
#ifdef KHARVOX_AUDIT_RUNTIME
        pi.pDynamicState=&ds;G::Check(g.vkCreateGraphicsPipelines(g.device,VK_NULL_HANDLE,1,&pi,nullptr,&audited));
#endif
#ifndef KHARVOX_AUDIT_RUNTIME
        stages[1].module=auditFragment;G::Check(g.vkCreateGraphicsPipelines(g.device,VK_NULL_HANDLE,1,&pi,nullptr,&audited));
#endif
        for(unsigned n=24;n<48;++n)hi[n]=1.f/9;hi[48]=hi[49]=8;hi[50]=hi[51]=1.f/8;hi[52]=hi[53]=hi[54]=hi[55]=1;
        // A centered perspective with unit viewport scale. Depth 0.5/0.25
        // reconstructs Z=2/4; previous matrices map it back to the same UV.
        hi[0]=hi[1]=8;hi[2]=hi[3]=1.f/8;hi[6]=1;hi[8]=hi[10]=.5f;hi[13]=hi[14]=.5f;hi[18]=hi[19]=1.f/8;
        lo[3]=1;lo[4]=-1;lo[5]=-1;lo[6]=1;lo[8]=1;lo[9]=-1;lo[10]=1;lo[12]=lo[13]=lo[14]=lo[15]=1;lo[16]=-1;lo[17]=lo[18]=1;lo[20]=lo[21]=lo[22]=1;
        for(unsigned offset:{16u,32u,56u,72u})for(unsigned n=0;n<4;++n)pr[offset+n*5]=1;
    }
    void Retire(uint64_t serial){
#ifdef KHARVOX_AUDIT_RUNTIME
        kharvox::native::FramePose pose;pose.serial=serial;pose.head.orientation.w=1;pose.worldScale=1;
        const XrFovf fov{-.7f,.7f,.7f,-.7f};for(auto& eye:pose.views){eye.pose.orientation.w=1;eye.fov=fov;}
        kharvox::sfs::prepare(g.device,pose,fov);kharvox::sfs::copyCompleted(g.device);kharvox::sfs::beginFrame(g.device);
#else
        capture->retire(serial);
#endif
    }
    std::array<std::vector<float>,2> Draw(bool audit,unsigned repeats=1,bool alternateOffsets=false){
        for(unsigned eye=0;eye<2;++eye)for(unsigned n=0;n<64;++n){depths[eye*64+n]=eye?.25f:.5f;motions[(eye*64+n)*2]=eye?-.375f:.125f;motions[(eye*64+n)*2+1]=-.25f;}
        std::vector<float> highData(size_t(high.size)/4,-999.f),lowData(size_t(low.size)/4,-999.f);
        std::memcpy(reinterpret_cast<char*>(highData.data())+highOffset,hi.data(),sizeof(hi));std::memcpy(reinterpret_cast<char*>(lowData.data())+lowOffset,lo.data(),sizeof(lo));
        if(alternateOffsets){auto alternateHigh=hi;auto alternateLow=lo;alternateHigh[24]+=.125f;alternateLow[12]+=.125f;
            std::memcpy(reinterpret_cast<char*>(highData.data())+uniformStride*3-highOffset,alternateHigh.data(),sizeof(hi));
            std::memcpy(reinterpret_cast<char*>(lowData.data())+uniformStride*3-lowOffset,alternateLow.data(),sizeof(lo));}
        g.Write(high,highData.data());g.Write(low,lowData.data());g.Write(projection,pr.data());g.Write(depthUpload,depths.data());g.Write(motionUpload,motions.data());
        g.Begin();g.Upload(depth,depthUpload,VK_IMAGE_LAYOUT_UNDEFINED);g.Upload(motion,motionUpload,VK_IMAGE_LAYOUT_UNDEFINED);
        VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=pass;begin.framebuffer=framebuffer;begin.renderArea.extent={8,8};g.vkCmdBeginRenderPass(g.command,&begin,VK_SUBPASS_CONTENTS_INLINE);
#ifdef KHARVOX_AUDIT_RUNTIME
        g.vkCmdBindPipeline(g.command,VK_PIPELINE_BIND_POINT_GRAPHICS,dynamicRaster?audited:pipeline);
        if(dynamicRaster){const VkViewport viewport{0,0,8,8,0,1};const VkRect2D scissor{{0,0},{8,8}};setViewport(g.command,0,1,&viewport);setScissor(g.command,0,1,&scissor);}
#else
        g.vkCmdBindPipeline(g.command,VK_PIPELINE_BIND_POINT_GRAPHICS,audit&&audited?audited:pipeline);
#endif
        const uint32_t offsets[]{highOffset,lowOffset};g.vkCmdBindDescriptorSets(g.command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout,0,1,&set,
#ifdef KHARVOX_AUDIT_RUNTIME
            2,offsets
#else
            0,nullptr
#endif
        );
        for(unsigned i=0;i<repeats;++i){
#ifdef KHARVOX_AUDIT_RUNTIME
            if(alternateOffsets&&i){const uint32_t alternate[]{uniformStride*3-highOffset,uniformStride*3-lowOffset};g.vkCmdBindDescriptorSets(g.command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout,0,1,&set,2,alternate);}
#endif
            g.vkCmdDraw(g.command,3,1,0,0);}
        g.vkCmdEndRenderPass(g.command);if(capture)capture->hostBarrier(g.command);
        g.Download(outMotion,motionRead);g.Download(outDepth,depthRead);g.Finish();return {g.Read(motionRead),g.Read(depthRead)};
    }
    void CheckLastSample(unsigned fromEnd=0){
        std::ifstream file(path);std::string line,last;std::vector<std::string> batches;
        while(std::getline(file,line))if(line.find("\"kind\":\"batch\"")!=std::string::npos)batches.push_back(line);
        need(batches.size()>fromEnd,"Missing requested GPU batch");last=batches[batches.size()-1-fromEnd];
        const auto start=last.find("\"records\":[");need(start!=std::string::npos,"GPU capture missing");
        std::vector<uint32_t> words;
        for(size_t i=start+11;i<last.size();){if(last[i]>='0'&&last[i]<='9'){size_t used{};words.push_back(uint32_t(std::stoul(last.substr(i),&used)));i+=used;}else ++i;}
        need(words.size()==320,"Expected exactly one sample per eye");unsigned mask{};
        for(unsigned n=0;n<2;++n){const auto* r=words.data()+160*n;need(r[0]<2&&r[1]==2,"Invalid eye/schema");const unsigned eye=r[0];mask|=1u<<eye;
#ifdef KHARVOX_AUDIT_RUNTIME
            const uint64_t operation=uint64_t(r[2])|(uint64_t(r[3])<<32);
            need(operation&&last.find("\"operationId\":\""+std::to_string(operation)+"\"")!=std::string::npos,"GPU operation token does not match CPU draw");
            need(last.find("\"dynamicOffsets\":["+std::to_string(highOffset)+","+std::to_string(lowOffset)+"]")!=std::string::npos,"Operation lost dynamic UBO offsets");
            need(last.find("\"viewport\":[0,0,8,8,0,1]")!=std::string::npos&&last.find("\"scissor\":[0,0,8,8]")!=std::string::npos,"Operation lost raster state");
            need(last.find(std::string("\"viewportSource\":\"")+(dynamicRaster?"dynamic":"static")+"\"")!=std::string::npos,"Wrong viewport source");
            need(last.find("\"descriptorGenerationMatches\":true")!=std::string::npos,"Operation descriptor generation differs");
#endif
            need(!std::memcmp(r+4,hi.data(),sizeof(hi))&&!std::memcmp(r+60,lo.data(),sizeof(lo)),"GPU constants differ from submitted high/low buffers");
            need(!std::memcmp(r+88,pr.data()+16+eye*16,64)&&!std::memcmp(r+104,pr.data()+48+eye*4,16),"Current eye projection mismatch");
            need(!std::memcmp(r+108,pr.data()+56+eye*16,64)&&!std::memcmp(r+124,pr.data()+88+eye*4,16),"Previous eye projection mismatch");
            for(unsigned t=128;t<148;t+=4)need(r[t]==8&&r[t+1]==8&&r[t+2]==2,"GPU texture dimensions incorrect");
        }need(mask==3,"Missing independent eye capture");
    }
    void RepeatCommand(){
        VulkanTestDevice::Check(g.vkResetFences(g.device,1,&g.done));VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&g.command;
        VulkanTestDevice::Check(g.vkQueueSubmit(g.queue,1,&submit,g.done));VulkanTestDevice::Check(g.vkQueueWaitIdle(g.queue));
    }
    ~AuditTest(){g.vkQueueWaitIdle(g.queue);capture.reset();if(audited)g.vkDestroyPipeline(g.device,audited,nullptr);if(auditFragment)g.vkDestroyShaderModule(g.device,auditFragment,nullptr);if(pipeline)g.vkDestroyPipeline(g.device,pipeline,nullptr);if(pipelineLayout)g.vkDestroyPipelineLayout(g.device,pipelineLayout,nullptr);if(framebuffer)g.vkDestroyFramebuffer(g.device,framebuffer,nullptr);if(pass)g.vkDestroyRenderPass(g.device,pass,nullptr);if(pool)g.vkDestroyDescriptorPool(g.device,pool,nullptr);if(layout)g.vkDestroyDescriptorSetLayout(g.device,layout,nullptr);if(sampler)g.vkDestroySampler(g.device,sampler,nullptr);if(vertex)g.vkDestroyShaderModule(g.device,vertex,nullptr);if(fragment)g.vkDestroyShaderModule(g.device,fragment,nullptr);
#ifdef KHARVOX_AUDIT_RUNTIME
        kharvox::sfs::shutdown(g.device);
#define RESTORE(n) g.n=reinterpret_cast<PFN_##n>(g.resolve(g.device,#n));
        RUNTIME_FUNCTIONS(RESTORE)
#undef RESTORE
#endif
    }
};
int main(int argc,char** argv){try{
#ifdef KHARVOX_AUDIT_RUNTIME
    need(argc==4,"Expected vertex, profile and original game TAA shader");
    SetEnvironmentVariableA("KHARVOX_SFS_NATIVE_PROBE","1");SetEnvironmentVariableA("KHARVOX_SFS_NATIVE_VR","1");
    AuditTest t(argv[1],argv[2],argv[3]);
    t.Retire(1);t.Draw(true);t.Retire(2);
    {std::ifstream file(t.path);std::string text((std::istreambuf_iterator<char>(file)),{});need(text.find("\"kind\":\"batch\"")==std::string::npos,"Runtime capture ran before arming");}
    const auto arm=std::filesystem::path(t.path.wstring()+L".arm");{std::ofstream marker(arm);marker<<"GPU runtime fixture only";}
    for(unsigned i=0;i<60;++i)t.Retire(0x100000007ULL);
    t.Draw(true);t.Retire(0x100000008ULL);t.CheckLastSample();
    // Rebind different nonzero offsets in the SAME descriptor set. No descriptor
    // update is issued. The original shader/profile/runtime must read new data.
    t.highOffset=t.uniformStride*2;t.lowOffset=t.uniformStride;t.hi[24]=.375f;t.lo[12]=.75f;
    t.dynamicRaster=true;t.Draw(true);t.Retire(0x100000009ULL);t.CheckLastSample();
    t.RepeatCommand();t.Retire(0x10000000aULL);t.CheckLastSample();
    // Two operations in one command, same descriptor set, different effective
    // offsets. Each must get its own GPU pair, not one ambiguous four-record set.
    t.Draw(true,2,true);t.Retire(0x10000000bULL);
    t.CheckLastSample(1);std::swap(t.highOffset,t.lowOffset);t.hi[24]+=.125f;t.lo[12]+=.125f;t.CheckLastSample();
    std::swap(t.highOffset,t.lowOffset);t.hi[24]-=.125f;t.lo[12]-=.125f;
    // Reset/re-record after GPU completion but BEFORE the evidence is retired.
    // Both recordings must remain independently readable until that boundary.
    t.Draw(true);t.Draw(true);t.Retire(0x10000000cULL);t.CheckLastSample();t.CheckLastSample(1);
    for(unsigned i=0;i<27;++i){t.Draw(true);t.Retire(20+i);}
    const auto contextPath=std::filesystem::path(t.path.wstring()+L".context.jsonl");
    const auto contextBytes=std::filesystem::file_size(contextPath);{std::ifstream c(contextPath);std::string context((std::istreambuf_iterator<char>(c)),{});
        need(context.find("\"intervals\":12,\"losses\":0,\"exceptions\":0")!=std::string::npos,"Context capture incomplete");
        need(context.find("\"kind\":\"draw\"")!=std::string::npos&&context.find("\"subpasses\"")!=std::string::npos,"Context lost draws or render pass definitions");}
    const auto size=std::filesystem::file_size(t.path);t.Draw(true);t.Retire(99);need(std::filesystem::file_size(t.path)==size,"Runtime capture did not stop");need(std::filesystem::file_size(contextPath)==contextBytes,"Context wrote after its bound");
    std::ifstream file(t.path);std::string text((std::istreambuf_iterator<char>(file)),{});
    need(text.find("\"observedBatches\":32")!=std::string::npos,"Runtime capture never completed");
    need(text.find("\"dynamic\":true,\"effectiveOffsetQualified\":false")!=std::string::npos,"Dynamic descriptor metadata was misqualified");
    std::filesystem::remove(arm);
    need(text.find("\"exhaustedOperations\":0")!=std::string::npos,"Operation slices were exhausted");
    // Saturation uses the disabled slice and must never overwrite a recording
    // retained by another executable command buffer.
    const auto capacityPath=std::filesystem::path(t.path.wstring()+L".capacity");
    kharvox::sfs::TemporalAuditCapture bounded(t.g.device,t.dispatch);bounded.initialize(t.g.memory,t.g.properties.limits,capacityPath,{},true);
    const auto fake=reinterpret_cast<VkDescriptorSet>(uintptr_t(1));bounded.allocate(fake);bounded.retire(1);
    kharvox::sfs::TemporalAuditOperation op;std::vector<uint32_t> retained;
    for(unsigned i=0;i<319;++i)need(bounded.operation(fake,op,retained)!=0,"Premature operation exhaustion");
    need(bounded.operation(fake,op,retained)==0,"Operation capture exceeded its bound");
    bounded.releaseOperations(retained);bounded.retire(2);need(bounded.operation(fake,op,retained)!=0,"Retired operation slices were not reclaimed");
    std::cout<<"PASS actual game shader selected by NativeSfs hooks; per-operation GPU tokens and dynamic offsets; static/dynamic raster state; executable command reuse; arming and 32-batch stop. Evidence: "<<t.path.string()<<'\n';
    return 0;
#else
    need(argc==3,"Expected vertex fixture and exact TAA profile");AuditTest t(argv[1],argv[2]);
    t.Draw(true);t.capture->retire(1);
    {std::ifstream file(t.path);std::string text((std::istreambuf_iterator<char>(file)),{});need(text.find("\"kind\":\"batch\"")==std::string::npos,"Capture ran before arming");}
    // Freed descriptors can be recycled only at the retired boundary. Churn
    // before gameplay must not consume the entire fixed slice budget.
    for(uintptr_t i=1;i<=128;++i){const auto set=reinterpret_cast<VkDescriptorSet>(i);need(t.capture->allocate(set).offset!=0,"Retired descriptor slices were not recycled");t.capture->forget(set);t.capture->retire(1);}
    const auto arm=std::filesystem::path(t.path.wstring()+L".arm");{std::ofstream marker(arm);marker<<"Synthetic GPU test only\n";}
    for(unsigned n=0;n<60;++n)t.capture->retire(0x100000007ULL);
    const auto base=t.Draw(false),audit=t.Draw(true);
    // Original profile writes RGB at location 0 and RG at location 1. Its
    // remaining channels are undefined and cannot be compared as evidence.
    for(unsigned a=0;a<2;++a)for(unsigned n=0;n<128;++n)for(unsigned c=0;c<(a?2u:3u);++c){const auto index=n*4+c;need(!std::memcmp(&base[a][index],&audit[a][index],4),"Audit changed a defined TAA output");if(!std::isfinite(audit[a][index]))throw std::runtime_error("Nonfinite defined output attachment="+std::to_string(a)+" index="+std::to_string(index));}
    t.capture->retire(0x100000008ULL);
    t.CheckLastSample();
    // Changing uniforms after the first execution must be visible in the next
    // GPU sample, with the actual eye-specific projection and unchanged outputs.
    t.hi[24]=.375f;t.lo[12]=.75f;t.pr[68]=.125f;t.pr[84]=-.25f;
    t.Draw(true);t.capture->retire(0x100000009ULL);
    t.CheckLastSample();
    t.Draw(true,6);t.capture->retire(0x10000000aULL);
    for(unsigned n=0;n<29;++n){t.Draw(true);t.capture->retire(20+n);}
    const auto before=std::filesystem::file_size(t.path);t.Draw(true);t.capture->retire(99);
    need(std::filesystem::file_size(t.path)==before,"Capture did not stop at its budget");
    std::ifstream file(t.path);std::string text((std::istreambuf_iterator<char>(file)),{});
    need(text.find("\"count\":12,\"overflow\":true")!=std::string::npos,"Overflow was not reported");
    need(text.find("\"observedBatches\":32")!=std::string::npos,"Missing bounded completion");
    need(text.find("4294967303")!=std::string::npos&&text.find("4294967304")!=std::string::npos,"64-bit frame serial lost");
    std::filesystem::remove(arm);
    std::cout<<"PASS defined TAA outputs bit-identical; actual GPU constants and dimensions checked in both eyes across updates; bounded overflow and automatic stop. Evidence: "<<t.path.string()<<'\n';
    return 0;
#endif
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
