#define KHARVOX_DLSS_CHAIN_TEST
#include "doom_temporal_resolve_gpu_tests.cpp"
#include "../src/dlss/VulkanDlss.h"
#include "../src/dlss/VulkanDlssRequirements.h"
#include "../src/sfs/DoomTemporalBridge.h"
#include <nvsdk_ngx_helpers_vk.h>
#include <tlhelp32.h>
#include <limits>

namespace dl=kharvox::dlss;
struct NgxFixture {
    inline static std::filesystem::path runtime,cache;
    inline static dl::ExtensionSelection selection;
    static constexpr const char* project="953a53d7-aeb4-49bb-957d-4a3e5c31c6f6";
    static std::vector<const char*> Extensions(VkInstance instance,VkPhysicalDevice physical){
        dl::Options o;o.runtimeDirectory=runtime;o.cacheDirectory=cache;o.projectId=project;o.instance=instance;o.physical=physical;
        const auto gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(GetModuleHandleW(L"vulkan-1.dll"),"vkGetInstanceProcAddr"));
        need(gipa!=nullptr,"Fixture Vulkan dispatch missing");
        const auto required=instance?dl::DeviceRequirements(o):dl::InstanceRequirements(o);
        const auto available=instance?dl::DeviceExtensions(physical,reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(gipa(instance,"vkEnumerateDeviceExtensionProperties"))):
            dl::InstanceExtensions(reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(gipa(nullptr,"vkEnumerateInstanceExtensionProperties")));
        selection=dl::SelectExtensions(0,nullptr,required,available);need(selection.Ready(),"NGX extensions unavailable on host");
        std::cout<<"NGX "<<(instance?"device":"instance")<<" requirements verified="<<selection.names.size()<<'\n';return selection.Pointers();
    }
};

// Observe the provider's dispatch separately from the synchronous test host.
struct ProviderCalls {
    inline static PFN_vkGetDeviceProcAddr original{};
    inline static uint64_t forbidden{};
    static PFN_vkVoidFunction VKAPI_PTR Get(VkDevice d,const char* name){
        // Count and forward the real calls. Record must leave this count intact;
        // teardown is allowed to wait for the host's borrowed completion fence.
        if(std::strcmp(name,"vkQueueSubmit")==0)return reinterpret_cast<PFN_vkVoidFunction>(&Submit);
        if(std::strcmp(name,"vkQueueWaitIdle")==0)return reinterpret_cast<PFN_vkVoidFunction>(&QueueIdle);
        if(std::strcmp(name,"vkDeviceWaitIdle")==0)return reinterpret_cast<PFN_vkVoidFunction>(&DeviceIdle);
        if(std::strcmp(name,"vkWaitForFences")==0)return reinterpret_cast<PFN_vkVoidFunction>(&Wait);
        if(std::strcmp(name,"vkBeginCommandBuffer")==0)return reinterpret_cast<PFN_vkVoidFunction>(&Begin);
        if(std::strcmp(name,"vkEndCommandBuffer")==0)return reinterpret_cast<PFN_vkVoidFunction>(&End);
        return original(d,name);
    }
    // Forward after counting: teardown legitimately waits for the borrowed fence.
    template<class T> static T F(VkDevice d,const char* name){return reinterpret_cast<T>(original(d,name));}
    inline static VkDevice device{};
    static VkResult VKAPI_PTR Submit(VkQueue q,uint32_t n,const VkSubmitInfo* s,VkFence f){++forbidden;return F<PFN_vkQueueSubmit>(device,"vkQueueSubmit")(q,n,s,f);}
    static VkResult VKAPI_PTR QueueIdle(VkQueue q){++forbidden;return F<PFN_vkQueueWaitIdle>(device,"vkQueueWaitIdle")(q);}
    static VkResult VKAPI_PTR DeviceIdle(VkDevice d){++forbidden;return F<PFN_vkDeviceWaitIdle>(d,"vkDeviceWaitIdle")(d);}
    static VkResult VKAPI_PTR Wait(VkDevice d,uint32_t n,const VkFence* f,VkBool32 all,uint64_t timeout){++forbidden;return F<PFN_vkWaitForFences>(d,"vkWaitForFences")(d,n,f,all,timeout);}
    static VkResult VKAPI_PTR Begin(VkCommandBuffer c,const VkCommandBufferBeginInfo* i){++forbidden;return F<PFN_vkBeginCommandBuffer>(device,"vkBeginCommandBuffer")(c,i);}
    static VkResult VKAPI_PTR End(VkCommandBuffer c){++forbidden;return F<PFN_vkEndCommandBuffer>(device,"vkEndCommandBuffer")(c);}
};

struct ChainTest:ResolveTest {
    Texture hdr,guideMotion,guideDepth;
    Buffer hdrUpload,guideMotionRead,guideDepthRead;
    std::array<Buffer,2> ngxRead;
    std::array<VkImageView,2> hdrViews{},motionViews{},depthViews{};
    VkRenderPass guidePass{};VkFramebuffer guideFramebuffer{};VkPipeline guidePipeline{};
    std::unique_ptr<dl::VulkanDlss> provider;
    VkFence completion{};uint64_t activeTicket{},hostSubmissions{},pixelsChecked{};bool submitted{};
    VkDeviceSize Pixels()const{return VkDeviceSize(g.extent.width)*g.extent.height;}
    std::unique_ptr<kharvox::sfs::DoomTemporalBridge> bridge;
    VkPipelineLayout bridgeLayout{};

    VkImageView EyeView(Texture t,unsigned eye){
        VkImageViewCreateInfo c{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};c.image=t.handle;c.format=t.format;c.viewType=VK_IMAGE_VIEW_TYPE_2D;c.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,eye,1};
        VkImageView view{};G::Check(g.vkCreateImageView(g.device,&c,nullptr,&view));return view;
    }
    dl::Image Input(Texture t,VkImageView view,unsigned eye,uint64_t frame,VkImageUsageFlags usage){
        dl::Image i;i.device=g.device;i.image=t.handle;i.view=view;i.format=t.format;i.extent=g.extent;
        i.range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,eye,1};i.usage=usage;i.identity={frame,1,eye};return i;
    }
    ChainTest(const char* vs,const char* fs,const char* copy,bool bridgeMode=false):ResolveTest(vs,fs,copy,{160,192},&NgxFixture::Extensions){
        hdr=g.MakeTexture(VK_FORMAT_B10G11R11_UFLOAT_PACK32,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        const auto guideUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        guideMotion=g.MakeTexture(VK_FORMAT_R32G32_SFLOAT,guideUsage);guideDepth=g.MakeTexture(VK_FORMAT_R32_SFLOAT,guideUsage);
        hdrUpload=g.MakeBuffer(Pixels()*2*4,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        guideMotionRead=g.MakeBuffer(Pixels()*2*8,VK_BUFFER_USAGE_TRANSFER_DST_BIT);guideDepthRead=g.MakeBuffer(Pixels()*2*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        for(unsigned e=0;e<2;++e){hdrViews[e]=EyeView(hdr,e);motionViews[e]=EyeView(guideMotion,e);depthViews[e]=EyeView(guideDepth,e);ngxRead[e]=g.MakeBuffer(Pixels()*8,VK_BUFFER_USAGE_TRANSFER_DST_BIT);}
        ImageBinding(gameSet,4,hdr);
        kharvox::sfs::ShaderCompileOptions options;options.profileReplacement=true;options.temporalGuides=true;
        auto guide=kharvox::sfs::compileStereoShader(readShader(fs),options);need(guide.temporalGuidesApplied&&guide.temporalCorrections==1,"Guide contract");
        guidePass=Pass(guideMotion,guideDepth,false);guideFramebuffer=Framebuffer(guidePass,guideMotion,guideDepth);
        guidePipeline=Pipeline(Module(readShader(vs)),Module(guide.words),guidePass,resolveLayout);
        dl::Options o;o.instance=g.instance;o.physical=g.physical;o.device=g.device;o.queue=g.queue;o.queueFamily=g.family;o.gipa=g.gipa;
        ProviderCalls::original=g.resolve;ProviderCalls::device=g.device;o.gdpa=&ProviderCalls::Get;
        o.runtimeDirectory=NgxFixture::runtime;o.cacheDirectory=NgxFixture::cache;o.projectId=NgxFixture::project;o.externallySynchronized=true;o.diagnosticLogging=true;
        provider=std::make_unique<dl::VulkanDlss>(o);
        if(bridgeMode){
            bridge=std::make_unique<kharvox::sfs::DoomTemporalBridge>(g.device,g.resolve,g.memory);bridgeLayout=PipelineLayout({layouts[0]});
            options.temporalColor=true;auto privateGuide=Module(kharvox::sfs::compileStereoShader(readShader(fs),options).words);
            options.temporalGuides=options.temporalColor=false;options.temporalResolve=true;auto privateResolve=Module(kharvox::sfs::compileStereoShader(readShader(fs),options).words);
            Pipeline(Module(readShader(vs)),privateResolve,gamePass,resolveLayout,[&](const VkGraphicsPipelineCreateInfo& original){auto ci=original;ci.layout=bridgeLayout;
                VkDynamicState states[]{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};dynamic.dynamicStateCount=2;dynamic.pDynamicStates=states;ci.pDynamicState=&dynamic;
                VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layout.setLayoutCount=1;layout.pSetLayouts=&layouts[0];bridge->initialize(ci,layout,privateGuide,privateResolve);});
        }
        VkFenceCreateInfo f{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};G::Check(g.vkCreateFence(g.device,&f,nullptr,&completion));
    }
    static float Half(uint16_t h){
        const float sign=(h&0x8000)?-1.f:1.f;const unsigned e=(h>>10)&31,m=h&1023;
        if(e==31)return m?std::numeric_limits<float>::quiet_NaN():sign*std::numeric_limits<float>::infinity();
        return sign*(e?std::ldexp(1.f+float(m)/1024,int(e)-15):std::ldexp(float(m),-24));
    }
    std::vector<uint16_t> ReadHalf(Buffer b){void* mapped{};G::Check(g.vkMapMemory(g.device,b.allocation,0,b.size,0,&mapped));
        std::vector<uint16_t> data(size_t(b.size)/2);std::memcpy(data.data(),mapped,size_t(b.size));g.vkUnmapMemory(g.device,b.allocation);return data;}
    void Barrier(const dl::Image& image,VkImageLayout before,VkImageLayout after){
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=image.image;b.subresourceRange=image.range;b.oldLayout=before;b.newLayout=after;
        b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;b.dstAccessMask=b.srcAccessMask;
        g.vkCmdPipelineBarrier(g.command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);
    }
    void ReadOutput(const dl::Output& output){
        for(unsigned e=0;e<2;++e){const auto& image=output.eyes[e];Barrier(image,image.layout,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            VkBufferImageCopy c{};c.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};c.imageExtent={g.extent.width,g.extent.height,1};
            g.vkCmdCopyImageToBuffer(g.command,image.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,ngxRead[e].handle,1,&c);Barrier(image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,image.layout);
            VkBufferMemoryBarrier b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};b.buffer=ngxRead[e].handle;b.size=VK_WHOLE_SIZE;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
            b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_HOST_READ_BIT;g.vkCmdPipelineBarrier(g.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&b,0,nullptr);}
    }
    void RunFrame(uint64_t frame){
        const unsigned scenario=unsigned((frame-1)/2);const auto pixels=size_t(Pixels());
        std::vector<float> depths(pixels*2),motions(pixels*4),colors(pixels*8);
        std::vector<uint32_t> packedHdr(pixels*2);
        for(unsigned e=0;e<2;++e)for(unsigned y=0;y<g.extent.height;++y)for(unsigned x=0;x<g.extent.width;++x){const auto n=e*pixels+y*g.extent.width+x;
            depths[n]=e?.25f:.5f;motions[n*2]=scenario==1?(e?-3.f/g.extent.width:1.f/g.extent.width):2;
            motions[n*2+1]=scenario==1?(e?2.f/g.extent.height:-2.f/g.extent.height):0;
            const float scale=frame==9?.5f:1.f;
            const float r=scale*(.25f+(e?0.f:1.f)+float(x%8)/32),gc=.25f+float(y%8)/32,b=scale*(.25f+(e?1.f:0.f)+float((x+y)%8)/32);
            packedHdr[n]=uint32_t(exactHalf(r)>>4)|(uint32_t(exactHalf(gc)>>4)<<11)|(uint32_t(exactHalf(b)>>5)<<22);
            colors[n*4]=r;colors[n*4+1]=gc;colors[n*4+2]=b;colors[n*4+3]=1;
        }
        pr[68]=scenario==2?2.f/g.extent.width:0;pr[84]=scenario==2?-4.f/g.extent.width:0;
        hi[54]=scenario==3?.5f:1;hi[55]=scenario==3?.25f:4;
        if(scenario==3){depths[3*g.extent.width+3]=.1f;depths[pixels+3*g.extent.width+3]=.05f;}
        g.Write(high,hi.data());g.Write(low,lo.data());g.Write(projection,pr.data());g.Write(depthUpload,depths.data());g.Write(motionUpload,motions.data());g.Write(sceneUpload,colors.data());g.Write(hdrUpload,packedHdr.data());
        // Separate baseline is only the packed-channel oracle. The actual chain
        // below produces its guides, evaluates NGX and consumes output in ONE CB.
        g.Begin();const auto old=uploaded?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED;
        g.Upload(depth,depthUpload,old);g.Upload(motion,motionUpload,old);g.Upload(hdr,hdrUpload,old);
        for(auto t:{history,secondary})g.Upload(t,sceneUpload,old);uploaded=true;
        Draw(gamePass,gameFramebuffer,originalPipeline,resolveLayout,{gameSet});Readback();g.Finish();const auto baseline=g.Read(readPacked);

        g.Begin();if(!bridge){Draw(guidePass,guideFramebuffer,guidePipeline,resolveLayout,{gameSet});
        for(auto pair:{std::make_pair(guideMotion,guideMotionRead),std::make_pair(guideDepth,guideDepthRead)}){
            g.Barrier(pair.first,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);g.Download(pair.first,pair.second);
            g.Barrier(pair.first,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);}}
        dl::PairInput input;input.mode=dl::Mode::Dlaa;input.temporalContractValidated=true;input.reset=frame%2==1;
        input.depthInverted=true; // Fixture reconstruction is Z=1/deviceDepth.
        for(unsigned e=0;e<2;++e){auto& eye=input.eyes[e];eye.color=Input(hdr,hdrViews[e],e,frame,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);
            eye.motion=Input(guideMotion,motionViews[e],e,frame,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
            eye.depth=Input(guideDepth,depthViews[e],e,frame,eye.motion.usage);input.output[e]=g.extent;}
        dl::Output output;
        auto evaluate=[&]{const auto calls=ProviderCalls::forbidden;
            auto invalid=input;invalid.temporalContractValidated=false;need(provider->Record(invalid,g.command,output)==dl::Status::Rejected&&!output.ticket,"Unqualified chain accepted");
            const auto status=provider->Record(input,g.command,output);activeTicket=output.ticket;
            need(status==dl::Status::Recorded,provider->Error().c_str());need(ProviderCalls::forbidden==calls,"Provider began/ended/submitted/waited during Record");
            for(const auto& image:output.eyes)need(image.layout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,"NGX output layout differs from private bridge descriptors");
            for(const auto& image:output.eyes)Barrier(image,image.layout,image.layout);};
        if(bridge){
            bridge->retired();VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=gamePass;begin.framebuffer=gameFramebuffer;begin.renderArea.extent=g.extent;
            g.vkCmdBeginRenderPass(g.command,&begin,VK_SUBPASS_CONTENTS_INLINE);VkViewport viewport{0,0,float(g.extent.width),float(g.extent.height),0,1};VkRect2D scissor{{0,0},g.extent};
            auto bind=[&]{g.vkCmdBindPipeline(g.command,VK_PIPELINE_BIND_POINT_GRAPHICS,originalPipeline);g.vkCmdBindDescriptorSets(g.command,VK_PIPELINE_BIND_POINT_GRAPHICS,bridgeLayout,0,1,&gameSet,0,nullptr);
                reinterpret_cast<PFN_vkCmdSetViewport>(g.resolve(g.device,"vkCmdSetViewport"))(g.command,0,1,&viewport);reinterpret_cast<PFN_vkCmdSetScissor>(g.resolve(g.device,"vkCmdSetScissor"))(g.command,0,1,&scissor);};
            const bool recorded=bridge->record(g.command,begin,g.extent,viewport,scissor,bind,[&]{g.vkCmdDraw(g.command,3,1,0,0);},[&](VkCommandBuffer command,const kharvox::sfs::DoomTemporalBridge::Inputs& resources){
                need(command==g.command,"Bridge changed host command buffer");const VkFormat formats[]{VK_FORMAT_R32G32_SFLOAT,VK_FORMAT_R32_SFLOAT,VK_FORMAT_R16G16B16A16_SFLOAT};
                for(unsigned e=0;e<2;++e){dl::Image* images[]{&input.eyes[e].motion,&input.eyes[e].depth,&input.eyes[e].color};for(unsigned n=0;n<3;++n){auto& image=*images[n];image.image=resources.images[n];image.view=resources.eyeViews[n][e];image.format=formats[n];image.extent=resources.extent;image.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;}}
                if(frame==9){input.temporalContractValidated=false;need(provider->Record(input,command,output)==dl::Status::Rejected&&!output.ticket,"Unqualified fallback accepted");return std::array<VkImageView,2>{};}
                evaluate();return std::array<VkImageView,2>{output.eyes[0].view,output.eyes[1].view};});
            need(recorded&&bridge->takeFailure().empty()&&(activeTicket||frame==9),"Bridge NGX recording failed");g.vkCmdEndRenderPass(g.command);
        }else{evaluate();for(unsigned e=0;e<2;++e){const auto& image=output.eyes[e];
            VkDescriptorImageInfo info{sampler,image.view,image.layout};VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=eyeSet;w.dstBinding=e;w.descriptorCount=1;w.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;w.pImageInfo=&info;g.vkUpdateDescriptorSets(g.device,1,&w,0,nullptr);}
        Draw(gamePass,gameFramebuffer,resolvePipeline,resolveLayout,{gameSet,eyeSet});}Readback();if(activeTicket)ReadOutput(output);
        G::Check(g.vkEndCommandBuffer(g.command));G::Check(g.vkResetFences(g.device,1,&completion));
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&g.command;
        G::Check(g.vkQueueSubmit(g.queue,1,&submit,completion));submitted=true;++hostSubmissions;
        if(bridge){G::Check(ProviderCalls::F<PFN_vkDeviceWaitIdle>(g.device,"vkDeviceWaitIdle")(g.device));
            if(activeTicket){const auto calls=ProviderCalls::forbidden;need(!provider->RetireRecordedAfterDeviceIdle(activeTicket+1),"Wrong retirement ticket accepted");
                need(provider->RetireRecordedAfterDeviceIdle(activeTicket),"Host idle retirement rejected");need(!provider->RetireRecordedAfterDeviceIdle(activeTicket),"Duplicate retirement accepted");need(calls==ProviderCalls::forbidden,"Retirement added submit/wait");}}
        else{need(provider->CommitRecorded(activeTicket,completion),"Host completion ticket rejected");G::Check(g.vkQueueWaitIdle(g.queue));}
        activeTicket=0;submitted=false;
        const auto rgb=g.Read(readColor),packed=g.Read(readPacked),mv=bridge?std::vector<float>{}:g.Read(guideMotionRead),z=bridge?std::vector<float>{}:g.Read(guideDepthRead);
        const bool fallback=bridge&&frame==9;
        const std::array<std::vector<uint16_t>,2> reference=fallback?std::array<std::vector<uint16_t>,2>{}:std::array<std::vector<uint16_t>,2>{ReadHalf(ngxRead[0]),ReadHalf(ngxRead[1])};
        uint64_t swappedMismatch{};double eyeSums[2][3]{};
        for(unsigned e=0;e<2;++e)for(size_t p=0;p<pixels;++p){const auto n=e*pixels+p;
            for(unsigned c=0;c<3;++c){const float expected=fallback?colors[n*4+c]:Half(reference[e][p*4+c]),actual=rgb[n*4+c];
                need(std::isfinite(expected)&&expected>=0&&std::isfinite(actual),"Invalid NGX/consumer color");
                const float ulp=std::ldexp(1.f,std::max(-14,std::ilogb(std::max(expected,0x1p-14f)))-(c==2?5:6));
                need(std::abs(actual-expected)<=ulp,"Consumer pixel differs from corresponding real NGX output");
                const float other=fallback?colors[((1-e)*pixels+p)*4+c]:Half(reference[1-e][p*4+c]);swappedMismatch+=std::abs(actual-other)>ulp;eyeSums[e][c]+=actual;}
            for(unsigned c=0;c<2;++c)need(std::memcmp(&packed[n*4+c],&baseline[n*4+c],4)==0,"Original packed blur/depth changed");
            const unsigned x=unsigned(p%g.extent.width),y=unsigned(p/g.extent.width);
            if(!bridge&&x>1&&x+2<g.extent.width&&y>1&&y+2<g.extent.height){
                const float ex=scenario==1?(e?3.f:-1.f):scenario==2?(e?-2.f:1.f):0,ey=scenario==1?(e?-2.f:2.f):0;
                need(std::isfinite(mv[n*2])&&std::abs(mv[n*2]-ex)<.001f&&std::abs(mv[n*2+1]-ey)<.001f,"Generated motion sign/scale wrong");
                need(std::abs(z[n]-depths[n])<.0001f,"Generated depth did not preserve selected sample");}
            ++pixelsChecked;
        }
        need(swappedMismatch>pixels,"Eye-swap negative control cannot distinguish the outputs");
        need(eyeSums[0][0]>eyeSums[0][2]+.2*pixels&&eyeSums[1][2]>eyeSums[1][0]+.2*pixels,"Reconstructed eyes crossed");
        if(!fallback)need(eyeSums[0][0]>pixels&&eyeSums[1][2]>pixels,"HDR values above one were lost");
        std::cout<<"PASS chain frame="<<frame<<" scenario="<<scenario<<" pixels="<<pixels*2<<" NGX evaluations="<<provider->Evaluations()<<" packed RG identical\n";
    }
    ~ChainTest(){
        g.vkQueueWaitIdle(g.queue);
        if(activeTicket){if(submitted)provider->CommitRecorded(activeTicket,completion);else{g.vkResetCommandPool(g.device,g.pool,0);provider->CancelRecorded(activeTicket);}}
        provider.reset();bridge.reset();if(completion)g.vkDestroyFence(g.device,completion,nullptr);
        for(const auto& views:{hdrViews,motionViews,depthViews})for(auto view:views)if(view)g.vkDestroyImageView(g.device,view,nullptr);
    }
};

int main(int argc,char** argv){try{
    need(argc==7||(argc==8&&std::strcmp(argv[7],"bridge")==0),"Expected vertex/profile/readback/runtime/cache/actual NGX module [bridge]");NgxFixture::runtime=std::filesystem::absolute(argv[4]);NgxFixture::cache=std::filesystem::absolute(argv[5]);std::filesystem::create_directories(NgxFixture::cache);
    const uint64_t frames=argc==8?10:8;ChainTest test(argv[1],argv[2],argv[3],argc==8);for(uint64_t frame=1;frame<=frames;++frame)test.RunFrame(frame);
    bool matched=false;const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());need(snapshot!=INVALID_HANDLE_VALUE,"Module inventory unavailable");
    MODULEENTRY32W entry{};entry.dwSize=sizeof(entry);if(Module32FirstW(snapshot,&entry))do{std::error_code ec;if(std::filesystem::equivalent(entry.szExePath,std::filesystem::path(argv[6]),ec)&&!ec){matched=true;break;}}while(Module32NextW(snapshot,&entry));CloseHandle(snapshot);need(matched,"Unexpected actual NGX module");
    need(test.hostSubmissions==frames&&test.provider->Evaluations()==(test.bridge?18:16),"Incomplete end-to-end chain");
    std::cout<<"PASS "<<test.pixelsChecked<<" eye pixels; "<<(test.bridge?"integrated temporal bridge with rejection fallback, resumed NGX and host idle retirement":"DOOM guides")<<" -> NGX DLAA -> game-format resolve. Synthetic scene, known zero jitter/preexposure1; live adapter not enabled.\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
