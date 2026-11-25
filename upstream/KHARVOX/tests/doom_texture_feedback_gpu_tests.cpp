#include "VulkanTestDevice.h"
#include "../src/sfs/ShaderCompiler.h"
#include "../src/sfs/DoomTextureFeedback.h"
#include "experimental_doom_texture_lod.h"
#include <fstream>
#include <array>
#include <algorithm>
#include <cmath>
using G=VulkanTestDevice;
static void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static std::vector<uint32_t> readWords(const char* path){
    std::ifstream f(path,std::ios::binary|std::ios::ate);need(bool(f),"Missing shader");
    const auto size=f.tellg();need(size>=20&&size%4==0,"Invalid SPIR-V");
    std::vector<uint32_t> w(size_t(size)/4);f.seekg(0);need(bool(f.read(reinterpret_cast<char*>(w.data()),size)),"Shader read failed");return w;
}
int main(int argc,char** argv){try{
    need(argc==3,"Expected vertex and feedback fragment shaders");
    const auto original=readWords(argv[2]);kharvox::sfs::ShaderCompileOptions options;
    const auto unchanged=kharvox::sfs::compileStereoShader(original,options);
    need(unchanged.textureFeedbackCorrections==0,"Unowned renderer changed");
    options.textureFeedback=true;
    const auto corrected=kharvox::sfs::compileStereoShader(original,options);
    need(corrected.textureFeedbackCorrections==1,"Actual fragment feedback not recognized");
    auto twice=corrected.glsl;need(kharvox::sfs::correctDoomTextureFeedback(twice)==0&&twice==corrected.glsl,"Feedback correction compounded");
    G g;const auto high=g.MakeBuffer(16,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT),low=g.MakeBuffer(128,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    const auto read=g.MakeBuffer(8*8*2*16,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    const auto output=g.MakeTexture(VK_FORMAT_R32G32B32A32_SFLOAT,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    VkDescriptorSetLayoutBinding bindings[]{{5,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},{6,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr}};
    VkDescriptorSetLayoutCreateInfo sc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};sc.bindingCount=2;sc.pBindings=bindings;
    VkDescriptorSetLayout setLayout{};G::Check(g.vkCreateDescriptorSetLayout(g.device,&sc,nullptr,&setLayout));
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,2};
    VkDescriptorPoolCreateInfo pc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pc.maxSets=1;pc.poolSizeCount=1;pc.pPoolSizes=&poolSize;
    VkDescriptorPool pool{};G::Check(g.vkCreateDescriptorPool(g.device,&pc,nullptr,&pool));
    VkDescriptorSetAllocateInfo sa{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};sa.descriptorPool=pool;sa.descriptorSetCount=1;sa.pSetLayouts=&setLayout;
    VkDescriptorSet set{};G::Check(g.vkAllocateDescriptorSets(g.device,&sa,&set));
    const G::Buffer buffers[]{high,low};
    for(unsigned i=0;i<2;++i){VkDescriptorBufferInfo bi{buffers[i].handle,0,buffers[i].size};VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=set;w.dstBinding=5+i;w.descriptorCount=1;w.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;w.pBufferInfo=&bi;g.vkUpdateDescriptorSets(g.device,1,&w,0,nullptr);}
    VkPipelineLayoutCreateInfo lc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};lc.setLayoutCount=1;lc.pSetLayouts=&setLayout;
    VkPipelineLayout layout{};G::Check(g.vkCreatePipelineLayout(g.device,&lc,nullptr,&layout));
    VkAttachmentDescription a{};a.format=output.format;a.samples=VK_SAMPLE_COUNT_1_BIT;a.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;a.storeOp=VK_ATTACHMENT_STORE_OP_STORE;a.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;a.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference ref{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=&ref;
    uint32_t mask=3;VkRenderPassMultiviewCreateInfo mv{VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO};mv.subpassCount=1;mv.pViewMasks=&mask;mv.correlationMaskCount=1;mv.pCorrelationMasks=&mask;
    VkSubpassDependency dep{};dep.srcSubpass=0;dep.dstSubpass=VK_SUBPASS_EXTERNAL;dep.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;dep.dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT;dep.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;dep.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    VkRenderPassCreateInfo rc{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};rc.pNext=&mv;rc.attachmentCount=1;rc.pAttachments=&a;rc.subpassCount=1;rc.pSubpasses=&sub;rc.dependencyCount=1;rc.pDependencies=&dep;
    VkRenderPass pass{};G::Check(g.vkCreateRenderPass(g.device,&rc,nullptr,&pass));
    VkFramebufferCreateInfo fc{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fc.renderPass=pass;fc.attachmentCount=1;fc.pAttachments=&output.view;fc.width=fc.height=8;fc.layers=1;
    VkFramebuffer frame{};G::Check(g.vkCreateFramebuffer(g.device,&fc,nullptr,&frame));
    const auto vertex=kharvox::sfs::compileStereoShader(readWords(argv[1]));
    VkShaderModule modules[2]{};const std::vector<uint32_t>* words[]{&vertex.words,&corrected.words};
    VkPipelineShaderStageCreateInfo stages[2]{};
    for(unsigned i=0;i<2;++i){VkShaderModuleCreateInfo mc{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};mc.codeSize=words[i]->size()*4;mc.pCode=words[i]->data();G::Check(g.vkCreateShaderModule(g.device,&mc,nullptr,&modules[i]));stages[i].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stages[i].stage=i?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT;stages[i].module=modules[i];stages[i].pName="main";}
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport viewport{0,0,8,8,0,1};VkRect2D scissor{{0,0},{8,8}};VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};vp.viewportCount=vp.scissorCount=1;vp.pViewports=&viewport;vp.pScissors=&scissor;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.lineWidth=1;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState ba{};ba.colorWriteMask=15;VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blend.attachmentCount=1;blend.pAttachments=&ba;
    VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};pi.stageCount=2;pi.pStages=stages;pi.pVertexInputState=&vi;pi.pInputAssemblyState=&ia;pi.pViewportState=&vp;pi.pRasterizationState=&raster;pi.pMultisampleState=&ms;pi.pColorBlendState=&blend;pi.layout=layout;pi.renderPass=pass;
    VkPipeline pipeline{};G::Check(g.vkCreateGraphicsPipelines(g.device,VK_NULL_HANDLE,1,&pi,nullptr,&pipeline));
    // Stay away from integer mip boundaries: CVar decimal serialization and
    // float32 derivatives legitimately round either side of an exact boundary.
    const float nativeLods[]{-4,-2,.125f,1.125f,2.625f,5.625f,11.125f,20};
    unsigned checked=0,coarserBefore=0;
    for(const char* scale:{"1","0.6667","0.5","0.3333","0.6667","1"})for(float pressure:{0.f,1.5f})for(const char* baseline:{"0","0.25"}){
        const float ratio=std::stof(scale),delta=std::log2(ratio),base=std::stof(baseline);
        const auto bias=kharvox::native::doomTextureLod(baseline,scale);need(bool(bias),"Invalid texture bias");
        const std::array<float,4> h{0,std::stof(*bias)+pressure,0,12};
        std::array<std::array<float,4>,8> l{};for(unsigned i=0;i<8;++i)l[i][0]=nativeLods[i]-delta;
        g.Write(high,h.data());g.Write(low,l.data());g.Begin();VkClearValue clear{};clear.color.float32[0]=-999;
        VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=pass;begin.framebuffer=frame;begin.renderArea.extent={8,8};begin.clearValueCount=1;begin.pClearValues=&clear;
        g.vkCmdBeginRenderPass(g.command,&begin,VK_SUBPASS_CONTENTS_INLINE);g.vkCmdBindPipeline(g.command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);g.vkCmdBindDescriptorSets(g.command,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&set,0,nullptr);g.vkCmdDraw(g.command,3,1,0,0);g.vkCmdEndRenderPass(g.command);g.Download(output,read);g.Finish();
        const auto pixels=g.Read(read);
        for(unsigned eye=0;eye<2;++eye)for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){
            const auto i=((eye*8+y)*8+x)*4;
            const auto expected=unsigned(std::clamp(std::max(0.f,l[x][0])+h[1],0.f,12.f));
            need(pixels[i]==float(expected)&&pixels[i+1]==3&&pixels[i+2]==17&&pixels[i+3]==29,"Feedback LOD corrupted page/source bits or selected the wrong mip");
            // For non-magnified textures SR should request the same native
            // mip. This fails with the previous unchanged residency bias.
            if(nativeLods[x]>=0){const auto native=unsigned(std::clamp(nativeLods[x]+base+pressure,0.f,12.f));need(expected==native,"Residency changed with render scale");
                coarserBefore+=unsigned(std::clamp(l[x][0]+base+pressure,0.f,12.f))>native;}
            ++checked;
        }
    }
    need(coarserBefore>0,"Fixture did not expose the previous residency mismatch");
    g.vkDestroyPipeline(g.device,pipeline,nullptr);for(auto m:modules)g.vkDestroyShaderModule(g.device,m,nullptr);
    g.vkDestroyFramebuffer(g.device,frame,nullptr);g.vkDestroyRenderPass(g.device,pass,nullptr);g.vkDestroyPipelineLayout(g.device,layout,nullptr);g.vkDestroyDescriptorPool(g.device,pool,nullptr);g.vkDestroyDescriptorSetLayout(g.device,setLayout,nullptr);
    std::cout<<"PASS texture feedback pixels="<<checked<<" previous-coarser="<<coarserBefore<<"; both eyes, hot scales, near clamp, page/source bits and cache pressure preserved\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
