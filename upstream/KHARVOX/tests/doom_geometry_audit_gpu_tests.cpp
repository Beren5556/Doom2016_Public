#include "VulkanTestDevice.h"
#include "../src/sfs/NativeSfs.h"
#include "../src/sfs/FrameProjection.h"
#include "../src/sfs/ShaderCompiler.h"
#include "../src/sfs/DoomGeometryAudit.h"
#include "../src/sfs/TemporalAuditCapture.h"
#include "../src/sfs/TemporalContextVulkan.h"
#include "../src/native/NativeStereo.h"
#include <fstream>
#include <algorithm>
#include <set>

using G=VulkanTestDevice;
using namespace kharvox::sfs;
static PFN_vkQueueSubmit actualQueueSubmit;
static uint64_t contextDevice;
static VKAPI_ATTR VkResult VKAPI_CALL contextSubmit(VkQueue q,uint32_t n,const VkSubmitInfo* s,VkFence f){
    const auto result=actualQueueSubmit(q,n,s,f);kharvox::temporal_context::submitted(contextDevice,q,n,s,f,result);return result;
}
#define WRAPPED(X) \
 X(vkCreateImage) X(vkDestroyImage) X(vkCreateImageView) X(vkDestroyImageView) \
 X(vkCreateShaderModule) X(vkDestroyShaderModule) X(vkCreateRenderPass) X(vkDestroyRenderPass) \
 X(vkCreateFramebuffer) X(vkDestroyFramebuffer) X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) \
 X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) \
 X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) X(vkCreateGraphicsPipelines) X(vkDestroyPipeline) \
 X(vkAllocateCommandBuffers) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkDestroyCommandPool) \
 X(vkCmdBeginRenderPass) X(vkCmdEndRenderPass) X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) X(vkCmdDraw) X(vkResetCommandPool)
static void need(bool b,const char* why){if(!b)throw std::runtime_error(why);}
static std::vector<uint32_t> words(const char* path){std::ifstream f(path,std::ios::binary|std::ios::ate);need(bool(f),"Missing shader");auto n=f.tellg();need(n>=20&&n%4==0,"Bad shader");std::vector<uint32_t> w(size_t(n)/4);f.seekg(0);need(bool(f.read(reinterpret_cast<char*>(w.data()),n)),"Bad shader read");return w;}
struct Test {
    G g{false,{8,8},nullptr,true};G::Buffer uniform,vertices,indices,read;G::Texture output;
    VkDescriptorSetLayout setLayout{};VkDescriptorPool pool{};VkDescriptorSet set{};VkPipelineLayout layout{};
    VkRenderPass pass{};VkFramebuffer fb{};VkShaderModule vertex{},fragment{};VkPipeline pipeline{};
    PFN_vkCmdBindVertexBuffers bindVertices{};PFN_vkCmdBindIndexBuffer bindIndex{};PFN_vkCmdDrawIndexed indexed{};
    PFN_vkCmdSetViewport viewport{};PFN_vkCmdSetScissor scissor{};
    std::filesystem::path path;uint32_t stride{};uint64_t expectedShaderHash{doomGeometryAuditHash};FrameUniforms eyes;
    std::array<std::array<float,32>,2> high{};std::array<float,16> low{};
    std::array<std::array<float,15>,6> points{};
    Test(const char* vs,const char* fs,bool audit=true,const char* profile=nullptr){
        path=std::filesystem::temp_directory_path()/("kharvox-geometry-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+".jsonl");
        SetEnvironmentVariableW(L"KHARVOX_TEMPORAL_AUDIT_PATH",path.c_str());SetEnvironmentVariableA("KHARVOX_GEOMETRY_AUDIT",audit?"1":"0");
        SetEnvironmentVariableA("KHARVOX_SFS_PROFILE","");
        if(profile){const auto directory=std::filesystem::path(path.wstring()+L".profile");std::filesystem::create_directory(directory);std::filesystem::copy_file(profile,directory/"f70ac337f3d62c58_VS.vert.spv");SetEnvironmentVariableW(L"KHARVOX_SFS_PROFILE",directory.c_str());expectedShaderHash=doomGeometryAuditProfileHash;}
        SetEnvironmentVariableA("KHARVOX_TEMPORAL_CONTEXT","1");
        need(initialize(g.device,VK_NULL_HANDLE,g.resolve,g.memory,{true,g.properties.limits}),"Native geometry init failed");
        actualQueueSubmit=g.vkQueueSubmit;contextDevice=reinterpret_cast<uintptr_t>(g.device);g.vkQueueSubmit=contextSubmit;
#define WRAP(n) g.n=reinterpret_cast<PFN_##n>(wrapProc(g.device,#n,reinterpret_cast<PFN_vkVoidFunction>(g.n)));
        WRAPPED(WRAP)
#undef WRAP
#define EXTRA(v,n) v=reinterpret_cast<PFN_##n>(wrapProc(g.device,#n,g.resolve(g.device,#n)));need(v,#n)
        EXTRA(bindVertices,vkCmdBindVertexBuffers);EXTRA(bindIndex,vkCmdBindIndexBuffer);EXTRA(indexed,vkCmdDrawIndexed);EXTRA(viewport,vkCmdSetViewport);EXTRA(scissor,vkCmdSetScissor);
#undef EXTRA
        reinterpret_cast<PFN_vkFreeCommandBuffers>(g.resolve(g.device,"vkFreeCommandBuffers"))(g.device,g.pool,1,&g.command);
        VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=g.pool;ca.commandBufferCount=1;G::Check(g.vkAllocateCommandBuffers(g.device,&ca,&g.command));
        stride=uint32_t(std::max<VkDeviceSize>(256,g.properties.limits.minUniformBufferOffsetAlignment));
        uniform=g.MakeBuffer(stride*8,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);vertices=g.MakeBuffer(sizeof(points),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);indices=g.MakeBuffer(12,VK_BUFFER_USAGE_INDEX_BUFFER_BIT);read=g.MakeBuffer(128*16,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        const uint32_t ix[]{3,4,5};g.Write(indices,ix);
        const float xyz[3][2]{{-.8f,-.8f},{.8f,-.8f},{0,.8f}};
        for(unsigned n=3;n<6;++n){points[n][0]=xyz[n-3][0];points[n][1]=xyz[n-3][1];points[n][2]=.2f;points[n][3]=1;points[n][4]=float(n)*.1f;points[n][5]=float(n)*.2f;}
        g.Write(vertices,points.data());
        for(unsigned j=0;j<2;++j){auto& h=high[j];h[0]=h[5]=.7f;h[10]=h[15]=1;h[3]=j?.2f:-.2f;h[16]=h[21]=h[26]=h[28]=1;}
        low[0]=low[1]=1;for(unsigned n=4;n<8;++n)low[n]=1;low[8]=.03f;low[12]=17;low[13]=23;low[14]=29;
        Upload();output=g.MakeTexture(VK_FORMAT_R32G32B32A32_SFLOAT,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,1);
        std::array<VkDescriptorSetLayoutBinding,24> bs{};
        for(unsigned n=0;n<24;++n){auto& b=bs[n];b.binding=n;b.descriptorCount=1;b.stageFlags=n<2?VK_SHADER_STAGE_VERTEX_BIT:VK_SHADER_STAGE_FRAGMENT_BIT;b.descriptorType=n<4?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:n<11?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:n<21?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:n<23?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;}
        VkDescriptorSetLayoutCreateInfo sc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};sc.bindingCount=24;sc.pBindings=bs.data();need(TemporalAuditCapture::supportsGeometry(sc),"Exact layout rejected");bs[0].stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;need(!TemporalAuditCapture::supportsGeometry(sc),"Wrong stage accepted");bs[0].stageFlags=VK_SHADER_STAGE_VERTEX_BIT;
        G::Check(g.vkCreateDescriptorSetLayout(g.device,&sc,nullptr,&setLayout));
        VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,4},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,7},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,10},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,2},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1}};
        VkDescriptorPoolCreateInfo pc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pc.maxSets=1;pc.poolSizeCount=5;pc.pPoolSizes=sizes;G::Check(g.vkCreateDescriptorPool(g.device,&pc,nullptr,&pool));
        VkDescriptorSetAllocateInfo sa{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};sa.descriptorPool=pool;sa.descriptorSetCount=1;sa.pSetLayouts=&setLayout;G::Check(g.vkAllocateDescriptorSets(g.device,&sa,&set));
        for(unsigned n=0;n<4;++n){VkDescriptorBufferInfo bi{uniform.handle,0,n==0?128u:64u};VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=set;w.dstBinding=n;w.descriptorCount=1;w.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;w.pBufferInfo=&bi;g.vkUpdateDescriptorSets(g.device,1,&w,0,nullptr);}
        VkPipelineLayoutCreateInfo lc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};lc.setLayoutCount=1;lc.pSetLayouts=&setLayout;G::Check(g.vkCreatePipelineLayout(g.device,&lc,nullptr,&layout));
        VkAttachmentDescription a{};a.format=output.format;a.samples=VK_SAMPLE_COUNT_1_BIT;a.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;a.storeOp=VK_ATTACHMENT_STORE_OP_STORE;a.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;a.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        VkAttachmentReference ref{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=&ref;
        VkSubpassDependency dep{};dep.srcSubpass=0;dep.dstSubpass=VK_SUBPASS_EXTERNAL;dep.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;dep.dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT;dep.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;dep.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        VkRenderPassCreateInfo rc{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};rc.attachmentCount=1;rc.pAttachments=&a;rc.subpassCount=1;rc.pSubpasses=&sub;rc.dependencyCount=1;rc.pDependencies=&dep;G::Check(g.vkCreateRenderPass(g.device,&rc,nullptr,&pass));
        VkFramebufferCreateInfo fc{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fc.renderPass=pass;fc.attachmentCount=1;fc.pAttachments=&output.view;fc.width=fc.height=8;fc.layers=1;G::Check(g.vkCreateFramebuffer(g.device,&fc,nullptr,&fb));
        auto module=[&](const std::vector<uint32_t>& w){VkShaderModuleCreateInfo c{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};c.codeSize=w.size()*4;c.pCode=w.data();VkShaderModule m{};G::Check(g.vkCreateShaderModule(g.device,&c,nullptr,&m));return m;};
        const auto original=words(vs);need(profileHash(original.data(),uint32_t(original.size()*4))==doomGeometryAuditHash,"Wrong original VS");vertex=module(original);fragment=module(words(fs));
        ShaderCompileOptions opts;opts.vertexProjection=opts.geometryAudit=true;need(compileStereoShader(original,opts).geometryAuditApplied,"Probe absent");
        auto reject=[&](const auto& w,const auto& o){try{compileStereoShader(w,o);return false;}catch(...){return true;}};
        auto wrong=original;wrong[2]^=1;need(reject(wrong,opts),"Changed fingerprint accepted");opts.monoscopicView=true;need(reject(original,opts),"Mono geometry audit accepted");opts.monoscopicView=false;opts.profileReplacement=true;need(reject(original,opts),"Profile geometry audit accepted");
        VkPipelineShaderStageCreateInfo stages[2]{};for(auto& s:stages){s.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;s.pName="main";}stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=vertex;stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=fragment;
        VkVertexInputBindingDescription vb{0,sizeof(points[0]),VK_VERTEX_INPUT_RATE_VERTEX};VkVertexInputAttributeDescription attrs[]{{0,0,VK_FORMAT_R32G32B32A32_SFLOAT,0},{1,0,VK_FORMAT_R32G32_SFLOAT,16},{2,0,VK_FORMAT_R32G32B32_SFLOAT,24},{12,0,VK_FORMAT_R32G32_SFLOAT,36},{13,0,VK_FORMAT_R32G32B32A32_SFLOAT,44}};
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};vi.vertexBindingDescriptionCount=1;vi.pVertexBindingDescriptions=&vb;vi.vertexAttributeDescriptionCount=5;vi.pVertexAttributeDescriptions=attrs;
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};vp.viewportCount=vp.scissorCount=1;
        VkDynamicState ds[]{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};dy.dynamicStateCount=2;dy.pDynamicStates=ds;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};rs.lineWidth=1;VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState ba{};ba.colorWriteMask=15;VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blend.attachmentCount=1;blend.pAttachments=&ba;
        VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};pi.stageCount=2;pi.pStages=stages;pi.pVertexInputState=&vi;pi.pInputAssemblyState=&ia;pi.pViewportState=&vp;pi.pRasterizationState=&rs;pi.pMultisampleState=&ms;pi.pColorBlendState=&blend;pi.pDynamicState=&dy;pi.layout=layout;pi.renderPass=pass;G::Check(g.vkCreateGraphicsPipelines(g.device,VK_NULL_HANDLE,1,&pi,nullptr,&pipeline));
    }
    void Upload(){std::vector<float> bytes(size_t(uniform.size)/4,-987.f);for(unsigned j=0;j<2;++j)std::memcpy(reinterpret_cast<char*>(bytes.data())+stride*(1+j),high[j].data(),128);std::memcpy(reinterpret_cast<char*>(bytes.data())+stride*3,low.data(),64);g.Write(uniform,bytes.data());}
    void Retire(uint64_t serial){
        kharvox::native::FramePose pose;pose.serial=serial;pose.worldScale=1;pose.gameplay=true;pose.head.orientation.w=1;
        const XrFovf fov{-.7f,.7f,.7f,-.7f};
        for(unsigned e=0;e<2;++e){pose.views[e].pose.orientation.w=1;pose.views[e].fov=fov;pose.views[e].pose.position.x=e?.003f:-.003f;}pose.views[1].fov.angleRight=.75f;
        need(frameProjection(pose.head,fov,pose.views,pose.worldScale,true,eyes),"Projection fixture rejected");prepare(g.device,pose,fov);copyCompleted(g.device);beginFrame(g.device);
    }
    std::vector<float> Draw(unsigned repeats=2){
        g.Begin();VkClearValue clear{};VkRenderPassBeginInfo b{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};b.renderPass=pass;b.framebuffer=fb;b.renderArea.extent={8,8};b.clearValueCount=1;b.pClearValues=&clear;
        g.vkCmdBeginRenderPass(g.command,&b,VK_SUBPASS_CONTENTS_INLINE);g.vkCmdBindPipeline(g.command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
        const VkViewport v{0,0,8,8,0,1};const VkRect2D r{{1,1},{6,6}};viewport(g.command,0,1,&v);scissor(g.command,0,1,&r);
        VkDeviceSize zero{};bindVertices(g.command,0,1,&vertices.handle,&zero);bindIndex(g.command,indices.handle,0,VK_INDEX_TYPE_UINT32);
        for(unsigned j=0;j<repeats;++j){uint32_t offsets[]{stride*(1+j%2),stride*3,stride*4,stride*5};g.vkCmdBindDescriptorSets(g.command,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&set,4,offsets);indexed(g.command,3,2,0,0,7);}
        g.vkCmdEndRenderPass(g.command);g.Download(output,read);g.Finish();return g.Read(read);
    }
    void Repeat(){VkSubmitInfo s{VK_STRUCTURE_TYPE_SUBMIT_INFO};s.commandBufferCount=1;s.pCommandBuffers=&g.command;G::Check(g.vkQueueSubmit(g.queue,1,&s,VK_NULL_HANDLE));G::Check(g.vkQueueWaitIdle(g.queue));}
    std::vector<std::string> Batches(){std::ifstream f(path);std::string l;std::vector<std::string> b;while(std::getline(f,l))if(l.find("\"kind\":\"batch\"")!=std::string::npos)b.push_back(l);return b;}
    void CheckLast(unsigned count){auto bs=Batches();need(bs.size()>=count,"Missing batches");std::set<uint64_t> tokens;
        for(unsigned j=0;j<count;++j){const auto& line=bs[bs.size()-count+j];size_t start=line.find("\"records\":[");need(start!=std::string::npos,"No records");std::vector<uint32_t> raw;
            for(size_t k=start+11;k<line.size();){if(line[k]>='0'&&line[k]<='9'){size_t used{};raw.push_back(uint32_t(std::stoul(line.substr(k),&used)));k+=used;}else ++k;}
            need(raw.size()==320,"Expected two geometry samples");unsigned mask{};const auto drawStart=line.find("\"draw\":\"");need(drawStart!=std::string::npos,"Draw ordinal missing");const unsigned variant=unsigned(std::stoul(line.substr(drawStart+8))-1)%2;
            for(unsigned n=0;n<2;++n){auto r=raw.data()+n*160;need(r[0]<2&&r[1]==3,"Bad geometry eye/schema");unsigned e=r[0];mask|=1<<e;need((uint64_t(r[82])|(uint64_t(r[83])<<32))==expectedShaderHash,"Wrong actual shader input fingerprint");const uint64_t token=uint64_t(r[2])|(uint64_t(r[3])<<32);tokens.insert(token);
                need(token&&line.find("\"operationId\":\""+std::to_string(token)+"\"")!=std::string::npos,"Token mismatch");
                need(!std::memcmp(r+4,high[variant].data(),128)&&!std::memcmp(r+36,low.data(),64),"Consumed UBO bytes mismatch");
                need(!std::memcmp(r+52,eyes.clip[e].data(),64)&&!std::memcmp(r+68,eyes.translation[e].data(),16),"Eye transform mismatch");
                need(r[80]>=3&&r[80]<=5&&r[81]>=7&&r[81]<=8,"Wrong indexed vertex/instance");need(!std::memcmp(r+72,points[r[80]].data(),16),"Vertex input mismatch");
                std::array<float,4> local{},center{},expected{},actual{};for(unsigned k=0;k<4;++k)local[k]=points[r[80]][k]*low[4+k]+low[8+k];
                for(unsigned row=0;row<4;++row)for(unsigned k=0;k<4;++k)center[row]+=high[variant][row*4+k]*local[k];center[1]=-center[1];
                for(unsigned row=0;row<4;++row){expected[row]=eyes.translation[e][row];for(unsigned k=0;k<4;++k)expected[row]+=eyes.clip[e][k*4+row]*center[k];}std::memcpy(actual.data(),r+76,16);
                for(unsigned k=0;k<4;++k)need(std::abs(actual[k]-expected[k])<.00001f,"Final clip coordinate mismatch");
            }need(mask==3,"Missing eye");need(line.find("\"descriptorGenerationMatches\":true")!=std::string::npos,"Descriptor lifetime mismatch");
            const auto offsets="\"dynamicOffsets\":["+std::to_string(stride*(1+variant))+","+std::to_string(stride*3)+","+std::to_string(stride*4)+","+std::to_string(stride*5)+"]";
            need(line.find(offsets)!=std::string::npos,"Dynamic offset sequence mismatch");need(line.find("\"scissor\":[1,1,6,6]")!=std::string::npos,"Scissor missing");
        }need(tokens.size()==count,"Per-draw tokens aliased");
    }
    ~Test(){g.vkQueueWaitIdle(g.queue);g.vkDestroyPipeline(g.device,pipeline,nullptr);g.vkDestroyFramebuffer(g.device,fb,nullptr);g.vkDestroyRenderPass(g.device,pass,nullptr);g.vkDestroyPipelineLayout(g.device,layout,nullptr);g.vkDestroyDescriptorPool(g.device,pool,nullptr);g.vkDestroyDescriptorSetLayout(g.device,setLayout,nullptr);g.vkDestroyShaderModule(g.device,vertex,nullptr);g.vkDestroyShaderModule(g.device,fragment,nullptr);shutdown(g.device);
#define RESTORE(n) g.n=reinterpret_cast<PFN_##n>(g.resolve(g.device,#n));
        WRAPPED(RESTORE)
#undef RESTORE
    }
};
int main(int argc,char** argv){try{
    need(argc==3||argc==4,"Expected original geometry VS, fixture FS, optional profile");const char* profile=argc==4?argv[3]:nullptr;SetEnvironmentVariableA("KHARVOX_SFS_NATIVE_PROBE","1");SetEnvironmentVariableA("KHARVOX_SFS_NATIVE_VR","1");
    std::vector<float> plain;{Test baseline(argv[1],argv[2],false,profile);baseline.Retire(1);plain=baseline.Draw();}
    Test t(argv[1],argv[2],true,profile);t.Retire(1);auto reference=t.Draw();need(reference==plain,"Instrumentation changed unarmed output");t.Retire(2);need(t.Batches().empty(),"Captured before arm");
    {std::ofstream arm(t.path.wstring()+L".arm");arm<<"lab";}for(unsigned n=0;n<60;++n)t.Retire(0x100000007ULL);
    // Reuse the already-recorded command, proving arming works without re-record.
    t.Repeat();auto active=t.g.Read(t.read);t.Retire(0x100000008ULL);t.CheckLast(2);
    need(reference==active&&plain==active,"Geometry capture changed rendered output");need(std::count_if(active.begin(),active.end(),[](float x){return x!=0;})>24,"Empty output reference");
    for(auto v:active)need(std::isfinite(v),"Nonfinite render");
    t.high[0][7]=.12f;t.high[1][7]=-.13f;t.low[8]=-.04f;t.Upload();t.Repeat();t.Retire(0x100000009ULL);t.CheckLast(2);
    t.Draw();t.Draw();t.Retire(0x10000000aULL);t.CheckLast(4);
    const auto before=t.Batches().size();t.Draw(70);t.Retire(0x10000000bULL);need(t.Batches().size()==before+64,"Geometry draw budget failed");t.CheckLast(64);
    for(unsigned n=0;n<28;++n){t.Draw();t.Retire(20+n);}auto size=std::filesystem::file_size(t.path);t.Draw();t.Retire(99);need(std::filesystem::file_size(t.path)==size,"Geometry capture did not stop");
    std::ifstream f(t.path);std::string all((std::istreambuf_iterator<char>(f)),{});need(all.find("\"observedBatches\":32")!=std::string::npos&&all.find("\"exhaustedOperations\":0")!=std::string::npos,"Capture completion/capacity failed");
    std::cout<<"PASS geometry runtime: actual indexed UBO/vertex/clip values per eye; distinct draw tokens; four dynamic offsets; command reuse and re-recording; bit-identical rendered output; 64-draw bound and 32-batch stop. Evidence: "<<t.path.string()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
