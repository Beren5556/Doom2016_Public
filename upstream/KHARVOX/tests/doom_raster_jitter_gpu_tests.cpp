#include "VulkanTestDevice.h"
#include "../src/sfs/DoomRasterJitter.h"
#include <fstream>
#include <algorithm>
#include <limits>

using G=VulkanTestDevice;
using namespace kharvox::sfs;
static void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static std::vector<uint32_t> spirv(const char* path){
    std::ifstream f(path,std::ios::binary|std::ios::ate);need(bool(f),"Missing replay shader");
    const auto size=f.tellg();need(size>=20&&size%4==0,"Invalid shader size");std::vector<uint32_t> r(size_t(size)/4);f.seekg(0);
    need(bool(f.read(reinterpret_cast<char*>(r.data()),size)),"Shader read failed");return r;
}
struct ReplayCase {
    std::array<float,16> pj{},p0{};FrameUniforms eyes;std::array<float,6> viewport{};
};
static std::vector<ReplayCase> cases(const char* path){
    std::ifstream f(path,std::ios::binary);char magic[4]{};uint32_t count{};f.read(magic,4);f.read(reinterpret_cast<char*>(&count),4);
    need(f&&std::memcmp(magic,"DJT1",4)==0&&count&&count<=64,"Invalid replay header");
    std::vector<ReplayCase> result(count);
    for(auto& c:result){std::array<float,78> data{};need(bool(f.read(reinterpret_cast<char*>(data.data()),sizeof(data))),"Truncated case");
        std::copy_n(data.begin(),16,c.pj.begin());std::copy_n(data.begin()+16,16,c.p0.begin());
        for(unsigned e=0;e<2;++e){std::copy_n(data.begin()+32+20*e,16,c.eyes.clip[e].begin());std::copy_n(data.begin()+48+20*e,4,c.eyes.translation[e].begin());}
        std::copy_n(data.begin()+72,6,c.viewport.begin());}
    need(f.peek()==std::char_traits<char>::eof(),"Trailing replay bytes");return result;
}
struct Replay {
    G g;G::Texture output;G::Buffer high,low,projection,vertices,read;
    VkDescriptorSetLayout setLayout{};VkDescriptorPool pool{};VkDescriptorSet set{};
    VkPipelineLayout layout{};VkRenderPass pass{};VkFramebuffer framebuffer{};VkPipeline pipeline{};
    VkShaderModule vertex{},fragment{};
    std::array<std::array<float,15>,8> points{};
    PFN_vkCmdBindVertexBuffers bindVertices{};
    unsigned compared{},wrongSign{},wrongScale{},outOfHalfPixel{};double maxError{};
    VkShaderModule Module(const char* path){const auto words=spirv(path);VkShaderModuleCreateInfo c{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};c.codeSize=words.size()*4;c.pCode=words.data();VkShaderModule m{};G::Check(g.vkCreateShaderModule(g.device,&c,nullptr,&m));return m;}
    Replay(const char* vs,const char* fs){
        bindVertices=reinterpret_cast<PFN_vkCmdBindVertexBuffers>(g.resolve(g.device,"vkCmdBindVertexBuffers"));need(bindVertices,"Vertex binding unavailable");
        high=g.MakeBuffer(128,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);low=g.MakeBuffer(64,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        projection=g.MakeBuffer(sizeof(FrameUniforms),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);vertices=g.MakeBuffer(sizeof(points),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        read=g.MakeBuffer(8*8*2*16,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        const float xyz[8][3]{{-1,.2f,-2},{2,-1,-8},{-10,3,-100},{15,-7,-400},{.1f,.3f,-3},{40,20,-200},{-4,8,-50},{2,-4,-20}};
        for(unsigned n=0;n<8;++n){std::copy_n(xyz[n],3,points[n].begin());points[n][3]=1;}
        g.Write(vertices,points.data());std::array<float,16> lo{};for(unsigned n=4;n<8;++n)lo[n]=1;g.Write(low,lo.data());
        output=g.MakeTexture(VK_FORMAT_R32G32B32A32_SFLOAT,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        VkDescriptorSetLayoutBinding bindings[]{{0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT,nullptr},{1,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT,nullptr},{31,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT,nullptr}};
        VkDescriptorSetLayoutCreateInfo sc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};sc.bindingCount=3;sc.pBindings=bindings;G::Check(g.vkCreateDescriptorSetLayout(g.device,&sc,nullptr,&setLayout));
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,3};VkDescriptorPoolCreateInfo pc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pc.maxSets=1;pc.poolSizeCount=1;pc.pPoolSizes=&size;G::Check(g.vkCreateDescriptorPool(g.device,&pc,nullptr,&pool));
        VkDescriptorSetAllocateInfo sa{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};sa.descriptorPool=pool;sa.descriptorSetCount=1;sa.pSetLayouts=&setLayout;G::Check(g.vkAllocateDescriptorSets(g.device,&sa,&set));
        const G::Buffer buffers[]{high,low,projection};for(unsigned n=0;n<3;++n){VkDescriptorBufferInfo bi{buffers[n].handle,0,buffers[n].size};VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=set;w.dstBinding=bindings[n].binding;w.descriptorCount=1;w.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;w.pBufferInfo=&bi;g.vkUpdateDescriptorSets(g.device,1,&w,0,nullptr);}
        VkPipelineLayoutCreateInfo lc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};lc.setLayoutCount=1;lc.pSetLayouts=&setLayout;G::Check(g.vkCreatePipelineLayout(g.device,&lc,nullptr,&layout));
        VkAttachmentDescription a{};a.format=output.format;a.samples=VK_SAMPLE_COUNT_1_BIT;a.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;a.storeOp=VK_ATTACHMENT_STORE_OP_STORE;a.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;a.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        VkAttachmentReference ref{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=&ref;
        uint32_t mask=3;VkRenderPassMultiviewCreateInfo mv{VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO};mv.subpassCount=1;mv.pViewMasks=&mask;mv.correlationMaskCount=1;mv.pCorrelationMasks=&mask;
        VkSubpassDependency deps[2]{};deps[0].srcSubpass=VK_SUBPASS_EXTERNAL;deps[0].dstSubpass=0;deps[0].srcStageMask=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;deps[0].dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;deps[0].srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;deps[0].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].srcSubpass=0;deps[1].dstSubpass=VK_SUBPASS_EXTERNAL;deps[1].srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;deps[1].dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT;deps[1].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;deps[1].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        VkRenderPassCreateInfo rc{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};rc.pNext=&mv;rc.attachmentCount=1;rc.pAttachments=&a;rc.subpassCount=1;rc.pSubpasses=&sub;rc.dependencyCount=2;rc.pDependencies=deps;G::Check(g.vkCreateRenderPass(g.device,&rc,nullptr,&pass));
        VkFramebufferCreateInfo fc{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fc.renderPass=pass;fc.attachmentCount=1;fc.pAttachments=&output.view;fc.width=fc.height=8;fc.layers=1;G::Check(g.vkCreateFramebuffer(g.device,&fc,nullptr,&framebuffer));
        vertex=Module(vs);fragment=Module(fs);VkPipelineShaderStageCreateInfo stages[2]{};for(auto& s:stages){s.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;s.pName="main";}stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=vertex;stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=fragment;
        VkVertexInputBindingDescription vb{0,sizeof(points[0]),VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription attributes[]{{0,0,VK_FORMAT_R32G32B32A32_SFLOAT,0},{1,0,VK_FORMAT_R32G32_SFLOAT,16},{2,0,VK_FORMAT_R32G32B32_SFLOAT,24},{12,0,VK_FORMAT_R32G32_SFLOAT,36},{13,0,VK_FORMAT_R32G32B32A32_SFLOAT,44}};
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};vi.vertexBindingDescriptionCount=1;vi.pVertexBindingDescriptions=&vb;vi.vertexAttributeDescriptionCount=5;vi.pVertexAttributeDescriptions=attributes;
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};ia.topology=VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        VkViewport viewport{0,0,8,8,0,1};VkRect2D scissor{{0,0},{8,8}};VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};vp.viewportCount=vp.scissorCount=1;vp.pViewports=&viewport;vp.pScissors=&scissor;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.lineWidth=1;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState cb{};cb.colorWriteMask=15;VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blend.attachmentCount=1;blend.pAttachments=&cb;
        VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};pi.stageCount=2;pi.pStages=stages;pi.pVertexInputState=&vi;pi.pInputAssemblyState=&ia;pi.pViewportState=&vp;pi.pRasterizationState=&raster;pi.pMultisampleState=&ms;pi.pColorBlendState=&blend;pi.layout=layout;pi.renderPass=pass;
        G::Check(g.vkCreateGraphicsPipelines(g.device,VK_NULL_HANDLE,1,&pi,nullptr,&pipeline));
    }
    std::vector<float> Draw(const ReplayCase& c,const std::array<float,16>& p){
        std::array<float,32> hi{};std::copy(p.begin(),p.end(),hi.begin());hi[16]=hi[21]=hi[26]=hi[28]=1;
        g.Write(high,hi.data());g.Write(projection,&c.eyes);g.Begin();VkClearValue clear{};
        VkRenderPassBeginInfo b{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};b.renderPass=pass;b.framebuffer=framebuffer;b.renderArea.extent={8,8};b.clearValueCount=1;b.pClearValues=&clear;
        g.vkCmdBeginRenderPass(g.command,&b,VK_SUBPASS_CONTENTS_INLINE);g.vkCmdBindPipeline(g.command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);g.vkCmdBindDescriptorSets(g.command,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&set,0,nullptr);
        VkDeviceSize offset=0;bindVertices(g.command,0,1,&vertices.handle,&offset);g.vkCmdDraw(g.command,8,1,0,0);g.vkCmdEndRenderPass(g.command);g.Download(output,read);g.Finish();return g.Read(read);
    }
    void Run(const ReplayCase& c,unsigned index){
        const auto result=doomRasterJitter(c.pj,c.p0,c.eyes,c.viewport);need(bool(result),"Replay projection contract rejected");
        outOfHalfPixel+=!result->withinHalfPixel;const auto j=Draw(c,c.pj),z=Draw(c,c.p0);
        for(unsigned e=0;e<2;++e)for(unsigned n=0;n<8;++n){const auto pos=(e*64+n)*4;
            need(j[pos+3]>0&&z[pos+3]>0,"Missing or invalid captured clip output");
            for(unsigned axis=0;axis<2;++axis){
                const double measured=(double(j[pos+axis])/j[pos+3]-double(z[pos+axis])/z[pos+3])*c.viewport[2+axis]*.5;
                const double expected=result->pixels[e][axis],error=std::abs(measured-expected);maxError=std::max(maxError,error);
                if(error>=.002){std::cerr<<"case="<<index<<" eye="<<e<<" point="<<n<<" axis="<<axis<<" measured="<<measured<<" expected="<<expected<<'\n';throw std::runtime_error("GPU projection differs from jitter conversion");}
                wrongSign+=std::abs(measured+expected)>.002;wrongScale+=std::abs(measured-expected/c.eyes.clip[e][axis?5:0])>.002;++compared;
            }
        }
        std::cout<<"PASS jitter case="<<index<<" left="<<result->pixels[0][0]<<','<<result->pixels[0][1]<<" right="<<result->pixels[1][0]<<','<<result->pixels[1][1]<<" withinHalfPixel="<<result->withinHalfPixel<<'\n';
    }
    ~Replay(){g.vkQueueWaitIdle(g.queue);if(pipeline)g.vkDestroyPipeline(g.device,pipeline,nullptr);if(framebuffer)g.vkDestroyFramebuffer(g.device,framebuffer,nullptr);if(pass)g.vkDestroyRenderPass(g.device,pass,nullptr);if(layout)g.vkDestroyPipelineLayout(g.device,layout,nullptr);if(pool)g.vkDestroyDescriptorPool(g.device,pool,nullptr);if(setLayout)g.vkDestroyDescriptorSetLayout(g.device,setLayout,nullptr);if(vertex)g.vkDestroyShaderModule(g.device,vertex,nullptr);if(fragment)g.vkDestroyShaderModule(g.device,fragment,nullptr);}
};
static void Rejections(const ReplayCase& c){
    auto rejects=[](const ReplayCase& v){return !doomRasterJitter(v.pj,v.p0,v.eyes,v.viewport);};
    auto bad=c;bad.pj[0]+=1;need(rejects(bad),"Changed focal scale accepted");
    bad=c;bad.pj[2]=std::numeric_limits<float>::quiet_NaN();need(rejects(bad),"NaN accepted");
    bad=c;bad.pj[15]=bad.p0[15]=1;need(rejects(bad),"Non-perspective clipW accepted");
    bad=c;bad.eyes.translation[1][3]=1;need(rejects(bad),"Depth-dependent eye jitter accepted");
    bad=c;bad.eyes.clip[0][3]=.1f;need(rejects(bad),"Projective eye transform accepted");
    bad=c;bad.viewport[2]=0;need(rejects(bad),"Empty viewport accepted");
    bad=c;bad.viewport[3]=std::numeric_limits<float>::infinity();need(rejects(bad),"Infinite viewport accepted");
    auto zero=c;zero.pj=zero.p0;auto r=doomRasterJitter(zero.pj,zero.p0,zero.eyes,zero.viewport);need(r&&r->pixels[0][0]==0&&r->pixels[1][1]==0&&r->withinHalfPixel,"Zero jitter changed");
    auto wide=c;wide.pj[2]=wide.p0[2]+.01f;r=doomRasterJitter(wide.pj,wide.p0,wide.eyes,wide.viewport);need(r&&!r->withinHalfPixel&&std::abs(r->pixels[0][0])>1,"Large real jitter silently clamped");
}
int main(int argc,char** argv){try{
    need(argc==4,"Expected instrumented vertex, fragment, cases");const auto data=cases(argv[3]);Rejections(data.front());Replay r(argv[1],argv[2]);
    for(unsigned n=0;n<data.size();++n)r.Run(data[n],n);
    const auto capturedOutside=r.outOfHalfPixel;
    auto negative=data.front();negative.viewport[1]=negative.viewport[3];negative.viewport[3]=-negative.viewport[3];r.Run(negative,unsigned(data.size()));
    auto asymmetric=data.front();asymmetric.eyes.clip[1][0]*=.8f;asymmetric.eyes.clip[1][5]*=1.1f;
    asymmetric.viewport[0]=53;asymmetric.viewport[1]=29;r.Run(asymmetric,unsigned(data.size()+1));
    auto zero=data.front();zero.pj=zero.p0;r.Run(zero,unsigned(data.size()+2));
    need(r.wrongSign>64&&r.wrongScale>64,"Negative controls did not distinguish sign/eye scaling");
    std::cout<<"PASS GPU original geometry + SFS wrapper: "<<r.compared<<" coordinate comparisons; max error="<<r.maxError<<" px; captured cases above half pixel="<<capturedOutside<<'/'<<data.size()<<"; live geometry UBO association remains unverified.\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
