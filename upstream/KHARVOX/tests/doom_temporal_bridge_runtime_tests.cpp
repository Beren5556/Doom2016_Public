#include "VulkanTestDevice.h"
#include "TemporalRuntimeFunctions.h"
#include "../src/sfs/NativeSfs.h"
#include "../src/sfs/DoomTemporalUniforms.h"
#include "../src/native/NativeStereo.h"
#include <fstream>
#include <filesystem>
#include <array>
static void need(bool v,const char* msg){if(!v)throw std::runtime_error(msg);}
static std::vector<uint32_t> words(const char* path){std::ifstream f(path,std::ios::binary|std::ios::ate);need(bool(f),"Missing shader");auto bytes=f.tellg();std::vector<uint32_t> out(size_t(bytes)/4);f.seekg(0);need(bool(f.read(reinterpret_cast<char*>(out.data()),bytes)),"Read shader");return out;}
#ifdef KHARVOX_HAVE_DLSS_STARTUP
#include "DoomRuntimeDlssFixture.h"
namespace kharvox::sfs { bool dlssTestOutput(VkDevice,kharvox::dlss::Output&,uint64_t* =nullptr); }
namespace kharvox::sfs { bool dlssTestControlsDraining(VkDevice); }
namespace kharvox::sfs { bool dlssTestGuidesFor(VkDevice,VkCommandBuffer,uint64_t); }
namespace kharvox::sfs { uint64_t dlssTestResetEvaluations(VkDevice); }
namespace kharvox::sfs { bool dlssTestInactiveReleased(VkDevice); }
#endif
#ifdef KHARVOX_HAVE_RECONSTRUCTION
#include "DoomFinalRuntimeFixture.h"
#endif
int main(int argc,char** argv)try{
    #ifdef KHARVOX_HAVE_DLSS_STARTUP
#ifdef KHARVOX_HAVE_RECONSTRUCTION
    need(argc==11,"Expected shaders/runtime/cache/replay/final/neural/mode/foveation");DoomRuntimeDlssFixture::runtime=std::filesystem::absolute(argv[4]);DoomRuntimeDlssFixture::cache=std::filesystem::absolute(argv[5]);DoomRuntimeDlssFixture captured(argv[6]);
    const auto own=kharvox::overlay::root();std::filesystem::create_directories(own/L"ngx");
    std::ofstream(own/L"enabled")<<"native-fixture";
    bool reconstruct=std::stoi(argv[9])>=3;const auto fovMode=std::stoi(argv[10]);
    char scaleText[16]{};GetEnvironmentVariableA("KHARVOX_TEST_SCALE_BPS",scaleText,sizeof(scaleText));
    const unsigned scaleBps=*scaleText?unsigned(std::stoul(scaleText)):7500;const float sceneScale=scaleBps/10000.f;
    char presetText[16]{},appearanceText[128]{};GetEnvironmentVariableA("KHARVOX_TEST_PRESET",presetText,sizeof(presetText));GetEnvironmentVariableA("KHARVOX_TEST_APPEARANCE",appearanceText,sizeof(appearanceText));
    std::ofstream(own/L"startup.cfg")<<"3 "<<argv[9]<<" "<<(*presetText?presetText:"11")<<" 1 0 "<<scaleBps<<" 0 85 0 3;"<<(fovMode?1:0)<<";1;2000;2000;2000;2000;1000;0;7500;11;11;10000;0;1;1;2000;2000;2000;2000;1000;0 "<<(*appearanceText?appearanceText:"2 120 127 134 1 100 0 100 100 0 0")<<" "<<fovMode<<" 11100 1000 1000 3000 3000 3000 3000 1000 1000 1000 1000 1\n";
    if(!std::filesystem::exists(own/L"ngx/nvngx_dlssnr.dll"))std::filesystem::copy_file(argv[8],own/L"ngx/nvngx_dlssnr.dll");
#else
    need(argc==7||argc==9,"Expected shaders/runtime/cache/replay [width height]");DoomRuntimeDlssFixture::runtime=std::filesystem::absolute(argv[4]);DoomRuntimeDlssFixture::cache=std::filesystem::absolute(argv[5]);DoomRuntimeDlssFixture captured(argv[6]);
#endif
    SetEnvironmentVariableA("KHARVOX_DLSS_DLAA","1");SetEnvironmentVariableA("KHARVOX_DLSS_RAW_HDR","1");
#else
    need(argc==4,"Expected vertex/profile/original TAA");
#endif
    SetEnvironmentVariableA("KHARVOX_SFS_NATIVE_PROBE","1");SetEnvironmentVariableA("KHARVOX_SFS_NATIVE_VR","1");SetEnvironmentVariableA("KHARVOX_TEMPORAL_BRIDGE","1");SetEnvironmentVariableA("KHARVOX_TEMPORAL_AUDIT_PATH",nullptr);
    SetEnvironmentVariableA("KHARVOX_TEMPORAL_PARAMETERS","1");
    SetEnvironmentVariableW(L"KHARVOX_SFS_PROFILE",std::filesystem::path(argv[2]).parent_path().c_str());
    using G=VulkanTestDevice;
#ifdef KHARVOX_HAVE_DLSS_STARTUP
    VkExtent2D testedExtent=argc==9?VkExtent2D{uint32_t(std::stoul(argv[7])),uint32_t(std::stoul(argv[8]))}:VkExtent2D{128,160};
#ifdef KHARVOX_HAVE_RECONSTRUCTION
    char widthText[16]{},heightText[16]{};GetEnvironmentVariableA("KHARVOX_TEST_WIDTH",widthText,sizeof(widthText));GetEnvironmentVariableA("KHARVOX_TEST_HEIGHT",heightText,sizeof(heightText));
    if(*widthText&&*heightText)testedExtent={uint32_t(std::stoul(widthText)),uint32_t(std::stoul(heightText))};
#endif
    need(testedExtent.width>=32&&testedExtent.height>=32&&testedExtent.width<=8192&&testedExtent.height<=8192&&uint64_t(testedExtent.width)*testedExtent.height<=32*1024*1024,"Unsupported test extent");
    G g(false,testedExtent,&DoomRuntimeDlssFixture::query,false,true);std::cout<<"Native DLAA test extent="<<testedExtent.width<<"x"<<testedExtent.height<<" per eye; negotiated storageImageExtendedFormats="<<g.storageImageExtendedFormatsEnabled<<'\n';
#else
    G g(false,{32,40});
#endif
    need(kharvox::sfs::initialize(g.device,g.physical,g.resolve,g.memory),"SFS initialization");
#ifdef KHARVOX_HAVE_DLSS_STARTUP
    kharvox::dlss::Options options;options.device=g.device;options.instance=g.instance;options.physical=g.physical;options.gipa=g.gipa;options.gdpa=g.resolve;options.queue=g.queue;options.queueFamily=g.family;options.externallySynchronized=true;
    options.projectId=DoomRuntimeDlssFixture::project;options.runtimeDirectory=DoomRuntimeDlssFixture::runtime;options.cacheDirectory=DoomRuntimeDlssFixture::cache;
    options.storageImageExtendedFormatsEnabled=g.storageImageExtendedFormatsEnabled;
    need(kharvox::sfs::configureDlss(g.device,options),"Native DLSS session not configured");
#endif
#define WRAP(n) g.n=reinterpret_cast<PFN_##n>(kharvox::sfs::wrapProc(g.device,#n,reinterpret_cast<PFN_vkVoidFunction>(g.n)));
    RUNTIME_FUNCTIONS(WRAP) WRAP(vkCreateCommandPool)
    WRAP(vkCreateBuffer) WRAP(vkDestroyBuffer) WRAP(vkAllocateMemory) WRAP(vkFreeMemory) WRAP(vkBindBufferMemory) WRAP(vkMapMemory) WRAP(vkUnmapMemory)
#undef WRAP
    auto setViewport=reinterpret_cast<PFN_vkCmdSetViewport>(kharvox::sfs::wrapProc(g.device,"vkCmdSetViewport",g.resolve(g.device,"vkCmdSetViewport")));
    auto setScissor=reinterpret_cast<PFN_vkCmdSetScissor>(kharvox::sfs::wrapProc(g.device,"vkCmdSetScissor",g.resolve(g.device,"vkCmdSetScissor")));
    reinterpret_cast<PFN_vkFreeCommandBuffers>(g.resolve(g.device,"vkFreeCommandBuffers"))(g.device,g.pool,1,&g.command);
    g.vkDestroyCommandPool(g.device,g.pool,nullptr);VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};cp.queueFamilyIndex=g.family;G::Check(g.vkCreateCommandPool(g.device,&cp,nullptr,&g.pool));
    VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=g.pool;ca.commandBufferCount=1;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;G::Check(g.vkAllocateCommandBuffers(g.device,&ca,&g.command));
    constexpr auto inputUsage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    auto depth=g.MakeTexture(VK_FORMAT_R32_SFLOAT,inputUsage,1),motion=g.MakeTexture(VK_FORMAT_R32G32_SFLOAT,inputUsage,1),color=g.MakeTexture(VK_FORMAT_B10G11R11_UFLOAT_PACK32,inputUsage,1);
    auto outColor=g.MakeTexture(color.format,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,1),outPacked=g.MakeTexture(VK_FORMAT_R16G16_SFLOAT,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,1);
    const uint32_t pixels=g.extent.width*g.extent.height;
    auto colorUpload=g.MakeBuffer(pixels*2*4,VK_BUFFER_USAGE_TRANSFER_SRC_BIT),depthUpload=g.MakeBuffer(pixels*2*4,VK_BUFFER_USAGE_TRANSFER_SRC_BIT),motionUpload=g.MakeBuffer(pixels*2*8,VK_BUFFER_USAGE_TRANSFER_SRC_BIT),readback=g.MakeBuffer(pixels*2*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    std::array<float,56> hi{};std::array<float,28> lo{};
    hi[0]=float(g.extent.width);hi[1]=float(g.extent.height);hi[2]=1.f/hi[0];hi[3]=1.f/hi[1];hi[6]=1;hi[8]=hi[10]=hi[13]=hi[14]=.5f;hi[18]=hi[2];hi[19]=hi[3];for(unsigned n=24;n<40;++n)hi[n]=1.f/9;hi[40]=hi[44]=1.f/9;hi[48]=hi[49]=1;hi[52]=hi[0];hi[53]=hi[1];hi[54]=1;hi[55]=4;
    lo[3]=1;lo[4]=lo[5]=lo[9]=lo[16]=-1;for(unsigned n:{6,8,10,12,13,14,15,17,18,20,21,22})lo[n]=1;
    #ifdef KHARVOX_HAVE_DLSS_STARTUP
    hi=captured.uniforms.high;lo=captured.uniforms.low;hi[0]=float(g.extent.width);hi[1]=float(g.extent.height);
    for(unsigned k=0;k<2;++k)hi[2+k]=hi[18+k]=hi[50+k]=1.f/hi[k];
#ifdef KHARVOX_HAVE_RECONSTRUCTION
    if(reconstruct){lo[12]=lo[13]=sceneScale;lo[14]=lo[15]=1.f/sceneScale;}
#endif
#endif
    auto high=g.MakeBuffer(sizeof(hi),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT),low=g.MakeBuffer(sizeof(lo),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);g.Write(high,hi.data());g.Write(low,lo.data());
    void* mappedHigh{};void* mappedLow{};G::Check(g.vkMapMemory(g.device,high.allocation,0,high.size,0,&mappedHigh));G::Check(g.vkMapMemory(g.device,low.allocation,0,low.size,0,&mappedLow));
    VkDescriptorSetLayoutBinding bindings[7]{};for(unsigned n=0;n<7;++n)bindings[n]={n,n<2?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};li.bindingCount=7;li.pBindings=bindings;VkDescriptorSetLayout layout{};G::Check(g.vkCreateDescriptorSetLayout(g.device,&li,nullptr,&layout));
    VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,2},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,5}};VkDescriptorPoolCreateInfo pc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pc.maxSets=1;pc.poolSizeCount=2;pc.pPoolSizes=sizes;VkDescriptorPool pool{};G::Check(g.vkCreateDescriptorPool(g.device,&pc,nullptr,&pool));
    VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};da.descriptorPool=pool;da.descriptorSetCount=1;da.pSetLayouts=&layout;VkDescriptorSet set{};G::Check(g.vkAllocateDescriptorSets(g.device,&da,&set));
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};VkSampler sampler{};G::Check(g.vkCreateSampler(g.device,&si,nullptr,&sampler));
    VkDescriptorBufferInfo buffers[]{{high.handle,0,sizeof(hi)},{low.handle,0,sizeof(lo)}};VkDescriptorImageInfo images[]{{sampler,depth.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{sampler,motion.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{sampler,color.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
    VkWriteDescriptorSet writes[7]{};for(unsigned n=0;n<7;++n){auto& w=writes[n];w={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=set;w.dstBinding=n;w.descriptorCount=1;w.descriptorType=bindings[n].descriptorType;if(n<2)w.pBufferInfo=&buffers[n];else w.pImageInfo=&images[std::min(n-2,2u)];}g.vkUpdateDescriptorSets(g.device,7,writes,0,nullptr);
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pl.setLayoutCount=1;pl.pSetLayouts=&layout;VkPipelineLayout pipelineLayout{};G::Check(g.vkCreatePipelineLayout(g.device,&pl,nullptr,&pipelineLayout));
    VkAttachmentDescription attachments[2]{};for(unsigned n=0;n<2;++n){auto& a=attachments[n];a.format=n?outPacked.format:outColor.format;a.samples=VK_SAMPLE_COUNT_1_BIT;a.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;a.storeOp=VK_ATTACHMENT_STORE_OP_STORE;a.initialLayout=a.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;}
    VkAttachmentReference refs[]{{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},{1,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=2;sub.pColorAttachments=refs;
    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};rp.attachmentCount=2;rp.pAttachments=attachments;rp.subpassCount=1;rp.pSubpasses=&sub;VkRenderPass pass{};G::Check(g.vkCreateRenderPass(g.device,&rp,nullptr,&pass));
    VkImageView views[]{outColor.view,outPacked.view};VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fb.renderPass=pass;fb.attachmentCount=2;fb.pAttachments=views;fb.width=g.extent.width;fb.height=g.extent.height;fb.layers=1;VkFramebuffer framebuffer{};G::Check(g.vkCreateFramebuffer(g.device,&fb,nullptr,&framebuffer));
    VkShaderModule modules[2]{};for(unsigned n=0;n<2;++n){auto code=words(argv[n?3:1]);VkShaderModuleCreateInfo m{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};m.codeSize=code.size()*4;m.pCode=code.data();G::Check(g.vkCreateShaderModule(g.device,&m,nullptr,&modules[n]));}
    VkPipelineShaderStageCreateInfo stages[2]{};for(unsigned n=0;n<2;++n){stages[n]={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};stages[n].stage=n?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT;stages[n].module=modules[n];stages[n].pName="main";}
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vs{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};vs.viewportCount=vs.scissorCount=1;VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.lineWidth=1;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;VkPipelineColorBlendAttachmentState colors[2]{};for(auto& c:colors)c.colorWriteMask=15;VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blend.attachmentCount=2;blend.pAttachments=colors;
    VkDynamicState dyn[]{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};ds.dynamicStateCount=2;ds.pDynamicStates=dyn;
    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};ci.stageCount=2;ci.pStages=stages;ci.pVertexInputState=&vi;ci.pInputAssemblyState=&ia;ci.pViewportState=&vs;ci.pRasterizationState=&raster;ci.pMultisampleState=&ms;ci.pColorBlendState=&blend;ci.pDynamicState=&ds;ci.layout=pipelineLayout;ci.renderPass=pass;VkPipeline pipeline{};G::Check(g.vkCreateGraphicsPipelines(g.device,VK_NULL_HANDLE,1,&ci,nullptr,&pipeline));
#ifdef KHARVOX_HAVE_RECONSTRUCTION
    DoomFinalRuntimeFixture final(g,outColor,sampler,ci,argv[7],sceneScale);
#endif
    std::vector<uint32_t> packed(pixels*2);std::vector<float> depths(pixels*2,.5f),motions(pixels*4,0);
    for(unsigned e=0;e<2;++e)for(unsigned p=0;p<pixels;++p)packed[e*pixels+p]=((e?12u:15u)<<6)|(((e?15u:13u)<<6)<<11)|(((e?16u:14u)<<5)<<22);
    g.Write(colorUpload,packed.data());g.Write(depthUpload,depths.data());g.Write(motionUpload,motions.data());
#ifdef KHARVOX_HAVE_DLSS_STARTUP
    auto computeValue=g.MakeBuffer(4,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);uint32_t sentinel=0;g.Write(computeValue,&sentinel);
    auto ngxReadback=g.MakeBuffer(pixels*2*8,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    // A sharp pattern distinguishes processed output from a raw HDR copy.
    for(unsigned e=0;e<2;++e)for(unsigned p=0;p<pixels;++p){
        const unsigned x=p%g.extent.width,y=p/g.extent.width;
        if((x+2*y)%5==0)packed[e*pixels+p]=((e?11u:16u)<<6)|((12u<<6)<<11)|(((e?17u:13u)<<5)<<22);
    }
#ifdef KHARVOX_HAVE_RECONSTRUCTION
    // Encoded LDR colors, independently distinguishable between eyes.
    for(unsigned e=0;e<2;++e)for(unsigned p=0;p<pixels;++p)packed[e*pixels+p]=((e?11u:14u)<<6)|((12u<<6)<<11)|(((e?14u:11u)<<5)<<22);
#endif
    g.Write(colorUpload,packed.data());size_t visiblyProcessedPixels=0;
    VkDescriptorSetLayoutBinding computeBinding{0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};VkDescriptorSetLayoutCreateInfo cli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};cli.bindingCount=1;cli.pBindings=&computeBinding;VkDescriptorSetLayout computeSetLayout{};G::Check(g.vkCreateDescriptorSetLayout(g.device,&cli,nullptr,&computeSetLayout));
    VkDescriptorPoolSize cs{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1};VkDescriptorPoolCreateInfo cpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};cpi.maxSets=1;cpi.poolSizeCount=1;cpi.pPoolSizes=&cs;VkDescriptorPool computePool{};G::Check(g.vkCreateDescriptorPool(g.device,&cpi,nullptr,&computePool));
    VkDescriptorSetAllocateInfo cai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};cai.descriptorPool=computePool;cai.descriptorSetCount=1;cai.pSetLayouts=&computeSetLayout;VkDescriptorSet computeSet{};G::Check(g.vkAllocateDescriptorSets(g.device,&cai,&computeSet));
    VkDescriptorBufferInfo cbi{computeValue.handle,0,4};VkWriteDescriptorSet cw{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};cw.dstSet=computeSet;cw.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;cw.descriptorCount=1;cw.pBufferInfo=&cbi;g.vkUpdateDescriptorSets(g.device,1,&cw,0,nullptr);
    VkPushConstantRange cr{VK_SHADER_STAGE_COMPUTE_BIT,0,4};VkPipelineLayoutCreateInfo cpl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};cpl.setLayoutCount=1;cpl.pSetLayouts=&computeSetLayout;cpl.pushConstantRangeCount=1;cpl.pPushConstantRanges=&cr;VkPipelineLayout computeLayout{};G::Check(g.vkCreatePipelineLayout(g.device,&cpl,nullptr,&computeLayout));
    const auto cc=words((std::filesystem::path(argv[1]).parent_path()/"dlss-restore-compute.comp.spv").string().c_str());VkShaderModuleCreateInfo cmi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};cmi.codeSize=cc.size()*4;cmi.pCode=cc.data();VkShaderModule computeModule{};G::Check(g.vkCreateShaderModule(g.device,&cmi,nullptr,&computeModule));
    VkComputePipelineCreateInfo cci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};cci.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};cci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;cci.stage.module=computeModule;cci.stage.pName="main";cci.layout=computeLayout;VkPipeline computePipeline{};
    G::Check(reinterpret_cast<PFN_vkCreateComputePipelines>(g.resolve(g.device,"vkCreateComputePipelines"))(g.device,VK_NULL_HANDLE,1,&cci,nullptr,&computePipeline));
    const auto push=reinterpret_cast<PFN_vkCmdPushConstants>(kharvox::sfs::wrapProc(g.device,"vkCmdPushConstants",g.resolve(g.device,"vkCmdPushConstants")));
    const auto dispatch=reinterpret_cast<PFN_vkCmdDispatch>(g.resolve(g.device,"vkCmdDispatch"));
#endif
#ifdef KHARVOX_HAVE_DLSS_STARTUP
    // DOOM rotates recording pools. The previous pool is not reset before the
    // next frame is recorded, although its submission has already completed.
    VkCommandPool rotatingPools[2]{g.pool,{}};VkCommandBuffer rotatingCommands[2]{g.command,{}};
    G::Check(g.vkCreateCommandPool(g.device,&cp,nullptr,&rotatingPools[1]));
    ca.commandPool=rotatingPools[1];G::Check(g.vkAllocateCommandBuffers(g.device,&ca,&rotatingCommands[1]));
#ifdef KHARVOX_HAVE_RECONSTRUCTION
    const bool hot=GetEnvironmentVariableA("KHARVOX_TEST_LIVE",nullptr,0)>0;
    const bool fullCycle=GetEnvironmentVariableA("KHARVOX_TEST_FULL_CYCLE",nullptr,0)>0;
    const uint64_t neuralFrame=fullCycle?16:6,dynamicFrame=fullCycle?28:9,dlaaFrame=fullCycle?40:12;
    kharvox::overlay::applyEngineSelection=[](const auto&){return true;};
    VkCommandPool splitPool{};VkCommandBuffer splitCommand{};VkCommandPoolCreateInfo splitInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};splitInfo.queueFamilyIndex=g.family;G::Check(g.vkCreateCommandPool(g.device,&splitInfo,nullptr,&splitPool));VkCommandBufferAllocateInfo splitAlloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};splitAlloc.commandPool=splitPool;splitAlloc.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;splitAlloc.commandBufferCount=1;G::Check(g.vkAllocateCommandBuffers(g.device,&splitAlloc,&splitCommand));
    const uint64_t frames=hot?(fullCycle?42:14):6;
    uint64_t previousResets{};
#else
    const uint64_t frames=6;
#endif
    std::vector<VkCommandBuffer> deferredSubmission;
#else
    const uint64_t frames=3;
#endif
    for(uint64_t serial=1;serial<=frames;++serial){
#ifdef KHARVOX_HAVE_DLSS_STARTUP
        g.pool=rotatingPools[(serial-1)%2];g.command=rotatingCommands[(serial-1)%2];
#endif
        if(serial==2){g.vkUnmapMemory(g.device,low.allocation);mappedLow=nullptr;}
        if(serial==3){G::Check(g.vkMapMemory(g.device,low.allocation,0,low.size,0,&mappedLow));
#ifndef KHARVOX_HAVE_DLSS_STARTUP
        hi[44]=.125f;
#endif
        std::memcpy(mappedHigh,hi.data(),sizeof(hi));}
#ifdef KHARVOX_HAVE_RECONSTRUCTION
        if(hot){
            if(serial==4||serial==neuralFrame||serial==dynamicFrame||serial==dlaaFrame){
                const unsigned mode=serial==4?3:serial==neuralFrame?4:serial==dynamicFrame?3:2;
                std::ifstream original(own/L"startup.cfg");std::string line;std::getline(original,line);std::istringstream in(line);std::vector<std::string> tokens;for(std::string t;in>>t;)tokens.push_back(t);
                tokens[1]=std::to_string(mode);tokens.back()=std::to_string(serial);
                const unsigned foveation=serial==neuralFrame?1:serial==dynamicFrame?2:0;
                tokens[21]=std::to_string(foveation);tokens[9][2]=foveation?'1':'0';
                // Exercise the user's indicator toggle through the same live
                // request as foveation, not by calling the shader in isolation.
                std::istringstream encoded(tokens[9]);std::vector<std::string> config;
                for(std::string field;std::getline(encoded,field,';');)config.push_back(field);
                config[13]=foveation?"1":"0";tokens[9].clear();
                for(const auto& field:config){if(!tokens[9].empty())tokens[9]+=';';tokens[9]+=field;}
                std::ofstream request(own/(L"request-"+std::to_wstring(GetCurrentProcessId())+L".cfg"));for(const auto& t:tokens)request<<t<<' ';request.close();Sleep(110);
            }
        }
#endif
        kharvox::native::FramePose pose;pose.serial=serial;pose.head.orientation.w=1;pose.worldScale=1;const XrFovf fov{-.7f,.7f,.7f,-.7f};for(auto& e:pose.views){e.pose.orientation.w=1;e.fov=fov;}
        kharvox::sfs::prepare(g.device,pose,fov);kharvox::sfs::copyCompleted(g.device);kharvox::sfs::beginFrame(g.device);
#ifdef KHARVOX_HAVE_DLSS_STARTUP
        // Engine trace: acquire/frame boundary precedes submission of the
        // previously recorded commands. Observe it here, not at recording end.
        if(!deferredSubmission.empty()){VkSubmitInfo submission{VK_STRUCTURE_TYPE_SUBMIT_INFO};submission.commandBufferCount=uint32_t(deferredSubmission.size());submission.pCommandBuffers=deferredSubmission.data();
#ifdef KHARVOX_HAVE_RECONSTRUCTION
            const bool changing=hot&&(serial==4||serial==neuralFrame||serial==dynamicFrame||serial==dlaaFrame);
            if(changing)need(kharvox::sfs::dlssTestControlsDraining(g.device),"Pending live control did not stop provider refills");
#endif
            kharvox::sfs::dlssSubmitted(g.device,g.queue,1,&submission,VK_SUCCESS);
            const auto token=kharvox::sfs::dlssCopyToken(g.device,g.queue);
            // Finish() waited for this queue: models the existing XR copy fence.
            kharvox::sfs::dlssCopyCompleted(g.device,token);
#ifdef KHARVOX_HAVE_RECONSTRUCTION
            if(changing){
                kharvox::sfs::prepare(g.device,pose,fov);kharvox::sfs::copyCompleted(g.device);kharvox::sfs::beginFrame(g.device);
                need(!kharvox::sfs::dlssTestControlsDraining(g.device),"Controls stayed blocked after provider retirement");
                need(kharvox::sfs::dlssTestInactiveReleased(g.device),"Apply retained inactive DLAA/SR outputs");
                std::cout<<"PASS live Apply drains delayed submission and commits at next boundary frame="<<serial<<'\n';
            }
#endif
        }
#endif
#ifdef KHARVOX_HAVE_RECONSTRUCTION
        if(hot){
            if(serial>=4){const unsigned expected=serial<neuralFrame?3:serial<dynamicFrame?4:serial<dlaaFrame?3:2;need(kharvox::overlay::current().mode==expected,"Live renderer selection not committed");}
            reconstruct=kharvox::overlay::reconstruction();
            // Consecutive real camera/UBO samples, including previous matrices
            // and Halton phase, exercise temporal history across the two buffers.
            captured.select(serial-1);hi=captured.uniforms.high;lo=captured.uniforms.low;
            hi[0]=float(g.extent.width);hi[1]=float(g.extent.height);
            for(unsigned k=0;k<2;++k)hi[2+k]=hi[18+k]=hi[50+k]=1.f/hi[k];
            lo[12]=lo[13]=reconstruct?sceneScale:1.f;lo[14]=lo[15]=reconstruct?1.f/sceneScale:1.f;
            if(mappedLow)std::memcpy(mappedLow,lo.data(),sizeof(lo));
        }
#endif
        g.Begin();
#ifdef KHARVOX_HAVE_RECONSTRUCTION
        VkCommandBuffer queuedProducer{};
#endif
#ifdef KHARVOX_HAVE_DLSS_STARTUP
#ifdef KHARVOX_HAVE_RECONSTRUCTION
        // Replay the REAL DOOM contract: full TAA viewport, nominal scene
        // fractions, integer jitter denominators, float32 perspective rounding
        // and scaled previous texel size, using the engine operations verbatim.
        kharvox::sfs::DoomTemporalUniformSnapshot scaled{hi,lo};
        doomEngineJitterFixture(captured.view,scaled,g.extent,lo[12]);
        hi=scaled.high;lo=scaled.low;
        std::memcpy(mappedHigh,hi.data(),sizeof(hi));
        if(hot&&mappedLow){
            kharvox::sfs::DoomTemporalUniformSnapshot bound;bound.high=hi;bound.low=lo;
            const auto active=kharvox::sfs::DoomTemporalParameters::renderExtent(bound,g.extent);const char* reason{};
            if(!active||!kharvox::sfs::DoomTemporalParameters::currentMatches(captured.view,bound,*active,g.extent,&reason))
                throw std::runtime_error(std::string("Replay contract frame=")+std::to_string(serial)+": "+(reason?reason:"extent"));
        }
#endif
        captured.observe();sentinel=0x12340000u+uint32_t(serial);
        g.vkCmdBindPipeline(g.command,VK_PIPELINE_BIND_POINT_COMPUTE,computePipeline);g.vkCmdBindDescriptorSets(g.command,VK_PIPELINE_BIND_POINT_COMPUTE,computeLayout,0,1,&computeSet,0,nullptr);push(g.command,computeLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,4,&sentinel);
#endif
        if(serial==1){g.Upload(color,colorUpload,VK_IMAGE_LAYOUT_UNDEFINED);g.Upload(depth,depthUpload,VK_IMAGE_LAYOUT_UNDEFINED);g.Upload(motion,motionUpload,VK_IMAGE_LAYOUT_UNDEFINED);g.Barrier(outPacked,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);}
        g.Barrier(outColor,serial==1?VK_IMAGE_LAYOUT_UNDEFINED:VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        // DOOM supplies clear values even though this pass uses LOAD, not CLEAR.
        VkClearValue ignoredClears[2]{};ignoredClears[0].color.float32[0]=99.f;
        VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=pass;begin.framebuffer=framebuffer;begin.renderArea.extent=g.extent;begin.clearValueCount=2;begin.pClearValues=ignoredClears;g.vkCmdBeginRenderPass(g.command,&begin,VK_SUBPASS_CONTENTS_INLINE);
        g.vkCmdBindPipeline(g.command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);g.vkCmdBindDescriptorSets(g.command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout,0,1,&set,0,nullptr);
        VkViewport viewport{0,0,float(g.extent.width),float(g.extent.height),0,1};VkRect2D scissor{{0,0},g.extent};
#ifdef KHARVOX_HAVE_RECONSTRUCTION
        // DOOM's TAA viewport remains at allocation size when the scene is scaled.
#endif
        setViewport(g.command,0,1,&viewport);setScissor(g.command,0,1,&scissor);g.vkCmdDraw(g.command,3,1,0,0);g.vkCmdEndRenderPass(g.command);
#ifdef KHARVOX_HAVE_RECONSTRUCTION
        if(reconstruct){
            g.Barrier(outColor,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            if(hot){
                // Exact engine schedule from 11 captured submissions: TAA and
                // scene producer are still UNSUBMITTED across acquire. The
                // final command follows them in the same queue submission.
                queuedProducer=g.command;G::Check(g.vkEndCommandBuffer(g.command));
                auto acquired=pose;++acquired.serial;
                kharvox::sfs::prepare(g.device,acquired,fov);kharvox::sfs::copyCompleted(g.device);kharvox::sfs::beginFrame(g.device);
                g.pool=splitPool;g.command=splitCommand;g.Begin();
                g.vkCmdBindPipeline(g.command,VK_PIPELINE_BIND_POINT_COMPUTE,computePipeline);g.vkCmdBindDescriptorSets(g.command,VK_PIPELINE_BIND_POINT_COMPUTE,computeLayout,0,1,&computeSet,0,nullptr);push(g.command,computeLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,4,&sentinel);
            }
            final.draw(serial,setViewport,setScissor);
            const auto resets=kharvox::sfs::dlssTestResetEvaluations(g.device);
            // A cold Neural feature can exceed the 250ms continuity budget on
            // its first frame; check its next steady pair, not that valid cut.
            const bool stableCycle=fullCycle&&serial>5&&serial!=neuralFrame&&serial!=neuralFrame+1&&
                serial!=dynamicFrame&&serial!=dynamicFrame+1&&serial<dlaaFrame&&(serial-1)%captured.samples.size()!=0;
            if(hot&&g.extent.width<=256&&(stableCycle||(!fullCycle&&(serial==5||serial==8||serial==11)))){
                need(resets==previousResets,"Continuous pending producer route reset temporal history");
                std::cout<<"PASS continuous reconstruction history across acquire frame="<<serial<<" resets="<<resets<<'\n';
            }
            previousResets=resets;
        }
#endif
        #ifdef KHARVOX_HAVE_DLSS_STARTUP
        // No rebind: NGX must restore the game's previous compute state.
        dispatch(g.command,1,1,1);
        VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};host.buffer=computeValue.handle;host.size=4;host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;host.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        g.vkCmdPipelineBarrier(g.command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
#endif
        #ifdef KHARVOX_HAVE_RECONSTRUCTION
        g.Barrier(outColor,reconstruct?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
#else
        g.Barrier(outColor,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
#endif
#ifdef KHARVOX_HAVE_RECONSTRUCTION
        if(reconstruct){g.Barrier(final.target,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);g.Download(final.target,readback);}else g.Download(outColor,readback);
#else
        g.Download(outColor,readback);
#endif
#ifdef KHARVOX_HAVE_DLSS_STARTUP
        kharvox::dlss::Output ngxOutput;
        if(serial!=2){uint64_t neuralCount{};need(kharvox::sfs::dlssTestOutput(g.device,ngxOutput,&neuralCount),"No native output for pixel comparison");
#ifdef KHARVOX_HAVE_RECONSTRUCTION
            if(kharvox::overlay::current().mode==4){need(neuralCount>0,"Neural mode produced no Neural evaluations");std::cout<<"PASS actual Neural evaluations="<<neuralCount<<" frame="<<serial<<'\n';}
#endif
            for(unsigned e=0;e<2;++e){const auto& image=ngxOutput.eyes[e];
                VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.image=image.image;barrier.subresourceRange=image.range;
                barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.oldLayout=image.layout;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;barrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
                g.vkCmdPipelineBarrier(g.command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
                VkBufferImageCopy copy{};copy.bufferOffset=VkDeviceSize(e)*pixels*8;copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={g.extent.width,g.extent.height,1};
                g.vkCmdCopyImageToBuffer(g.command,image.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,ngxReadback.handle,1,&copy);
                std::swap(barrier.oldLayout,barrier.newLayout);barrier.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
                g.vkCmdPipelineBarrier(g.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
            }
            VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};host.buffer=ngxReadback.handle;host.size=VK_WHOLE_SIZE;host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
            g.vkCmdPipelineBarrier(g.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
        }
#endif
#ifdef KHARVOX_HAVE_RECONSTRUCTION
        if(queuedProducer){
            G::Check(g.vkEndCommandBuffer(g.command));G::Check(g.vkResetFences(g.device,1,&g.done));
            VkCommandBuffer batch[]{queuedProducer,g.command};VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=2;submit.pCommandBuffers=batch;
            need(kharvox::sfs::dlssValidateSubmission(g.device,g.queue,1,&submit),"Ordered producer/final submission rejected");
            VkSubmitInfo separateBatches[]{submit,submit};for(unsigned k=0;k<2;++k){separateBatches[k].commandBufferCount=1;separateBatches[k].pCommandBuffers=&batch[k];}
            need(kharvox::sfs::dlssValidateSubmission(g.device,g.queue,2,separateBatches),"Producer/final across ordered batches rejected");
            VkSubmitInfo missing=submit;missing.commandBufferCount=1;missing.pCommandBuffers=&g.command;
            need(!kharvox::sfs::dlssValidateSubmission(g.device,g.queue,1,&missing),"Unsubmitted producer omitted from final batch");
            VkCommandBuffer reversed[]{g.command,queuedProducer};VkSubmitInfo wrong=submit;wrong.pCommandBuffers=reversed;
            need(!kharvox::sfs::dlssValidateSubmission(g.device,g.queue,1,&wrong),"Reversed producer/final order accepted");
            need(!kharvox::sfs::dlssValidateSubmission(g.device,VK_NULL_HANDLE,1,&submit),"Foreign graphics queue accepted");
            VkCommandBufferSubmitInfo commands2[2]{};for(unsigned k=0;k<2;++k){commands2[k]={VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};commands2[k].commandBuffer=batch[k];}
            VkSubmitInfo2 submit2{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};submit2.commandBufferInfoCount=2;submit2.pCommandBufferInfos=commands2;
            need(kharvox::sfs::dlssValidateSubmission2(g.device,g.queue,1,&submit2),"Ordered Submit2 rejected");
            std::swap(commands2[0],commands2[1]);need(!kharvox::sfs::dlssValidateSubmission2(g.device,g.queue,1,&submit2),"Reversed Submit2 accepted");
            G::Check(g.vkQueueSubmit(g.queue,1,&submit,g.done));G::Check(g.vkQueueWaitIdle(g.queue));
            deferredSubmission.assign(std::begin(batch),std::end(batch));
            std::cout<<"PASS real engine pending producer -> acquire -> final -> joint submission frame="<<serial<<"; reversed/missing/foreign queue rejected before GPU\n";
        }else
#endif
        {g.Finish();
#ifdef KHARVOX_HAVE_DLSS_STARTUP
            deferredSubmission={g.command};
#endif
        }
#ifdef KHARVOX_HAVE_RECONSTRUCTION
        if(serial!=2){
            need(kharvox::sfs::outputRenderedAt(g.device,g.extent,serial),"Completed reconstruction not associated with its output dimensions");
            need(!kharvox::sfs::outputRenderedAt(g.device,{g.extent.width+2,g.extent.height},serial),"Output resize accepted a stale image size");
            need(!kharvox::sfs::outputRenderedAt(g.device,g.extent,serial+1),"Output resize accepted another frame's reconstruction");
        }
#endif
#ifdef KHARVOX_HAVE_DLSS_STARTUP
        void* computeMapped{};G::Check(g.vkMapMemory(g.device,computeValue.allocation,0,4,0,&computeMapped));need(*static_cast<uint32_t*>(computeMapped)==sentinel,"NGX did not restore compute pipeline, descriptors and push constants");g.vkUnmapMemory(g.device,computeValue.allocation);
#endif
        void* mapped{};G::Check(g.vkMapMemory(g.device,readback.allocation,0,readback.size,0,&mapped));
#ifdef KHARVOX_HAVE_DLSS_STARTUP
        const auto* actual=static_cast<const uint32_t*>(mapped);for(unsigned e=0;e<2;++e){double red=0,blue=0;for(unsigned p=0;p<pixels;++p){const auto pixel=actual[e*pixels+p];const auto r=pixel&2047u,b=pixel>>22;red+=std::ldexp(1.+double(r&63)/64,int(r>>6)-15);blue+=std::ldexp(1.+double(b&31)/32,int(b>>5)-15);}need(e?blue>red+.2*pixels:red>blue+.2*pixels,"Native DLAA crossed eyes or lost source color");}
#ifdef KHARVOX_HAVE_RECONSTRUCTION
        if(hot&&serial>=neuralFrame&&serial<dlaaFrame){
            for(unsigned eye=0;eye<2;++eye){unsigned marked{};
                for(unsigned p=0;p<pixels;++p){
                    if(p%g.extent.width<8&&p/g.extent.width<8)continue; // Green HUD is unrelated.
                    const auto pixel=actual[eye*pixels+p];
                    const auto decode=[](unsigned v,unsigned bits){const unsigned exp=v>>bits,frac=v&((1u<<bits)-1);return exp?std::ldexp(1.f+float(frac)/float(1u<<bits),int(exp)-15):std::ldexp(float(frac),-14-int(bits));};
                    const auto r=decode(pixel&2047u,6),g=decode((pixel>>11)&2047u,6),b=decode(pixel>>22,5);
                    marked+=g>.7f&&g>r+.15f&&g>b+.15f;
                }
                need(marked>20,"Foveation indicator absent from final game color in one eye");
            }
            std::cout<<"PASS live foveation indicator in both final eye attachments frame="<<serial<<'\n';
        }
#endif
        if(serial!=2){void* reference{};G::Check(g.vkMapMemory(g.device,ngxReadback.allocation,0,ngxReadback.size,0,&reference));
            const auto* halves=static_cast<const uint16_t*>(reference);
            for(unsigned n=0;n<pixels*2;++n)for(unsigned c=0;c<3;++c){
#ifdef KHARVOX_HAVE_RECONSTRUCTION
                if(reconstruct&&n%g.extent.width<8&&(n%pixels)/g.extent.width<8)continue; // Independent HUD verified below.
                if(reconstruct&&final.renderMode==2.f){
                    const float y=(float((n%pixels)/g.extent.width)+.5f)/g.extent.height;
                    if(y<.05f||y>.95f){need(actual[n]==0,"Engine letterbox mask was lost after reconstruction");continue;}
                }
#endif
                const uint16_t h=halves[n*4+c];const unsigned exponent=(h>>10)&31,mantissa=h&1023;
                const float expected=exponent?std::ldexp(1.f+float(mantissa)/1024,int(exponent)-15):std::ldexp(float(mantissa),-24);
                const unsigned bits=c==2?5:6,shift=c==0?0:c==1?11:22,mask=(1u<<(bits+5))-1,v=(actual[n]>>shift)&mask;
                const unsigned exp=v>>bits,frac=v&((1u<<bits)-1);
                const float value=exp?std::ldexp(1.f+float(frac)/float(1u<<bits),int(exp)-15):std::ldexp(float(frac),-14-int(bits));
                const float ulp=std::ldexp(1.f,std::max(-14,std::ilogb(std::max(expected,0x1p-14f)))-int(bits));
                need(std::isfinite(expected)&&std::abs(value-expected)<=ulp,"Game color attachment differs from actual DLAA output");
                visiblyProcessedPixels+=c==0&&actual[n]!=packed[n];
            }g.vkUnmapMemory(g.device,ngxReadback.allocation);
        }
#ifndef KHARVOX_HAVE_RECONSTRUCTION
        else need(!std::memcmp(actual,packed.data(),size_t(readback.size)),"Unavailable parameters did not restore raw HDR fallback");
#else
        if(reconstruct)for(unsigned e=0;e<2;++e){const auto hud=actual[e*pixels+2*g.extent.width+2];
            const auto channel=[](unsigned v,unsigned bits){const unsigned exp=v>>bits,frac=v&((1u<<bits)-1);return exp?std::ldexp(1.f+float(frac)/float(1u<<bits),int(exp)-15):std::ldexp(float(frac),-14-int(bits));};
            const float red=channel(hud&2047u,6),green=channel((hud>>11)&2047u,6),blue=channel(hud>>22,5);
            need(green>red+.15f&&green>blue+.15f,"Independent GUI was lost or reconstructed");
        }
#endif
#ifdef KHARVOX_HAVE_RECONSTRUCTION
        need(kharvox::sfs::dlssEvaluations(g.device)>0,"Native reconstruction did not evaluate");
#else
        need(kharvox::sfs::dlssEvaluations(g.device)==(serial==1?2u:2u*(serial-1)),"Native SFS skipped DLAA with alternating completed command pools");
#endif
#else
        need(!std::memcmp(mapped,packed.data(),size_t(readback.size)),"Native bridge output differs from HDR source");
#endif
        g.vkUnmapMemory(g.device,readback.allocation);
        kharvox::sfs::DoomTemporalUniformSnapshot snapshot;uint64_t observedSerial{};
        const bool observed=kharvox::sfs::temporalUniformSnapshot(g.device,observedSerial,snapshot);
        need(observed==(serial!=2),"Mapped UBO availability did not follow map lifetime");
        if(observed)need(observedSerial==serial&&snapshot.high==hi&&snapshot.low==lo,"TAA uniform bytes or frame association differ from bound buffers");
        #ifdef KHARVOX_HAVE_DLSS_STARTUP
        std::cout<<"PASS native SFS DLAA frame="<<serial<<" eye evaluations="<<kharvox::sfs::dlssEvaluations(g.device)<<"; stereo color and unavailable-parameter fallback\n";
#else
        std::cout<<"PASS native SFS bridge stereo HDR frame="<<serial<<" exact LOAD/STORE color attachments; mapped uniforms="<<observed<<'\n';
#endif
    }
#ifdef KHARVOX_HAVE_DLSS_STARTUP
    for(auto pool:rotatingPools)G::Check(g.vkResetCommandPool(g.device,pool,0));
#ifndef KHARVOX_HAVE_RECONSTRUCTION
    need(visiblyProcessedPixels>pixels,"Processed attachment indistinguishable from raw HDR bypass");
#endif
    std::cout<<"PASS native output pixels match NGX within game-format rounding; changed pixels="<<visiblyProcessedPixels<<'\n';
    g.vkDestroyCommandPool(g.device,rotatingPools[0],nullptr);
    g.pool=rotatingPools[1];g.command=rotatingCommands[1];
    g.vkDestroyPipeline(g.device,computePipeline,nullptr);g.vkDestroyShaderModule(g.device,computeModule,nullptr);g.vkDestroyPipelineLayout(g.device,computeLayout,nullptr);g.vkDestroyDescriptorPool(g.device,computePool,nullptr);g.vkDestroyDescriptorSetLayout(g.device,computeSetLayout,nullptr);
#endif
#ifdef KHARVOX_HAVE_RECONSTRUCTION
    final.release();g.vkDestroyCommandPool(g.device,splitPool,nullptr);
#endif
    g.vkUnmapMemory(g.device,high.allocation);if(mappedLow)g.vkUnmapMemory(g.device,low.allocation);
    g.vkDestroyPipeline(g.device,pipeline,nullptr);for(auto m:modules)g.vkDestroyShaderModule(g.device,m,nullptr);g.vkDestroyFramebuffer(g.device,framebuffer,nullptr);g.vkDestroyRenderPass(g.device,pass,nullptr);g.vkDestroyPipelineLayout(g.device,pipelineLayout,nullptr);g.vkDestroyDescriptorPool(g.device,pool,nullptr);g.vkDestroyDescriptorSetLayout(g.device,layout,nullptr);g.vkDestroySampler(g.device,sampler,nullptr);
    // Destruction of tracked fixture images and commands remains on the layer;
    // shut it down after those objects have been explicitly released.
    for(auto t:g.textures){g.vkDestroyImageView(g.device,t.view,nullptr);g.vkDestroyImage(g.device,t.handle,nullptr);g.vkFreeMemory(g.device,t.allocation,nullptr);}g.textures.clear();
    for(auto b:g.buffers){g.vkDestroyBuffer(g.device,b.handle,nullptr);g.vkFreeMemory(g.device,b.allocation,nullptr);}g.buffers.clear();
    g.vkDestroyCommandPool(g.device,g.pool,nullptr);g.pool={};kharvox::sfs::shutdown(g.device);return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
