#define KHARVOX_TEMPORAL_BRIDGE_TEST
#include "doom_temporal_resolve_gpu_tests.cpp"
#include "../src/sfs/DoomTemporalBridge.h"

struct BridgeTest:ResolveTest {
    std::unique_ptr<kharvox::sfs::DoomTemporalBridge> bridge;
    VkPipelineLayout bridgeLayout{};
    BridgeTest(const char* vs,const char* fs,const char* copy):ResolveTest(vs,fs,copy,{64,80}){
        kharvox::sfs::ShaderCompileOptions options;options.profileReplacement=true;options.temporalGuides=options.temporalColor=true;
        auto guide=compileStereoShader(readShader(fs),options);options.temporalGuides=options.temporalColor=false;options.temporalResolve=true;
        auto resolved=compileStereoShader(readShader(fs),options);auto guideModule=Module(guide.words),resolveModule=Module(resolved.words);
        bridge=std::make_unique<kharvox::sfs::DoomTemporalBridge>(g.device,g.resolve,g.memory);
        bridgeLayout=PipelineLayout({layouts[0]});
        Pipeline(Module(readShader(vs)),resolveModule,gamePass,resolveLayout,[&](const VkGraphicsPipelineCreateInfo& original){
            auto ci=original;ci.layout=bridgeLayout;
            VkDynamicState states[]{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};dynamic.dynamicStateCount=2;dynamic.pDynamicStates=states;ci.pDynamicState=&dynamic;
            VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layout.setLayoutCount=1;layout.pSetLayouts=&layouts[0];
            bridge->initialize(ci,layout,guideModule,resolveModule);
        });
    }
    void Run(unsigned frame){
        const unsigned pixels=g.extent.width*g.extent.height;std::vector<float> depths(pixels*2),motionValues(pixels*4),colors(pixels*8);
        for(unsigned e=0;e<2;++e)for(unsigned p=0;p<pixels;++p){const auto n=e*pixels+p;depths[n]=e?.25f:.5f;
            motionValues[n*2]=frame%2?(e?-.375f:.125f):2;motionValues[n*2+1]=frame%2?(e?.25f:-.25f):0;
            for(unsigned c=0;c<4;++c)colors[n*4+c]=color(e,p%g.extent.width,p/g.extent.width,c,frame);}
        depths[g.extent.width*3+3]=.1f;pr[68]=frame==2?.25f:0;pr[84]=frame==2?-.5f:0;
        g.Write(high,hi.data());g.Write(low,lo.data());g.Write(projection,pr.data());g.Write(depthUpload,depths.data());g.Write(motionUpload,motionValues.data());g.Write(sceneUpload,colors.data());
        g.Begin();auto old=uploaded?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED;
        g.Upload(depth,depthUpload,old);g.Upload(motion,motionUpload,old);for(auto t:{scene,history,secondary})g.Upload(t,sceneUpload,old);uploaded=true;
        Draw(gamePass,gameFramebuffer,originalPipeline,resolveLayout,{gameSet,eyeSet});Readback();g.Finish();const auto baseline=g.Read(readPacked);
        g.Begin();bridge->retired();
        VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=gamePass;begin.framebuffer=gameFramebuffer;begin.renderArea.extent=g.extent;
        g.vkCmdBeginRenderPass(g.command,&begin,VK_SUBPASS_CONTENTS_INLINE);
        VkViewport vp{0,0,float(g.extent.width),float(g.extent.height),0,1};VkRect2D sc{{0,0},g.extent};
        auto bind=[&]{g.vkCmdBindPipeline(g.command,VK_PIPELINE_BIND_POINT_GRAPHICS,originalPipeline);g.vkCmdBindDescriptorSets(g.command,VK_PIPELINE_BIND_POINT_GRAPHICS,bridgeLayout,0,1,&gameSet,0,nullptr);
            reinterpret_cast<PFN_vkCmdSetViewport>(g.resolve(g.device,"vkCmdSetViewport"))(g.command,0,1,&vp);
            reinterpret_cast<PFN_vkCmdSetScissor>(g.resolve(g.device,"vkCmdSetScissor"))(g.command,0,1,&sc);};
        auto draw=[&]{g.vkCmdDraw(g.command,3,1,0,0);};
        auto cropped=vp;cropped.width-=1;need(!bridge->record(g.command,begin,g.extent,cropped,sc,bind,draw),"Cropped viewport accepted");
        need(bridge->record(g.command,begin,g.extent,vp,sc,bind,draw),"Bridge rejected supported draw");
        need(!bridge->record(g.command,begin,g.extent,vp,sc,bind,draw),"Images reused before retirement");
        g.vkCmdEndRenderPass(g.command);Readback();g.Finish();const auto packed=g.Read(readPacked),rgb=g.Read(readColor);
        for(unsigned p=0;p<pixels*2;++p){for(unsigned c=0;c<3;++c)need(rgb[p*4+c]==colors[p*4+c],"Bridge color changed/eye crossed/dilated");
            for(unsigned c=0;c<2;++c)need(std::memcmp(&packed[p*4+c],&baseline[p*4+c],4)==0,"Packed output changed");}
        std::cout<<"PASS bridge frame="<<frame<<" exact stereo HDR and packed RG; rejected cropped/reused resources\n";
    }
    void RunScaled(VkExtent2D active){
        // Use the real TAA shader with a nonconstant current image. Constant
        // images cannot expose a second spatial/temporal filter before DLSS.
        const unsigned pixels=g.extent.width*g.extent.height;
        std::vector<float> depths(pixels*2,.5f),motionValues(pixels*4,0),colors(pixels*8);
        for(unsigned e=0;e<2;++e)for(unsigned y=0;y<g.extent.height;++y)for(unsigned x=0;x<g.extent.width;++x)
            for(unsigned c=0;c<4;++c)colors[((e*g.extent.height+y)*g.extent.width+x)*4+c]=color(e,x,y,c,0);
        lo[12]=float(active.width)/g.extent.width;lo[13]=float(active.height)/g.extent.height;
        lo[14]=1/lo[12];lo[15]=1/lo[13];pr[68]=pr[84]=0;
        g.Write(high,hi.data());g.Write(low,lo.data());g.Write(projection,pr.data());
        g.Write(depthUpload,depths.data());g.Write(motionUpload,motionValues.data());g.Write(sceneUpload,colors.data());
        g.Begin();g.Upload(depth,depthUpload,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);g.Upload(motion,motionUpload,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        for(auto t:{scene,history,secondary})g.Upload(t,sceneUpload,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        Draw(gamePass,gameFramebuffer,originalPipeline,resolveLayout,{gameSet,eyeSet});Readback();g.Finish();
        const auto baseline=g.Read(readPacked),filtered=g.Read(readColor);
        size_t filteredPixels{};
        for(unsigned e=0;e<2;++e)for(unsigned y=0;y<active.height;++y)for(unsigned x=0;x<active.width;++x){
            const auto n=((e*g.extent.height+y)*g.extent.width+x)*4;
            for(unsigned c=0;c<3;++c)filteredPixels+=filtered[n+c]!=colors[n+c];
        }
        need(filteredPixels>0,"Scaled regression fixture did not expose engine TAA filtering");
        std::cout<<"Scaled TAA control active="<<active.width<<'x'<<active.height<<" changedChannels="<<filteredPixels<<std::endl;
        g.Begin();bridge->retired();
        VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=gamePass;begin.framebuffer=gameFramebuffer;begin.renderArea.extent=g.extent;
        g.vkCmdBeginRenderPass(g.command,&begin,VK_SUBPASS_CONTENTS_INLINE);
        VkViewport vp{0,0,float(g.extent.width),float(g.extent.height),0,1};VkRect2D sc{{0,0},g.extent};
        auto bind=[&]{g.vkCmdBindPipeline(g.command,VK_PIPELINE_BIND_POINT_GRAPHICS,originalPipeline);g.vkCmdBindDescriptorSets(g.command,VK_PIPELINE_BIND_POINT_GRAPHICS,bridgeLayout,0,1,&gameSet,0,nullptr);
            reinterpret_cast<PFN_vkCmdSetViewport>(g.resolve(g.device,"vkCmdSetViewport"))(g.command,0,1,&vp);
            reinterpret_cast<PFN_vkCmdSetScissor>(g.resolve(g.device,"vkCmdSetScissor"))(g.command,0,1,&sc);};
        auto draw=[&]{g.vkCmdDraw(g.command,3,1,0,0);};
        need(bridge->record(g.command,begin,active,vp,sc,bind,draw,{},true),"Qualified scaled bridge rejected");
        g.vkCmdEndRenderPass(g.command);Readback();g.Finish();
        const auto packed=g.Read(readPacked),rgb=g.Read(readColor);
        for(unsigned e=0;e<2;++e)for(unsigned y=0;y<active.height;++y)for(unsigned x=0;x<active.width;++x){
            const auto n=((e*g.extent.height+y)*g.extent.width+x)*4;
            for(unsigned c=0;c<3;++c)need(rgb[n+c]==colors[n+c],"Scaled DLSS source still filtered by original engine TAA");
            for(unsigned c=0;c<2;++c)need(std::memcmp(&packed[n+c],&baseline[n+c],4)==0,"Scaled packed velocity/depth changed");
        }
        std::cout<<"PASS scaled active="<<active.width<<'x'<<active.height<<" raw HDR exact in both eyes, original packed RG preserved\n";
    }
    ~BridgeTest(){g.vkQueueWaitIdle(g.queue);bridge.reset();}
};
int main(int argc,char** argv)try{need(argc==4,"Expected fixture shaders");BridgeTest test(argv[1],argv[2],argv[3]);for(unsigned i=0;i<4;++i)test.Run(i);for(auto extent:{VkExtent2D{32,40},VkExtent2D{43,53},VkExtent2D{48,60}})test.RunScaled(extent);return 0;}
catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
