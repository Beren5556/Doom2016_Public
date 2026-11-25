#include "VulkanTestDevice.h"
#include "../src/sfs/ShaderCompiler.h"
#include "../src/sfs/DoomTemporalResolve.h"
#include <array>
#include <cmath>
#include <fstream>
#include <algorithm>
#include <functional>

static void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static std::vector<uint32_t> readShader(const char* path){
    std::ifstream in(path,std::ios::binary|std::ios::ate);need(bool(in),"Missing shader");
    const auto bytes=in.tellg();need(bytes>=20&&bytes%4==0,"Invalid SPIR-V size");
    std::vector<uint32_t> words(size_t(bytes)/4);in.seekg(0);
    need(bool(in.read(reinterpret_cast<char*>(words.data()),bytes)),"Shader read failed");return words;
}
static uint16_t exactHalf(float f){
    // All fixture colors are positive normal, exactly representable halves.
    uint32_t u;std::memcpy(&u,&f,4);
    need(f>0&&f<16&&(u&0x1fffu)==0,"Fixture color not an exact half");
    return uint16_t((((u>>23)&255u)-112u)<<10|((u>>13)&1023u));
}
static float color(unsigned eye,unsigned x,unsigned y,unsigned channel,unsigned frame){
    if(channel==0)return eye?.125f+float((y+frame)%8)/32:1.f+float((x+frame)%8)/16;
    if(channel==1)return .25f+float((x+2*y+frame)%8)/32;
    if(channel==2)return eye?2.f+float((y+frame)%8)/8:.25f+float((x+y+frame)%8)/32;
    return 1;
}

struct ResolveTest {
    using G=VulkanTestDevice;using Texture=G::Texture;using Buffer=G::Buffer;
    G g;
    std::vector<VkShaderModule> modules;
    std::vector<VkPipeline> pipelines;
    std::vector<VkPipelineLayout> pipelineLayouts;
    std::vector<VkDescriptorSetLayout> layouts;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<VkRenderPass> passes;
    VkSampler sampler{};VkDescriptorPool pool{};
    VkDescriptorSet gameSet{},eyeSet{},copySet{};
    VkPipelineLayout resolveLayout{},copyLayout{};
    VkPipeline originalPipeline{},resolvePipeline{},copyPipeline{};
    VkRenderPass gamePass{},copyPass{};VkFramebuffer gameFramebuffer{},copyFramebuffer{};
    Texture depth,motion,scene,history,secondary,gameColor,gamePacked,copyColor,copyPacked;
    std::array<Texture,2> eyes;
    Buffer high,low,projection,depthUpload,motionUpload,sceneUpload,readColor,readPacked;
    std::array<Buffer,2> eyeUploads;
    std::array<float,56> hi{};std::array<float,28> lo{};std::array<float,96> pr{};
    bool uploaded{};

    VkShaderModule Module(const std::vector<uint32_t>& words){
        VkShaderModuleCreateInfo c{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};c.codeSize=words.size()*4;c.pCode=words.data();
        VkShaderModule m{};G::Check(g.vkCreateShaderModule(g.device,&c,nullptr,&m));modules.push_back(m);return m;
    }
    Texture EyeTexture(){
        Texture t{};t.format=VK_FORMAT_R16G16B16A16_SFLOAT;
        VkImageCreateInfo c{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};c.imageType=VK_IMAGE_TYPE_2D;c.extent={g.extent.width,g.extent.height,1};
        c.mipLevels=c.arrayLayers=1;c.samples=VK_SAMPLE_COUNT_1_BIT;c.format=t.format;c.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        G::Check(g.vkCreateImage(g.device,&c,nullptr,&t.handle));VkMemoryRequirements r{};g.vkGetImageMemoryRequirements(g.device,t.handle,&r);
        VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};a.allocationSize=r.size;a.memoryTypeIndex=g.MemoryType(r.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        G::Check(g.vkAllocateMemory(g.device,&a,nullptr,&t.allocation));G::Check(g.vkBindImageMemory(g.device,t.handle,t.allocation,0));
        VkImageViewCreateInfo v{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};v.image=t.handle;v.format=t.format;v.viewType=VK_IMAGE_VIEW_TYPE_2D;v.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        G::Check(g.vkCreateImageView(g.device,&v,nullptr,&t.view));g.textures.push_back(t);return t;
    }
    VkDescriptorSetLayout Layout(const std::vector<VkDescriptorSetLayoutBinding>& bindings){
        VkDescriptorSetLayoutCreateInfo c{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};c.bindingCount=uint32_t(bindings.size());c.pBindings=bindings.data();
        VkDescriptorSetLayout l{};G::Check(g.vkCreateDescriptorSetLayout(g.device,&c,nullptr,&l));layouts.push_back(l);return l;
    }
    VkDescriptorSet Set(VkDescriptorSetLayout l){
        VkDescriptorSetAllocateInfo a{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};a.descriptorPool=pool;a.descriptorSetCount=1;a.pSetLayouts=&l;
        VkDescriptorSet s{};G::Check(g.vkAllocateDescriptorSets(g.device,&a,&s));return s;
    }
    void ImageBinding(VkDescriptorSet s,unsigned binding,Texture t){
        VkDescriptorImageInfo i{sampler,t.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=s;w.dstBinding=binding;w.descriptorCount=1;w.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;w.pImageInfo=&i;
        g.vkUpdateDescriptorSets(g.device,1,&w,0,nullptr);
    }
    void BufferBinding(unsigned binding,Buffer b){
        VkDescriptorBufferInfo i{b.handle,0,b.size};VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=gameSet;w.dstBinding=binding;w.descriptorCount=1;
        w.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;w.pBufferInfo=&i;g.vkUpdateDescriptorSets(g.device,1,&w,0,nullptr);
    }
    VkPipelineLayout PipelineLayout(const std::vector<VkDescriptorSetLayout>& setLayouts){
        VkPipelineLayoutCreateInfo c{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};c.setLayoutCount=uint32_t(setLayouts.size());c.pSetLayouts=setLayouts.data();
        VkPipelineLayout l{};G::Check(g.vkCreatePipelineLayout(g.device,&c,nullptr,&l));pipelineLayouts.push_back(l);return l;
    }
    VkRenderPass Pass(Texture a,Texture b,bool readback){
        VkAttachmentDescription attachments[2]{};const Texture ts[]{a,b};
        for(unsigned n=0;n<2;++n){auto& t=attachments[n];t.format=ts[n].format;t.samples=VK_SAMPLE_COUNT_1_BIT;t.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;t.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
            t.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;t.finalLayout=readback?VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;}
        VkAttachmentReference refs[]{{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},{1,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
        VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=2;sub.pColorAttachments=refs;
        uint32_t mask=3;VkRenderPassMultiviewCreateInfo mv{VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO};mv.subpassCount=1;mv.pViewMasks=&mask;mv.correlationMaskCount=1;mv.pCorrelationMasks=&mask;
        VkSubpassDependency deps[2]{};deps[0].srcSubpass=VK_SUBPASS_EXTERNAL;deps[0].dstSubpass=0;
        deps[0].srcStageMask=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;deps[0].dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[0].srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;deps[0].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].srcSubpass=0;deps[1].dstSubpass=VK_SUBPASS_EXTERNAL;deps[1].srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[1].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;deps[1].dstStageMask=readback?VK_PIPELINE_STAGE_TRANSFER_BIT:VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[1].dstAccessMask=readback?VK_ACCESS_TRANSFER_READ_BIT:VK_ACCESS_SHADER_READ_BIT;
        VkRenderPassCreateInfo c{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};c.pNext=&mv;c.attachmentCount=2;c.pAttachments=attachments;c.subpassCount=1;c.pSubpasses=&sub;c.dependencyCount=2;c.pDependencies=deps;
        VkRenderPass p{};G::Check(g.vkCreateRenderPass(g.device,&c,nullptr,&p));passes.push_back(p);return p;
    }
    VkFramebuffer Framebuffer(VkRenderPass p,Texture a,Texture b){
        VkImageView views[]{a.view,b.view};VkFramebufferCreateInfo c{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};c.renderPass=p;c.attachmentCount=2;c.pAttachments=views;c.width=g.extent.width;c.height=g.extent.height;c.layers=1;
        VkFramebuffer f{};G::Check(g.vkCreateFramebuffer(g.device,&c,nullptr,&f));framebuffers.push_back(f);return f;
    }
    VkPipeline Pipeline(VkShaderModule vs,VkShaderModule fs,VkRenderPass pass,VkPipelineLayout layout,const std::function<void(const VkGraphicsPipelineCreateInfo&)>& inspect={}){
        VkPipelineShaderStageCreateInfo stages[2]{};for(auto& s:stages){s.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;s.pName="main";}
        stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=vs;stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=fs;
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport viewport{0,0,float(g.extent.width),float(g.extent.height),0,1};VkRect2D scissor{{0,0},g.extent};VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};vp.viewportCount=vp.scissorCount=1;vp.pViewports=&viewport;vp.pScissors=&scissor;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.lineWidth=1;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState colors[2]{};for(auto& c:colors)c.colorWriteMask=15;
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blend.attachmentCount=2;blend.pAttachments=colors;
        VkGraphicsPipelineCreateInfo c{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};c.stageCount=2;c.pStages=stages;c.pVertexInputState=&vi;c.pInputAssemblyState=&ia;c.pViewportState=&vp;c.pRasterizationState=&raster;c.pMultisampleState=&ms;c.pColorBlendState=&blend;c.layout=layout;c.renderPass=pass;
        if(inspect)inspect(c);
        VkPipeline p{};G::Check(g.vkCreateGraphicsPipelines(g.device,VK_NULL_HANDLE,1,&c,nullptr,&p));pipelines.push_back(p);return p;
    }
    ResolveTest(const char* vs,const char* fs,const char* copy,VkExtent2D extent={8,8},G::ExtensionQuery extensions=nullptr):g(false,extent,extensions){
        auto words=readShader(fs);kharvox::sfs::ShaderCompileOptions o;o.profileReplacement=true;
        auto original=kharvox::sfs::compileStereoShader(words,o);need(original.temporalCorrections==1&&!original.temporalResolveApplied,"Original resolve contract unavailable");
        o.temporalResolve=true;auto resolved=kharvox::sfs::compileStereoShader(words,o);need(resolved.temporalResolveApplied,"Private resolve not generated");
        // Rejections are checked before any private shader is used.
        auto rejects=[&](std::string source,unsigned corrections){try{kharvox::sfs::makeDoomTemporalResolve(source,corrections);return false;}catch(const std::runtime_error&){return true;}};
        need(rejects(original.glsl,0),"Uncorrected motion accepted");
        auto altered=original.glsl;const auto packing=altered.find("1.0 / out_FragColor1.x");need(packing!=std::string::npos,"Missing packing anchor");altered.replace(packing,3,"2.0");
        need(rejects(altered,1),"Changed downstream packing accepted");
        altered=original.glsl;const auto channels=altered.find(".w, out_FragColor1.z, out_FragColor1.w");need(channels!=std::string::npos,"Missing channel anchor");altered[channels+1]='y';
        need(rejects(altered,1),"Changed downstream channel selection accepted");
        need(rejects(original.glsl+"\nlayout(set=1,binding=4) uniform sampler2D collision;\n",1),"Private set collision accepted");
        o.temporalGuides=true;bool rejected=false;try{kharvox::sfs::compileStereoShader(words,o);}catch(const std::runtime_error&){rejected=true;}need(rejected,"Mixed temporal variants accepted");
        o.temporalGuides=false;o.monoscopicView=true;rejected=false;try{kharvox::sfs::compileStereoShader(words,o);}catch(const std::runtime_error&){rejected=true;}need(rejected,"Monoscopic resolve accepted");
        const auto vertex=Module(readShader(vs)),baseFragment=Module(original.words),resolveFragment=Module(resolved.words),copyFragment=Module(readShader(copy));
        constexpr auto sampled=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        depth=g.MakeTexture(VK_FORMAT_R32_SFLOAT,sampled);motion=g.MakeTexture(VK_FORMAT_R32G32_SFLOAT,sampled);
        scene=g.MakeTexture(VK_FORMAT_R32G32B32A32_SFLOAT,sampled);history=g.MakeTexture(scene.format,sampled);secondary=g.MakeTexture(scene.format,sampled);
        // Exact observed usage (20 color, 28 packed) and formats. No capability
        // is added to the game outputs to make this test work.
        constexpr auto gameUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
        gameColor=g.MakeTexture(VK_FORMAT_B10G11R11_UFLOAT_PACK32,gameUsage);gamePacked=g.MakeTexture(VK_FORMAT_R16G16_SFLOAT,gameUsage|VK_IMAGE_USAGE_STORAGE_BIT);
        copyColor=g.MakeTexture(VK_FORMAT_R32G32B32A32_SFLOAT,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT);copyPacked=g.MakeTexture(copyColor.format,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        high=g.MakeBuffer(sizeof(hi),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);low=g.MakeBuffer(sizeof(lo),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);projection=g.MakeBuffer(sizeof(pr),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        const VkDeviceSize pixels=VkDeviceSize(g.extent.width)*g.extent.height;
        depthUpload=g.MakeBuffer(pixels*2*4,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);motionUpload=g.MakeBuffer(pixels*2*8,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);sceneUpload=g.MakeBuffer(pixels*2*16,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        readColor=g.MakeBuffer(pixels*2*16,VK_BUFFER_USAGE_TRANSFER_DST_BIT);readPacked=g.MakeBuffer(pixels*2*16,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        for(unsigned e=0;e<2;++e){eyes[e]=EyeTexture();eyeUploads[e]=g.MakeBuffer(pixels*8,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);}
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};si.magFilter=si.minFilter=VK_FILTER_NEAREST;si.addressModeU=si.addressModeV=si.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        G::Check(g.vkCreateSampler(g.device,&si,nullptr,&sampler));
        VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,3},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,9}};
        VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dp.maxSets=3;dp.poolSizeCount=2;dp.pPoolSizes=sizes;G::Check(g.vkCreateDescriptorPool(g.device,&dp,nullptr,&pool));
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        for(unsigned n:{0,1,2,3,4,5,6,31})bindings.push_back({n,(n==0||n==1||n==31)?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr});
        const auto gameLayout=Layout(bindings);bindings.clear();for(unsigned n:{0,1})bindings.push_back({n,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr});
        const auto sampledLayout=Layout(bindings);gameSet=Set(gameLayout);eyeSet=Set(sampledLayout);copySet=Set(sampledLayout);
        BufferBinding(0,high);BufferBinding(1,low);BufferBinding(31,projection);
        ImageBinding(gameSet,2,depth);ImageBinding(gameSet,3,motion);ImageBinding(gameSet,4,scene);ImageBinding(gameSet,5,secondary);ImageBinding(gameSet,6,history);
        for(unsigned e=0;e<2;++e)ImageBinding(eyeSet,e,eyes[e]);ImageBinding(copySet,0,gameColor);ImageBinding(copySet,1,gamePacked);
        resolveLayout=PipelineLayout({gameLayout,sampledLayout});copyLayout=PipelineLayout({sampledLayout});
        gamePass=Pass(gameColor,gamePacked,false);copyPass=Pass(copyColor,copyPacked,true);
        gameFramebuffer=Framebuffer(gamePass,gameColor,gamePacked);copyFramebuffer=Framebuffer(copyPass,copyColor,copyPacked);
        originalPipeline=Pipeline(vertex,baseFragment,gamePass,resolveLayout);resolvePipeline=Pipeline(vertex,resolveFragment,gamePass,resolveLayout);copyPipeline=Pipeline(vertex,copyFragment,copyPass,copyLayout);
        hi[0]=float(g.extent.width);hi[1]=float(g.extent.height);hi[2]=1.f/hi[0];hi[3]=1.f/hi[1];hi[6]=1;hi[8]=hi[10]=.5f;hi[13]=hi[14]=.5f;hi[18]=hi[2];hi[19]=hi[3];
        for(unsigned n=24;n<40;++n)hi[n]=1.f/9;hi[40]=hi[44]=1.f/9;hi[48]=hi[49]=1;
        hi[52]=hi[0];hi[53]=hi[1];hi[54]=1;hi[55]=4;
        lo[3]=1;lo[4]=-1;lo[5]=-1;lo[6]=1;lo[8]=1;lo[9]=-1;lo[10]=1;lo[12]=lo[13]=lo[14]=lo[15]=1;lo[16]=-1;lo[17]=lo[18]=1;lo[20]=lo[21]=lo[22]=1;
        for(unsigned offset:{16u,32u,56u,72u})for(unsigned n=0;n<4;++n)pr[offset+n*5]=1;
    }
    void UploadEyes(){
        for(unsigned e=0;e<2;++e){VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=eyes[e].handle;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            b.oldLayout=uploaded?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
            b.srcAccessMask=uploaded?VK_ACCESS_SHADER_READ_BIT:0;b.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
            g.vkCmdPipelineBarrier(g.command,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
            VkBufferImageCopy c{};c.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};c.imageExtent={g.extent.width,g.extent.height,1};g.vkCmdCopyBufferToImage(g.command,eyeUploads[e].handle,eyes[e].handle,b.newLayout,1,&c);
            b.oldLayout=b.newLayout;b.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
            g.vkCmdPipelineBarrier(g.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&b);}
    }
    void Draw(VkRenderPass pass,VkFramebuffer framebuffer,VkPipeline pipeline,VkPipelineLayout layout,const std::vector<VkDescriptorSet>& sets){
        VkRenderPassBeginInfo b{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};b.renderPass=pass;b.framebuffer=framebuffer;b.renderArea.extent=g.extent;
        g.vkCmdBeginRenderPass(g.command,&b,VK_SUBPASS_CONTENTS_INLINE);g.vkCmdBindPipeline(g.command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
        g.vkCmdBindDescriptorSets(g.command,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,uint32_t(sets.size()),sets.data(),0,nullptr);g.vkCmdDraw(g.command,3,1,0,0);g.vkCmdEndRenderPass(g.command);
    }
    void Readback(){Draw(copyPass,copyFramebuffer,copyPipeline,copyLayout,{copySet});g.Download(copyColor,readColor);g.Download(copyPacked,readPacked);}
    void Run(unsigned scenario){
        need(g.extent.width==8&&g.extent.height==8,"Exact-color fixture requires 8x8");
        std::array<float,128> depths{};std::array<float,256> motions{};std::array<float,512> colors{};
        for(unsigned e=0;e<2;++e)for(unsigned p=0;p<64;++p){const auto n=e*64+p;depths[n]=e?.25f:.5f;
            motions[n*2]=scenario==1?(e?-.375f:.125f):2;motions[n*2+1]=scenario==1?(e?.25f:-.25f):0;
            for(unsigned c=0;c<4;++c)colors[n*4+c]=c==3?1:.125f+float((n+scenario+c)%16)/32;}
        pr[68]=scenario==2?.25f:0;pr[84]=scenario==2?-.5f:0;
        if(scenario==3){depths[3*8+3]=.1f;depths[64+3*8+3]=.05f;hi[54]=.5f;hi[55]=.25f;}
        g.Write(high,hi.data());g.Write(low,lo.data());g.Write(projection,pr.data());g.Write(depthUpload,depths.data());g.Write(motionUpload,motions.data());g.Write(sceneUpload,colors.data());
        for(unsigned e=0;e<2;++e){std::array<uint16_t,256> pixels{};for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)for(unsigned c=0;c<4;++c)pixels[(y*8+x)*4+c]=exactHalf(color(e,x,y,c,scenario));g.Write(eyeUploads[e],pixels.data());}
        g.Begin();const auto old=uploaded?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED;
        g.Upload(depth,depthUpload,old);g.Upload(motion,motionUpload,old);for(auto t:{scene,history,secondary})g.Upload(t,sceneUpload,old);UploadEyes();uploaded=true;
        Draw(gamePass,gameFramebuffer,originalPipeline,resolveLayout,{gameSet,eyeSet});Readback();g.Finish();const auto packedBaseline=g.Read(readPacked);
        g.Begin();Draw(gamePass,gameFramebuffer,resolvePipeline,resolveLayout,{gameSet,eyeSet});Readback();g.Finish();const auto packed=g.Read(readPacked),rgb=g.Read(readColor);
        for(unsigned e=0;e<2;++e)for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){const auto p=e*64+y*8+x;
            for(unsigned c=0;c<3;++c)need(rgb[p*4+c]==color(e,x,y,c,scenario),"Color blended, flipped or assigned to wrong eye/pixel");
            for(unsigned c=0;c<2;++c)need(std::memcmp(&packed[p*4+c],&packedBaseline[p*4+c],sizeof(float))==0,"Packed blur/depth changed from original TAA");
            need(!std::isnan(packed[p*4]),"Fixture blur packing is NaN");
            need(std::isfinite(packed[p*4+1]),"Fixture linear depth invalid");}
        std::cout<<"PASS temporal resolve scenario="<<scenario<<" exact color in both eyes; packed RG bit-identical after R16G16 conversion\n";
    }
    ~ResolveTest(){g.vkQueueWaitIdle(g.queue);for(auto p:pipelines)g.vkDestroyPipeline(g.device,p,nullptr);for(auto p:pipelineLayouts)g.vkDestroyPipelineLayout(g.device,p,nullptr);
        for(auto f:framebuffers)g.vkDestroyFramebuffer(g.device,f,nullptr);for(auto p:passes)g.vkDestroyRenderPass(g.device,p,nullptr);
        if(pool)g.vkDestroyDescriptorPool(g.device,pool,nullptr);for(auto l:layouts)g.vkDestroyDescriptorSetLayout(g.device,l,nullptr);
        if(sampler)g.vkDestroySampler(g.device,sampler,nullptr);for(auto m:modules)g.vkDestroyShaderModule(g.device,m,nullptr);}
};
#if !defined(KHARVOX_DLSS_CHAIN_TEST) && !defined(KHARVOX_TEMPORAL_BRIDGE_TEST)
int main(int argc,char** argv){try{need(argc==4,"Expected vertex, installed TAA profile and readback shader");ResolveTest test(argv[1],argv[2],argv[3]);
    for(unsigned n=0;n<4;++n)test.Run(n);
    std::cout<<"PASS 512 stereo pixels: independent RGBA16F eyes to B10G11R11 + RG16F attachments, exact usages 20/28, no TAA color blend. Synthetic DLSS output; game insertion/jitter/exposure not qualified.\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
#endif
