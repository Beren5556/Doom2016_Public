#define main chainFixtureMain
#include "doom_dlss_chain_gpu_tests.cpp"
#undef main
#include "../src/sfs/DoomDlssSession.h"

int main(int argc,char** argv)try{
    need(argc==7,"Expected shader/runtime fixture arguments");
    NgxFixture::runtime=std::filesystem::absolute(argv[4]);NgxFixture::cache=std::filesystem::absolute(argv[5]);
    ChainTest fixture(argv[1],argv[2],argv[3]);fixture.provider.reset();auto& g=fixture.g;using G=std::remove_reference_t<decltype(g)>;
    dl::Options options;options.instance=g.instance;options.physical=g.physical;options.device=g.device;options.queue=g.queue;options.queueFamily=g.family;
    options.gipa=g.gipa;options.gdpa=&ProviderCalls::Get;options.externallySynchronized=true;options.runtimeDirectory=NgxFixture::runtime;options.cacheDirectory=NgxFixture::cache;options.projectId=NgxFixture::project;
    kharvox::sfs::DoomDlssSession session(options);
    const auto usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    auto motion=g.MakeTexture(VK_FORMAT_R32G32_SFLOAT,usage),depth=g.MakeTexture(VK_FORMAT_R32_SFLOAT,usage),color=g.MakeTexture(VK_FORMAT_R16G16B16A16_SFLOAT,usage);
    const size_t pixels=size_t(g.extent.width)*g.extent.height;
    std::vector<float> mv(pixels*4,0),z(pixels*2,.5f);std::vector<uint16_t> hdr(pixels*8,0);
    for(unsigned e=0;e<2;++e)for(size_t p=0;p<pixels;++p){hdr[(e*pixels+p)*4+(e?2:0)]=0x4000;hdr[(e*pixels+p)*4+3]=0x3c00;}
    auto bm=g.MakeBuffer(mv.size()*4,VK_BUFFER_USAGE_TRANSFER_SRC_BIT),bz=g.MakeBuffer(z.size()*4,VK_BUFFER_USAGE_TRANSFER_SRC_BIT),bc=g.MakeBuffer(hdr.size()*2,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    g.Write(bm,mv.data());g.Write(bz,z.data());g.Write(bc,hdr.data());
    kharvox::sfs::DoomTemporalBridge::Inputs resources;resources.extent=g.extent;resources.images={motion.handle,depth.handle,color.handle};
    std::array<decltype(motion),3> textures{motion,depth,color};for(unsigned n=0;n<3;++n)for(unsigned e=0;e<2;++e)resources.eyeViews[n][e]=fixture.EyeView(textures[n],e);
    kharvox::sfs::DoomTemporalParameters temporal;temporal.previousMatches=true;
    VkCommandPool pools[2]{g.pool,{}};VkCommandBuffer commands[2]{g.command,{}};
    VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};cp.queueFamilyIndex=g.family;
    G::Check(g.vkCreateCommandPool(g.device,&cp,nullptr,&pools[1]));
    VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=pools[1];ca.commandBufferCount=1;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    G::Check(g.vkAllocateCommandBuffers(g.device,&ca,&commands[1]));
    for(uint64_t frame=1;frame<=8;++frame){g.pool=pools[(frame-1)%2];g.command=commands[(frame-1)%2];g.Begin();session.discarded(g.command);
        need(!session.pendingCommand(),"Reset after completed work did not release recording owner");
        if(frame==1){g.Upload(motion,bm,VK_IMAGE_LAYOUT_UNDEFINED);g.Upload(depth,bz,VK_IMAGE_LAYOUT_UNDEFINED);g.Upload(color,bc,VK_IMAGE_LAYOUT_UNDEFINED);}
        const auto calls=ProviderCalls::forbidden;
        if(frame==1)need(!session.record(g.command,resources,temporal,frame,frame*16000000,{} )[0]&&!session.pendingCommand(),"Unknown exposure was evaluated");
        const auto views=session.record(g.command,resources,temporal,frame,frame*16000000,1.f);
        need(views[0]&&views[1]&&views[0]!=views[1],session.error().c_str());need(ProviderCalls::forbidden==calls,"Session submitted or waited while recording");
        need(!session.retiredAfterIdle()&&session.pendingCommand(),"Unsubmitted work retired merely by idle notification");
        need(!session.completionToken(g.queue)&&!session.completed(0),"Unsubmitted work received a completion token");
        session.observeSubmission(g.queue,g.command,VK_ERROR_DEVICE_LOST);
        need(!session.retiredAfterIdle()&&session.pendingCommand(),"Failed submission was treated as completed GPU work");
        if(frame==2){G::Check(g.vkResetCommandPool(g.device,g.pool,0));session.discarded(g.command);need(!session.pendingCommand(),"Discarded unsubmitted recording not cancelled");continue;}
        for(const auto& output:session.output().eyes)need(output.layout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,"Resolve descriptor layout mismatch");
        fixture.ReadOutput(session.output());g.Finish();session.observeSubmission(g.queue,g.command,VK_SUCCESS);
        if(frame==3){G::Check(g.vkResetCommandPool(g.device,g.pool,0));session.discarded(g.command);need(session.pendingCommand()!=VK_NULL_HANDLE,"Reset alone retired submitted work without the idle observation");}
        G::Check(ProviderCalls::F<PFN_vkDeviceWaitIdle>(g.device,"vkDeviceWaitIdle")(g.device));
        const auto token=session.completionToken(g.queue);
        need(token&&!session.completionToken(reinterpret_cast<VkQueue>(uintptr_t(1))),"Completion token accepted a different queue");
        need(!session.completed(token+1)&&session.pendingCommand(),"Stale completion token retired another recording");
        need((frame<4?session.retiredAfterIdle():session.completed(token))&&!session.pendingCommand(),"Completed GPU work remained blocked on CPU command recycling");
        need(!session.completed(token),"Completed token was consumed twice");
        for(unsigned e=0;e<2;++e){const auto data=fixture.ReadHalf(fixture.ngxRead[e]);double own=0,other=0;for(size_t p=0;p<pixels;++p){own+=ChainTest::Half(data[p*4+(e?2:0)]);other+=ChainTest::Half(data[p*4+(e?0:2)]);}need(own>pixels&&own>other+pixels*.2,"Session crossed eyes or lost HDR");}
    }
    need(session.evaluations()==16&&session.resetEvaluations()==4,"DLAA continuity changed");
    // True SR keeps reduced guides and reconstructs a separate, larger output.
    // Check all four corners, including pixels beyond the entire input extent.
    for(unsigned e=0;e<2;++e)for(unsigned y=0;y<g.extent.height;++y)for(unsigned x=0;x<g.extent.width;++x){
        const auto p=e*pixels+y*g.extent.width+x;
        hdr[p*4+(e?2:0)]=exactHalf(x<g.extent.width/2?1.f:3.f);
        hdr[p*4+1]=exactHalf(y<g.extent.height/2?.25f:.75f);
    }
    const dl::Mode modes[]{dl::Mode::Quality,dl::Mode::Balanced,dl::Mode::Performance,dl::Mode::UltraPerformance};
    const VkExtent2D sizes[]{{240,288},{276,332},{320,384},{480,576}};
    const auto sourceSize=g.extent;
    g.Write(bc,hdr.data());
    for(uint64_t frame=9;frame<=20;++frame){
        const auto scenario=unsigned((frame-9)/3);const auto outputSize=sizes[scenario];
        if((frame-9)%3==0)for(auto& read:fixture.ngxRead)read=g.MakeBuffer(VkDeviceSize(outputSize.width)*outputSize.height*8,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        g.Begin();session.discarded(g.command);
        if(frame==9)g.Upload(color,bc,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        kharvox::sfs::DoomReconstruction reconstruction{modes[scenario],outputSize,true};
        if(frame==9){auto unknown=reconstruction;unknown.engineMappingQualified=false;
            need(!session.record(g.command,resources,temporal,frame,frame*16000000,1.f,unknown)[0],"Unqualified engine scaling accepted");}
        const auto calls=ProviderCalls::forbidden;
        auto views=session.record(g.command,resources,temporal,frame,frame*16000000,1.f,reconstruction);
        need(views[0]&&views[1],session.error().c_str());
        need(ProviderCalls::forbidden==calls,"SR session added command lifecycle/submit/wait");
        for(const auto& eye:session.output().eyes)need(eye.extent.width==outputSize.width&&eye.extent.height==outputSize.height,"SR output remained at input size");
        g.extent=outputSize;fixture.ReadOutput(session.output());g.Finish();g.extent=sourceSize;
        session.observeSubmission(g.queue,g.command,VK_SUCCESS);
        need(session.completed(session.completionToken(g.queue)),"SR completion not retired");
        for(unsigned e=0;e<2;++e){const auto data=fixture.ReadHalf(fixture.ngxRead[e]);
            for(unsigned y:{8u,outputSize.height-9})for(unsigned x:{8u,outputSize.width-9}){
                const auto p=(size_t(y)*outputSize.width+x)*4;
                need(std::abs(ChainTest::Half(data[p+(e?2:0)])-(x<outputSize.width/2?1.f:3.f))<.12f,"SR eye/corner cropped or incorrect");
                need(std::abs(ChainTest::Half(data[p+1])-(y<outputSize.height/2?.25f:.75f))<.08f,"SR vertical mapping incorrect");
            }
        }
    }
    for(auto pool:pools)G::Check(g.vkResetCommandPool(g.device,pool,0));
    g.vkDestroyCommandPool(g.device,pools[0],nullptr);g.pool=pools[1];g.command=commands[1];
    need(!session.pendingCommand()&&session.evaluations()==40,"Incomplete session retirement/evaluation sequence");
    need(session.resetEvaluations()==12,"History reset beyond initialization, cancellation and quality changes");
    for(const auto& views:resources.eyeViews)for(auto view:views)g.vkDestroyImageView(g.device,view,nullptr);
    std::cout<<"PASS session: 40 real NGX evaluations, DLAA plus four SR qualities with full-size corner readback; 12 reset eyes at initialization/cancellation/quality boundaries only\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
