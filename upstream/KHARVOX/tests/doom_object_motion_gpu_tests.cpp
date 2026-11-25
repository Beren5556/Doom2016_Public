#include "VulkanTestDevice.h"
#include "../src/sfs/ShaderCompiler.h"
#include "../src/sfs/FrameProjection.h"
#include "../src/sfs/ShaderIdentity.h"
#include <fstream>
#include <array>
#include <cmath>

using namespace kharvox::sfs;
using G=VulkanTestDevice;
static void need(bool b,const char* why){if(!b)throw std::runtime_error(why);}
static std::vector<uint32_t> words(const char* path){std::ifstream f(path,std::ios::binary|std::ios::ate);need(bool(f),"Missing original shader");auto n=f.tellg();need(n>=20&&n%4==0,"Bad shader size");std::vector<uint32_t> w(size_t(n)/4);f.seekg(0);need(bool(f.read(reinterpret_cast<char*>(w.data()),n)),"Shader read failed");return w;}
struct Test {
    G g{false,{16,20}};
    VkDescriptorSetLayout setLayout{};VkDescriptorPool pool{};VkDescriptorSet set{};VkPipelineLayout layout{};
    VkRenderPass pass{};VkFramebuffer fb{};VkSampler sampler{};
    std::vector<VkShaderModule> modules;std::vector<VkPipeline> pipelines;
    G::Buffer high,projection,vertices,read,noiseUpload;G::Texture output,noise;
    std::array<float,52> hi{};FrameUniforms eyes;
    VkShaderModule Module(const std::vector<uint32_t>& w){VkShaderModuleCreateInfo c{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};c.codeSize=w.size()*4;c.pCode=w.data();VkShaderModule m{};G::Check(g.vkCreateShaderModule(g.device,&c,nullptr,&m));modules.push_back(m);return m;}
    G::Buffer Uniform(unsigned binding,const void* data,size_t bytes){auto b=g.MakeBuffer(bytes,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);g.Write(b,data);VkDescriptorBufferInfo i{b.handle,0,bytes};VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=set;w.dstBinding=binding;w.descriptorCount=1;w.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;w.pBufferInfo=&i;g.vkUpdateDescriptorSets(g.device,1,&w,0,nullptr);return b;}
    Test(const char* vs,const char* fs){
        auto vertex=words(vs),fragment=words(fs);
        const auto hash=profileHash(vertex.data(),uint32_t(vertex.size()*4));const bool opaque=hash==0xd271df747727c7efULL;
        need(hash==0xc72e22537cca7eedULL||opaque,"Unexpected object VS");
        need(profileHash(fragment.data(),uint32_t(fragment.size()*4))==(opaque?0x1cd7637afafff064ULL:0xf78073101fe9a35dULL),"Unexpected object FS");
        std::array<VkDescriptorSetLayoutBinding,8> bindings{};
        for(unsigned n=0;n<8;++n)bindings[n]={n==7?31u:n,n==6?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VkShaderStageFlags(n<4||n==7?VK_SHADER_STAGE_VERTEX_BIT:VK_SHADER_STAGE_FRAGMENT_BIT),nullptr};
        VkDescriptorSetLayoutCreateInfo sl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};sl.bindingCount=uint32_t(bindings.size());sl.pBindings=bindings.data();G::Check(g.vkCreateDescriptorSetLayout(g.device,&sl,nullptr,&setLayout));
        VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,7},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1}};VkDescriptorPoolCreateInfo pc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pc.maxSets=1;pc.poolSizeCount=2;pc.pPoolSizes=sizes;G::Check(g.vkCreateDescriptorPool(g.device,&pc,nullptr,&pool));
        VkDescriptorSetAllocateInfo sa{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};sa.descriptorPool=pool;sa.descriptorSetCount=1;sa.pSetLayouts=&setLayout;G::Check(g.vkAllocateDescriptorSets(g.device,&sa,&set));
        hi[4]=hi[9]=hi[14]=hi[19]=1;high=Uniform(0,hi.data(),sizeof(hi));
        const float low[]{1,1,1,1,0,0,0,0,1,1,0,0};Uniform(1,low,sizeof(low));
        std::array<float,3072> bones{};bones[0]=bones[5]=bones[10]=1;Uniform(2,bones.data(),sizeof(bones));Uniform(3,bones.data(),sizeof(bones));
        const float fragmentHigh[]{opaque?1.f:0.f,opaque?1.f:0.f,0,0,1,0,0,0,1,1,0,0};Uniform(4,fragmentHigh,sizeof(fragmentHigh));
        const float fragmentLow[]{1,1,1,1};Uniform(5,fragmentLow,sizeof(fragmentLow));projection=Uniform(31,&eyes,sizeof(eyes));
        std::array<std::array<float,18>,3> points{};const float xy[3][2]{{-100,-100},{100,-100},{0,100}};
        for(unsigned n=0;n<3;++n){points[n][0]=xy[n][0];points[n][1]=xy[n][1];points[n][2]=.5f;points[n][3]=1;}
        vertices=g.MakeBuffer(sizeof(points),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);g.Write(vertices,points.data());
        const auto pixels=g.extent.width*g.extent.height*2;read=g.MakeBuffer(pixels*16,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        output=g.MakeTexture(VK_FORMAT_R32G32B32A32_SFLOAT,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        noise=g.MakeTexture(VK_FORMAT_R32G32B32A32_SFLOAT,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);noiseUpload=g.MakeBuffer(pixels*16,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);std::vector<float> zeros(pixels*4);g.Write(noiseUpload,zeros.data());g.Begin();g.Upload(noise,noiseUpload,VK_IMAGE_LAYOUT_UNDEFINED);g.Finish();
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};G::Check(g.vkCreateSampler(g.device,&si,nullptr,&sampler));VkDescriptorImageInfo ii{sampler,noise.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkWriteDescriptorSet wi{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};wi.dstSet=set;wi.dstBinding=6;wi.descriptorCount=1;wi.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;wi.pImageInfo=&ii;g.vkUpdateDescriptorSets(g.device,1,&wi,0,nullptr);
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pl.setLayoutCount=1;pl.pSetLayouts=&setLayout;G::Check(g.vkCreatePipelineLayout(g.device,&pl,nullptr,&layout));
        VkAttachmentDescription a{};a.format=output.format;a.samples=VK_SAMPLE_COUNT_1_BIT;a.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;a.storeOp=VK_ATTACHMENT_STORE_OP_STORE;a.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;a.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        VkAttachmentReference ref{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=&ref;
        uint32_t mask=3;VkRenderPassMultiviewCreateInfo mv{VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO};mv.subpassCount=1;mv.pViewMasks=&mask;mv.correlationMaskCount=1;mv.pCorrelationMasks=&mask;
        VkSubpassDependency dep{0,VK_SUBPASS_EXTERNAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT,0};
        VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};rp.pNext=&mv;rp.attachmentCount=1;rp.pAttachments=&a;rp.subpassCount=1;rp.pSubpasses=&sub;rp.dependencyCount=1;rp.pDependencies=&dep;G::Check(g.vkCreateRenderPass(g.device,&rp,nullptr,&pass));
        VkFramebufferCreateInfo fc{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fc.renderPass=pass;fc.attachmentCount=1;fc.pAttachments=&output.view;fc.width=g.extent.width;fc.height=g.extent.height;fc.layers=1;G::Check(g.vkCreateFramebuffer(g.device,&fc,nullptr,&fb));
        ShaderCompileOptions options;options.vertexProjection=true;
        // A changed generator word retains identical SPIR-V behavior but must
        // not receive the fingerprint-qualified object-motion correction.
        auto unknown=vertex;unknown[2]^=1;
        const auto fsModule=Module(compileStereoShader(fragment).words);
        for(const auto& code:{vertex,unknown}){
            const auto compiled=compileStereoShader(code,options);
            VkPipelineShaderStageCreateInfo stages[2]{};for(auto& s:stages){s.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;s.pName="main";}stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=Module(compiled.words);stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=fsModule;
            VkVertexInputBindingDescription vb{0,sizeof(points[0]),VK_VERTEX_INPUT_RATE_VERTEX};VkVertexInputAttributeDescription attrs[]{{0,0,VK_FORMAT_R32G32B32A32_SFLOAT,0},{1,0,VK_FORMAT_R32G32_SFLOAT,16},{9,0,VK_FORMAT_R32G32B32A32_SFLOAT,24},{2,0,VK_FORMAT_R32G32B32A32_SFLOAT,40},{3,0,VK_FORMAT_R32G32B32A32_SFLOAT,56}};
            VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};vi.vertexBindingDescriptionCount=1;vi.pVertexBindingDescriptions=&vb;vi.vertexAttributeDescriptionCount=5;vi.pVertexAttributeDescriptions=attrs;
            VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkViewport vp{0,0,float(g.extent.width),float(g.extent.height),0,1};VkRect2D sc{{0,0},g.extent};VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};viewport.viewportCount=viewport.scissorCount=1;viewport.pViewports=&vp;viewport.pScissors=&sc;
            VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};rs.lineWidth=1;VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
            VkPipelineColorBlendAttachmentState ba{};ba.colorWriteMask=15;VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blend.attachmentCount=1;blend.pAttachments=&ba;
            VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};ci.stageCount=2;ci.pStages=stages;ci.pVertexInputState=&vi;ci.pInputAssemblyState=&ia;ci.pViewportState=&viewport;ci.pRasterizationState=&rs;ci.pMultisampleState=&ms;ci.pColorBlendState=&blend;ci.layout=layout;ci.renderPass=pass;VkPipeline p{};G::Check(g.vkCreateGraphicsPipelines(g.device,VK_NULL_HANDLE,1,&ci,nullptr,&p));pipelines.push_back(p);
        }
    }
    std::vector<float> Draw(unsigned variant){
        g.Begin();VkClearValue clear{};clear.color.float32[0]=clear.color.float32[1]=99;VkRenderPassBeginInfo b{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};b.renderPass=pass;b.framebuffer=fb;b.renderArea.extent=g.extent;b.clearValueCount=1;b.pClearValues=&clear;g.vkCmdBeginRenderPass(g.command,&b,VK_SUBPASS_CONTENTS_INLINE);
        g.vkCmdBindPipeline(g.command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelines[variant]);g.vkCmdBindDescriptorSets(g.command,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&set,0,nullptr);VkDeviceSize offset=0;reinterpret_cast<PFN_vkCmdBindVertexBuffers>(g.resolve(g.device,"vkCmdBindVertexBuffers"))(g.command,0,1,&vertices.handle,&offset);g.vkCmdDraw(g.command,3,1,0,0);g.vkCmdEndRenderPass(g.command);g.Download(output,read);g.Finish();return g.Read(read);
    }
    void Run(unsigned scenario){
        // Current raster is intentionally different from the engine's no-jitter
        // varying. Reusing gl_Position would therefore fail this oracle.
        const std::array<float,4> now{.4f+scenario*.1f,.2f,.5f,2.f+scenario*.3f},old{.1f,-.3f+scenario*.1f,.4f,1.5f};
        for(unsigned r=0;r<4;++r){hi[20+r*4+3]=now[r];hi[36+r*4+3]=old[r];}
        hi[0]=float(scenario%2); // Also execute the engine's skinning branch.
        eyes=FrameUniforms{};
        for(unsigned e=0;e<2;++e){auto& c=eyes.clip[e];auto& p=eyes.previousClip[e];c[0]=e?.8f:1.2f;c[5]=e?1.1f:.9f;c[12]=e?.17f:-.11f;c[13]=e?-.13f:.07f;
            p[0]=e?.95f:1.05f;p[5]=e?.85f:1.15f;p[12]=e?-.03f:.04f;p[13]=e?.02f:-.05f;
            eyes.translation[e]={e?.2f:-.2f,e?-.04f:.06f,0,0};eyes.previousTranslation[e]={e?.15f:-.15f,e?.03f:-.02f,0,0};}
        if(scenario==2){eyes.previousClip=eyes.clip;eyes.previousTranslation=eyes.translation;}
        g.Write(high,hi.data());g.Write(projection,&eyes);auto actual=Draw(0),control=Draw(1);const unsigned pixels=g.extent.width*g.extent.height;unsigned distinguished=0;
        for(unsigned e=0;e<2;++e)for(unsigned p=0;p<pixels;++p)for(unsigned k=0;k<2;++k){
            const float sign=k==1?-1.f:1.f;
            const auto& c=eyes.clip[e];const auto& prev=eyes.previousClip[e];
            const float expected=.5f*(sign*c[k*5]*now[k]/now[3]+c[12+k]+eyes.translation[e][k]/now[3]-sign*prev[k*5]*old[k]/old[3]-prev[12+k]-eyes.previousTranslation[e][k]/old[3]);
            const float center=.5f*sign*(now[k]/now[3]-old[k]/old[3]);const auto n=(e*pixels+p)*4+k;
            need(std::isfinite(actual[n])&&std::abs(actual[n]-expected)<.00001f,"Object motion is not eye-local current-minus-previous");
            need(std::abs(control[n]-center)<.00001f,"Unknown fingerprint was changed or center control failed");distinguished+=std::abs(control[n]-expected)>.001f;
        }
        need(distinguished>pixels,"Center-eye negative control cannot distinguish correction");std::cout<<"PASS object motion scenario="<<scenario<<" pixels="<<pixels*2<<" asymmetric eyes, previous transform, depth, Y orientation, no-jitter source, center control\n";
    }
    ~Test(){g.vkQueueWaitIdle(g.queue);for(auto p:pipelines)g.vkDestroyPipeline(g.device,p,nullptr);for(auto m:modules)g.vkDestroyShaderModule(g.device,m,nullptr);g.vkDestroyFramebuffer(g.device,fb,nullptr);g.vkDestroyRenderPass(g.device,pass,nullptr);g.vkDestroyPipelineLayout(g.device,layout,nullptr);g.vkDestroyDescriptorPool(g.device,pool,nullptr);g.vkDestroyDescriptorSetLayout(g.device,setLayout,nullptr);g.vkDestroySampler(g.device,sampler,nullptr);}
};
int main(int argc,char** argv)try{need(argc==3,"Expected captured original object vertex/fragment shaders");Test test(argv[1],argv[2]);for(unsigned n=0;n<3;++n)test.Run(n);return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
