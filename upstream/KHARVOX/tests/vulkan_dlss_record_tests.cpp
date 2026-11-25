#define KHARVOX_DLSS_RECORD_TEST
#include "vulkan_dlss_tests.cpp"

// Observe actual Vulkan entry points supplied to the provider, including NGX.
// The host uses the original dispatch, so its one submission is independent.
struct ObservedDispatch {
    inline static PFN_vkGetDeviceProcAddr original{};
    inline static PFN_vkQueueSubmit submit{};
    inline static PFN_vkWaitForFences waitFences{};
    inline static PFN_vkDeviceWaitIdle idle{};
    inline static PFN_vkBeginCommandBuffer begin{};
    inline static PFN_vkEndCommandBuffer end{};
    inline static PFN_vkCreateImage createImage{};
    inline static uint64_t forbiddenCalls{},failAfterEvaluations{};
    inline static VulkanDlss* provider{};
    static VkResult VKAPI_PTR Submit(VkQueue q,uint32_t n,const VkSubmitInfo* s,VkFence f){++forbiddenCalls;return submit(q,n,s,f);}
    static VkResult VKAPI_PTR Wait(VkDevice d,uint32_t n,const VkFence* f,VkBool32 all,uint64_t t){++forbiddenCalls;return waitFences(d,n,f,all,t);}
    static VkResult VKAPI_PTR Idle(VkDevice d){++forbiddenCalls;return idle(d);}
    static VkResult VKAPI_PTR Begin(VkCommandBuffer c,const VkCommandBufferBeginInfo* b){++forbiddenCalls;return begin(c,b);}
    static VkResult VKAPI_PTR End(VkCommandBuffer c){++forbiddenCalls;return end(c);}
    static VkResult VKAPI_PTR CreateImage(VkDevice d,const VkImageCreateInfo* i,const VkAllocationCallbacks* a,VkImage* out){
        if(failAfterEvaluations&&provider&&provider->Evaluations()>=failAfterEvaluations){
            failAfterEvaluations=0;*out=VK_NULL_HANDLE;return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        return createImage(d,i,a,out);
    }
    static PFN_vkVoidFunction VKAPI_PTR Get(VkDevice d,const char* name){
        const auto f=original(d,name);
#define WRAP(api,member,handler) if(std::strcmp(name,#api)==0){member=reinterpret_cast<PFN_##api>(f);return reinterpret_cast<PFN_vkVoidFunction>(&handler);}
        WRAP(vkQueueSubmit,submit,Submit) WRAP(vkWaitForFences,waitFences,Wait)
        WRAP(vkDeviceWaitIdle,idle,Idle) WRAP(vkBeginCommandBuffer,begin,Begin)
        WRAP(vkEndCommandBuffer,end,End) WRAP(vkCreateImage,createImage,CreateImage)
#undef WRAP
        return f;
    }
};

struct ReadbackPair {
    Lab& lab;
    struct Eye {VkBuffer buffer{};VkDeviceMemory memory{};VkDeviceSize bytes{};VkExtent2D extent{};};
    std::array<Eye,2> eyes{};
    explicit ReadbackPair(Lab& l):lab(l){}
    void Record(const Output& output){
        need(output.eyes[0].image!=output.eyes[1].image,"Recorded eye outputs alias");
        for(unsigned e=0;e<2;++e){auto& host=eyes[e];const auto& i=output.eyes[e];host.extent=i.extent;host.bytes=VkDeviceSize(i.extent.width)*i.extent.height*8;
            VkBufferCreateInfo b{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};b.size=host.bytes;b.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            vkCheck(lab.createBuffer(lab.o.device,&b,nullptr,&host.buffer),"Same-buffer readback allocation");
            VkMemoryRequirements req{};lab.bufferRequirements(lab.o.device,host.buffer,&req);
            VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=lab.MemoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            vkCheck(lab.f.vkAllocateMemory(lab.o.device,&ai,nullptr,&host.memory),"Same-buffer readback memory");vkCheck(lab.bindBuffer(lab.o.device,host.buffer,host.memory,0),"Same-buffer readback bind");
            lab.Barrier(i,i.layout,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={i.extent.width,i.extent.height,1};
            lab.readback(lab.command,i.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,host.buffer,1,&copy);
            lab.Barrier(i,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,i.layout);
            VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};barrier.buffer=host.buffer;barrier.size=VK_WHOLE_SIZE;
            barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
            lab.f.vkCmdPipelineBarrier(lab.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&barrier,0,nullptr);
        }
    }
    void Check(){for(unsigned e=0;e<2;++e){const auto& host=eyes[e];void* mapped{};
        vkCheck(lab.map(lab.o.device,host.memory,0,host.bytes,0,&mapped),"Same-buffer readback map");
        const auto* words=static_cast<const uint16_t*>(mapped);double sums[3]{};bool finite=true;
        for(size_t p=0;p<host.bytes/8;++p)for(unsigned c=0;c<3;++c){const auto v=Lab::Half(words[p*4+c]);finite&=std::isfinite(v);sums[c]+=v;}
        lab.unmap(lab.o.device,host.memory);need(finite,"Recorded output not finite");
        need(sums[e?2:0]>sums[e?0:2]+0.2*host.extent.width*host.extent.height,"Recorded producer/consumer eye color mismatch");
    }}
    ~ReadbackPair(){for(auto& e:eyes){if(e.buffer)lab.destroyBuffer(lab.o.device,e.buffer,nullptr);if(e.memory)lab.f.vkFreeMemory(lab.o.device,e.memory,nullptr);}}
};

static void checkRuntime(const wchar_t* expected){
    bool matched=false;const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());need(snapshot!=INVALID_HANDLE_VALUE,"Module enumeration");
    MODULEENTRY32W entry{};entry.dwSize=sizeof(entry);
    if(Module32FirstW(snapshot,&entry))do{std::error_code ec;
        if(std::filesystem::equivalent(entry.szExePath,expected,ec)&&!ec){matched=true;break;}
    }while(Module32NextW(snapshot,&entry));CloseHandle(snapshot);need(matched,"Unexpected actual NGX module");
}

int wmain(int argc,wchar_t** argv){try{
    need(argc==4,"Expected runtime, cache and actual module");Lab lab;lab.Initialize(argv[1],argv[2]);
    auto options=lab.o;ObservedDispatch::original=options.gdpa;options.gdpa=&ObservedDispatch::Get;
    VulkanDlss provider(options);ObservedDispatch::provider=&provider;
    uint64_t epoch=0,submittedEyes=0;
    for(bool arrays:{false,true})for(auto mode:{Mode::Dlaa,Mode::Quality})for(auto size:{128u,160u,128u}){
        auto pair=lab.Inputs(size,size+32,++epoch,mode,arrays,false);Output output,unused;
        auto invalid=pair;invalid.temporalContractValidated=false;
        need(provider.Record(invalid,lab.command,unused)==Status::Rejected&&!unused.ticket,"Unqualified recording accepted");
        need(provider.Record(pair,VK_NULL_HANDLE,unused)==Status::Rejected&&!unused.ticket,"Null command accepted");
        for(uint64_t frame=1;frame<=3;++frame){
            if(frame>1)lab.Begin();
            for(auto& eye:pair.eyes)for(auto* i:{&eye.color,&eye.depth,&eye.motion})i->identity.frame=frame;
            pair.reset=frame==1;
            const auto calls=ObservedDispatch::forbiddenCalls;
            need(provider.Record(pair,lab.command,output)==Status::Recorded,provider.Error().c_str());
            need(ObservedDispatch::forbiddenCalls==calls,"Record began/ended/submitted/waited on command buffers");
            checkRuntime(argv[3]);
            need(output.ticket&&output.eyes[0].identity.frame==frame&&output.eyes[1].identity.eye==1,"Recorded identity lost");
            need(provider.Poll(unused)==Status::Recorded&&!unused.ticket,"Unsubmitted work exposed as ready");
            need(provider.Submit(pair)==Status::Busy,"Submission overwrote outstanding record");
            need(provider.Record(pair,lab.command,unused)==Status::Busy,"Record overwrote outstanding record");
            need(!provider.CommitRecorded(output.ticket+1,lab.done)&&!provider.CommitRecorded(output.ticket,VK_NULL_HANDLE),"Invalid retirement accepted");
            need(!provider.CancelRecorded(output.ticket+1),"Stale cancel accepted");
            ReadbackPair readback(lab);readback.Record(output);lab.Finish();submittedEyes+=2;
            VkFence held{};
            if(frame==1){VkFenceCreateInfo ci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};vkCheck(lab.f.vkCreateFence(lab.o.device,&ci,nullptr,&held),"Held recorded consumer");}
            need(provider.CommitRecorded(output.ticket,held?held:lab.done),"Recorded completion rejected");
            need(!provider.CommitRecorded(output.ticket,lab.done)&&!provider.CancelRecorded(output.ticket),"Ticket reused");
            readback.Check();
            if(held){need(provider.Record(pair,lab.command,unused)==Status::Busy,"Unfinished external consumer reused outputs");
                VkSubmitInfo signal{VK_STRUCTURE_TYPE_SUBMIT_INFO};vkCheck(lab.f.vkQueueSubmit(lab.o.queue,1,&signal,held),"Recorded consumer signal");
                vkCheck(lab.f.vkWaitForFences(lab.o.device,1,&held,VK_TRUE,10000000000ull),"Recorded consumer retirement");}
            need(provider.Record(pair,lab.command,unused)==Status::Rejected,"Duplicate recorded frame accepted");
            if(held)lab.f.vkDestroyFence(lab.o.device,held,nullptr);
        }
        std::cout<<"PASS recorded "<<(mode==Mode::Dlaa?"DLAA":"SR")<<" size="<<size<<" arrays="<<arrays<<" producer, NGX and consumer in one submission\n";
    }
    // Cancellation invalidates recorded references before releasing resources.
    auto pair=lab.Inputs(96,128,++epoch,Mode::Dlaa,true,false);Output output,unused;
    need(provider.Record(pair,lab.command,output)==Status::Recorded,"Cancellation recording failed");
    vkCheck(lab.f.vkResetCommandPool(lab.o.device,lab.pool,0),"Discard recorded command buffer");
    need(provider.CancelRecorded(output.ticket),"Cancellation rejected");
    // Re-create the unsubmitted inputs: cancellation discarded their clear too.
    pair=lab.Inputs(96,128,epoch,Mode::Dlaa,true,false);
    need(provider.Record(pair,lab.command,output)==Status::Recorded,"Canceled frame consumed identity");
    {ReadbackPair readback(lab);readback.Record(output);lab.Finish();submittedEyes+=2;need(provider.CommitRecorded(output.ticket,lab.done),"Cancel retry retirement");readback.Check();}
    need(provider.Record(pair,lab.command,unused)==Status::Rejected,"Cancel retry duplicate accepted");
    // Fail an allocation after eye zero has already recorded its evaluation.
    for(bool cancel:{true,false}){
        const auto width=cancel?112u:120u;
        pair=lab.Inputs(width,144,++epoch,Mode::Dlaa,false,false);
        ObservedDispatch::failAfterEvaluations=provider.Evaluations()+1;
        need(provider.Record(pair,lab.command,output)==Status::Failed,"Injected second-eye allocation failure missed");
        need(!ObservedDispatch::failAfterEvaluations&&output.ticket&&!output.eyes[0].image&&!output.eyes[1].image,"Partial pair published");
        need(provider.Record(pair,lab.command,unused)==Status::Busy,"Partial work freed before retirement");
        if(cancel){vkCheck(lab.f.vkResetCommandPool(lab.o.device,lab.pool,0),"Discard failed partial commands");need(provider.CancelRecorded(output.ticket),"Partial cancel rejected");}
        else {lab.Finish();need(provider.CommitRecorded(output.ticket,lab.done),"Partial completion rejected");
            auto invalid=pair;invalid.temporalContractValidated=false;need(provider.Record(invalid,lab.command,unused)==Status::Rejected,"Failed work retirement blocked retry");}
        pair=lab.Inputs(width,144,epoch,Mode::Dlaa,false,false);
        need(provider.Record(pair,lab.command,output)==Status::Recorded,"Partial failure retry failed");
        {ReadbackPair readback(lab);readback.Record(output);lab.Finish();submittedEyes+=2;need(provider.CommitRecorded(output.ticket,lab.done),"Partial retry retirement");readback.Check();}
        need(provider.Record(pair,lab.command,unused)==Status::Rejected,"Partial retry duplicate accepted");
    }
    // Both APIs share histories and must exclude one another while work lives.
    pair=lab.Inputs(128,160,++epoch,Mode::Dlaa,true);
    need(provider.Submit(pair)==Status::Pending,"Record-to-Submit transition failed");
    need(provider.Record(pair,lab.command,unused)==Status::Busy,"Record accepted internal in-flight work");
    output=wait(provider);need(provider.Record(pair,lab.command,unused)==Status::Busy,"Record overwrote internally published output");
    lab.CheckPixels(output);submittedEyes+=2;need(provider.Release(lab.done),"Mixed API reader release failed");
    for(auto& eye:pair.eyes)for(auto* i:{&eye.color,&eye.depth,&eye.motion})i->identity.frame=2;
    pair.reset=false;lab.Begin();need(provider.Record(pair,lab.command,output)==Status::Recorded,"Submit-to-Record transition failed");
    {ReadbackPair readback(lab);readback.Record(output);lab.Finish();submittedEyes+=2;need(provider.CommitRecorded(output.ticket,lab.done),"Mixed API retirement");readback.Check();}
    need(provider.Record(pair,lab.command,unused)==Status::Rejected,"Mixed API duplicate accepted");
    PairInput off;need(provider.Record(off,VK_NULL_HANDLE,unused)==Status::Disabled&&!unused.ticket,"Off recorded work");
    need(submittedEyes==82,"Unexpected submitted/verified eye count");
    std::cout<<"PASS 82 verified eye outputs using caller-owned submission and mixed API transitions, native-size and SR, stereo arrays, resize, cancellation and partial-failure retirement. No game/VR/FPS claim.\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
