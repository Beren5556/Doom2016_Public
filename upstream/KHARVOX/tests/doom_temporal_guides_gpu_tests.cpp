#include "VulkanTestDevice.h"
#include "../src/sfs/ShaderCompiler.h"
#include <fstream>
#include <array>
#include <cmath>
#include <algorithm>

static void need(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
static std::vector<uint32_t> readShader(const char* path){std::ifstream file(path,std::ios::binary|std::ios::ate);need(bool(file),"Shader missing");const auto bytes=file.tellg();need(bytes>=20&&bytes%4==0,"Invalid SPIR-V");std::vector<uint32_t> words(size_t(bytes)/4);file.seekg(0);need(bool(file.read(reinterpret_cast<char*>(words.data()),bytes)),"Shader read failed");return words;}
struct GuideTest {
    VulkanTestDevice g;
    VkRenderPass pass{};VkFramebuffer framebuffer{};VkDescriptorSetLayout layout{};VkDescriptorPool pool{};VkDescriptorSet set{};
    VkPipelineLayout pipelineLayout{};VkPipeline pipeline{};VkShaderModule vertex{},fragment{};VkSampler sampler{};
    VulkanTestDevice::Texture depth,motion,outMotion,outDepth;
    VulkanTestDevice::Buffer high,low,projection,depthUpload,motionUpload,motionRead,depthRead;
    std::array<float,56> hi{};std::array<float,28> lo{};std::array<float,96> pr{};
    std::array<float,128> depths{};std::array<float,256> motions{};
    GuideTest(const char* vs,const char* fs){
        using G=VulkanTestDevice;
        auto code=readShader(fs);kharvox::sfs::ShaderCompileOptions options;options.profileReplacement=true;options.temporalGuides=true;
        auto compiled=kharvox::sfs::compileStereoShader(code,options);need(compiled.temporalGuidesApplied&&compiled.temporalCorrections==1,"Exact temporal profile was not transformed");
        auto module=[&](const auto& words){VkShaderModuleCreateInfo c{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};c.codeSize=words.size()*4;c.pCode=words.data();VkShaderModule m{};G::Check(g.vkCreateShaderModule(g.device,&c,nullptr,&m));return m;};
        vertex=module(readShader(vs));fragment=module(compiled.words);
        depth=g.MakeTexture(VK_FORMAT_R32_SFLOAT,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        motion=g.MakeTexture(VK_FORMAT_R32G32_SFLOAT,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        outMotion=g.MakeTexture(VK_FORMAT_R32G32_SFLOAT,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        outDepth=g.MakeTexture(VK_FORMAT_R32_SFLOAT,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        high=g.MakeBuffer(sizeof(hi),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);low=g.MakeBuffer(sizeof(lo),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);projection=g.MakeBuffer(sizeof(pr),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        depthUpload=g.MakeBuffer(sizeof(depths),VK_BUFFER_USAGE_TRANSFER_SRC_BIT);motionUpload=g.MakeBuffer(sizeof(motions),VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        motionRead=g.MakeBuffer(sizeof(motions),VK_BUFFER_USAGE_TRANSFER_DST_BIT);depthRead=g.MakeBuffer(sizeof(depths),VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};si.magFilter=si.minFilter=VK_FILTER_NEAREST;si.addressModeU=si.addressModeV=si.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;G::Check(g.vkCreateSampler(g.device,&si,nullptr,&sampler));
        std::array<VkDescriptorSetLayoutBinding,5> bindings{};const unsigned indices[]{0,1,2,3,31};
        for(unsigned n=0;n<5;++n){bindings[n].binding=indices[n];bindings[n].descriptorCount=1;bindings[n].stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;bindings[n].descriptorType=(n==2||n==3)?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;}
        VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};dl.bindingCount=unsigned(bindings.size());dl.pBindings=bindings.data();G::Check(g.vkCreateDescriptorSetLayout(g.device,&dl,nullptr,&layout));
        VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,3},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,2}};
        VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dp.maxSets=1;dp.poolSizeCount=2;dp.pPoolSizes=sizes;G::Check(g.vkCreateDescriptorPool(g.device,&dp,nullptr,&pool));
        VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};da.descriptorPool=pool;da.descriptorSetCount=1;da.pSetLayouts=&layout;G::Check(g.vkAllocateDescriptorSets(g.device,&da,&set));
        VkDescriptorBufferInfo buffers[]{{high.handle,0,high.size},{low.handle,0,low.size},{projection.handle,0,projection.size}};
        VkDescriptorImageInfo images[]{{sampler,depth.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{sampler,motion.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
        VkWriteDescriptorSet writes[5]{};for(unsigned n=0;n<5;++n){auto& w=writes[n];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=set;w.dstBinding=indices[n];w.descriptorCount=1;w.descriptorType=bindings[n].descriptorType;if(n==2||n==3)w.pImageInfo=&images[n-2];else w.pBufferInfo=&buffers[n==4?2:n];}g.vkUpdateDescriptorSets(g.device,5,writes,0,nullptr);
        VkAttachmentDescription attachments[2]{};for(unsigned n=0;n<2;++n){attachments[n].format=n?outDepth.format:outMotion.format;attachments[n].samples=VK_SAMPLE_COUNT_1_BIT;attachments[n].loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachments[n].storeOp=VK_ATTACHMENT_STORE_OP_STORE;attachments[n].initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;attachments[n].finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;}
        VkAttachmentReference refs[]{{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},{1,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=2;sub.pColorAttachments=refs;
        uint32_t mask=3;VkRenderPassMultiviewCreateInfo mv{VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO};mv.subpassCount=1;mv.pViewMasks=&mask;mv.correlationMaskCount=1;mv.pCorrelationMasks=&mask;
        VkSubpassDependency dependency{};dependency.srcSubpass=0;dependency.dstSubpass=VK_SUBPASS_EXTERNAL;dependency.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;dependency.dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT;dependency.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;dependency.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};rp.pNext=&mv;rp.attachmentCount=2;rp.pAttachments=attachments;rp.subpassCount=1;rp.pSubpasses=&sub;rp.dependencyCount=1;rp.pDependencies=&dependency;G::Check(g.vkCreateRenderPass(g.device,&rp,nullptr,&pass));
        VkImageView views[]{outMotion.view,outDepth.view};VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fb.renderPass=pass;fb.attachmentCount=2;fb.pAttachments=views;fb.width=fb.height=8;fb.layers=1;G::Check(g.vkCreateFramebuffer(g.device,&fb,nullptr,&framebuffer));
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pl.setLayoutCount=1;pl.pSetLayouts=&layout;G::Check(g.vkCreatePipelineLayout(g.device,&pl,nullptr,&pipelineLayout));
        VkPipelineShaderStageCreateInfo stages[2]{};for(auto& s:stages){s.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;s.pName="main";}stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=vertex;stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=fragment;
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport viewport{0,0,8,8,0,1};VkRect2D scissor{{0,0},{8,8}};VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};vp.viewportCount=vp.scissorCount=1;vp.pViewports=&viewport;vp.pScissors=&scissor;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.lineWidth=1;VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState blendAttachments[2]{};for(auto& a:blendAttachments)a.colorWriteMask=15;VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blend.attachmentCount=2;blend.pAttachments=blendAttachments;
        VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};pi.stageCount=2;pi.pStages=stages;pi.pVertexInputState=&vi;pi.pInputAssemblyState=&ia;pi.pViewportState=&vp;pi.pRasterizationState=&raster;pi.pMultisampleState=&ms;pi.pColorBlendState=&blend;pi.layout=pipelineLayout;pi.renderPass=pass;G::Check(g.vkCreateGraphicsPipelines(g.device,VK_NULL_HANDLE,1,&pi,nullptr,&pipeline));
        // A centered perspective with unit viewport scale. Depth 0.5/0.25
        // reconstructs Z=2/4; previous matrices map it back to the same UV.
        hi[0]=hi[1]=8;hi[2]=hi[3]=1.f/8;hi[6]=1;hi[8]=hi[10]=.5f;hi[13]=hi[14]=.5f;hi[18]=hi[19]=1.f/8;
        lo[3]=1;lo[4]=-1;lo[5]=-1;lo[6]=1;lo[8]=1;lo[9]=-1;lo[10]=1;lo[12]=lo[13]=lo[14]=lo[15]=1;lo[16]=-1;lo[17]=lo[18]=1;lo[20]=lo[21]=lo[22]=1;
        for(unsigned offset:{16u,32u,56u,72u})for(unsigned n=0;n<4;++n)pr[offset+n*5]=1;
    }
    void Run(unsigned scenario){
        for(unsigned eye=0;eye<2;++eye)for(unsigned n=0;n<64;++n){depths[eye*64+n]=eye?.25f:.5f;motions[(eye*64+n)*2]=scenario==1?(eye?-.375f:.125f):2.f;motions[(eye*64+n)*2+1]=scenario==1?(eye?.25f:-.25f):0;}
        pr[68]=scenario==2?.25f:0;pr[84]=scenario==2?-.5f:0;
        if(scenario==3){depths[3*8+3]=.1f;depths[64+3*8+3]=.05f;}
        g.Write(high,hi.data());g.Write(low,lo.data());g.Write(projection,pr.data());g.Write(depthUpload,depths.data());g.Write(motionUpload,motions.data());
        g.Begin();g.Upload(depth,depthUpload,scenario?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED);g.Upload(motion,motionUpload,scenario?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED);
        VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=pass;begin.framebuffer=framebuffer;begin.renderArea.extent={8,8};g.vkCmdBeginRenderPass(g.command,&begin,VK_SUBPASS_CONTENTS_INLINE);g.vkCmdBindPipeline(g.command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);g.vkCmdBindDescriptorSets(g.command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout,0,1,&set,0,nullptr);g.vkCmdDraw(g.command,3,1,0,0);g.vkCmdEndRenderPass(g.command);
        g.Download(outMotion,motionRead);g.Download(outDepth,depthRead);g.Finish();const auto mv=g.Read(motionRead),z=g.Read(depthRead);
        for(unsigned eye=0;eye<2;++eye)for(unsigned y=2;y<6;++y)for(unsigned x=2;x<6;++x){const unsigned n=eye*64+y*8+x;const float ex=scenario==1?(eye?3.f:-1.f):scenario==2?(eye?-2.f:1.f):0;const float ey=scenario==1?(eye?-2.f:2.f):0;
            need(std::isfinite(mv[n*2])&&std::abs(mv[n*2]-ex)<.0001f&&std::abs(mv[n*2+1]-ey)<.0001f,"Incorrect eye-local motion sign/scale/reprojection");
            // The installed profile uses textureGatherOffsets with four zero
            // offsets: GLSL selects i0,j0 four times, not the four neighboring
            // texels of textureGather. Preserve and test that exact behavior.
            // https://github.khronos.org/Vulkan-Site/glsl/latest/chapters/builtinfunctions.html
            const bool foreground=scenario==3&&x==3&&y==3;const float expected=foreground?(eye?.05f:.1f):(eye?.25f:.5f);
            if(std::abs(z[n]-expected)>=.0001f){std::cerr<<"Depth eye="<<eye<<" x="<<x<<" y="<<y<<" actual="<<z[n]<<" expected="<<expected<<'\n';throw std::runtime_error("Depth did not preserve the nearest gather-selected value");}}
        std::cout<<"PASS temporal guides scenario="<<scenario<<" both array layers\n";
    }
    ~GuideTest(){g.vkQueueWaitIdle(g.queue);if(pipeline)g.vkDestroyPipeline(g.device,pipeline,nullptr);if(pipelineLayout)g.vkDestroyPipelineLayout(g.device,pipelineLayout,nullptr);if(framebuffer)g.vkDestroyFramebuffer(g.device,framebuffer,nullptr);if(pass)g.vkDestroyRenderPass(g.device,pass,nullptr);if(pool)g.vkDestroyDescriptorPool(g.device,pool,nullptr);if(layout)g.vkDestroyDescriptorSetLayout(g.device,layout,nullptr);if(sampler)g.vkDestroySampler(g.device,sampler,nullptr);if(vertex)g.vkDestroyShaderModule(g.device,vertex,nullptr);if(fragment)g.vkDestroyShaderModule(g.device,fragment,nullptr);}
};
int main(int argc,char** argv){try{need(argc==3,"Expected vertex fixture and exact installed TAA profile");GuideTest test(argv[1],argv[2]);for(unsigned n=0;n<4;++n)test.Run(n);std::cout<<"PASS actual transformed fragment on GPU: static camera, object vectors, independent previous-eye matrices and dilated device depth. Runtime jitter/scale remain unqualified.\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
