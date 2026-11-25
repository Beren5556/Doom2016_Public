#define KHARVOX_DLSS_RECORD_TEST
#include "vulkan_dlss_tests.cpp"
#include "../src/dlss/VulkanReconstruction.h"
#include <map>
#include <mutex>
// Count actual device allocations made through the adapter dispatch, including
// NGX calls that use it. This is a lower bound, not total driver VRAM usage.
namespace AllocationAudit {
PFN_vkGetDeviceProcAddr base{};std::map<VkDeviceMemory,VkDeviceSize> live;std::mutex mutex;
VKAPI_ATTR VkResult VKAPI_CALL allocate(VkDevice d,const VkMemoryAllocateInfo* info,const VkAllocationCallbacks* cb,VkDeviceMemory* out){
    const auto result=reinterpret_cast<PFN_vkAllocateMemory>(base(d,"vkAllocateMemory"))(d,info,cb,out);
    if(result==VK_SUCCESS){std::lock_guard<std::mutex> lock(mutex);live[*out]=info->allocationSize;}return result;
}
VKAPI_ATTR void VKAPI_CALL free(VkDevice d,VkDeviceMemory memory,const VkAllocationCallbacks* cb){
    {std::lock_guard<std::mutex> lock(mutex);live.erase(memory);}
    reinterpret_cast<PFN_vkFreeMemory>(base(d,"vkFreeMemory"))(d,memory,cb);
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL get(VkDevice d,const char* name){
    if(!std::strcmp(name,"vkAllocateMemory"))return reinterpret_cast<PFN_vkVoidFunction>(allocate);
    if(!std::strcmp(name,"vkFreeMemory"))return reinterpret_cast<PFN_vkVoidFunction>(free);
    return base(d,name);
}
VkDeviceSize bytes(){std::lock_guard<std::mutex> lock(mutex);VkDeviceSize n{};for(const auto& p:live)n+=p.second;return n;}
}
int wmain(int argc,wchar_t** argv){try{
    need(argc==4,"Expected SR runtime, private cache and Neural DLL");
    Lab lab;lab.Initialize(argv[1],argv[2],true);lab.o.neuralLibrary=std::filesystem::absolute(argv[3]);
    AllocationAudit::base=lab.o.gdpa;auto audited=lab.o;audited.gdpa=AllocationAudit::get;
    VulkanReconstruction pipeline(audited,true);
    auto input=lab.Inputs(256,288,1,Mode::Quality,true);input.hdr=false;input.reset=false;
    ReconstructionSettings settings;settings.foveation.enabled=true;
    settings.foveation.central.topBps=1500;settings.foveation.central.bottomBps=2500;
    settings.foveation.central.leftBps=2500;settings.foveation.central.rightBps=1500;
    settings.foveation.neuralLinked=false;settings.foveation.neural.topBps=1000;
    settings.foveation.neural.bottomBps=1000;settings.foveation.neural.leftBps=1000;settings.foveation.neural.rightBps=1000;
    for(unsigned e=0;e<2;++e){auto& v=settings.geometry[e];v.input={256,288};v.output={512,576};v.viewId=e+1;v.nasalSide=e?dlss_foveado::NasalSide::Left:dlss_foveado::NasalSide::Right;v.binocular={{1./512,0,e?.05:-.05,0,1./576,0,0,0,1},1,1};}
    uint64_t frame{};
    for(unsigned variant=0;variant<11;++variant){
        settings.foveation.enabled=variant<5||variant>=7;settings.foveation.periphery=variant%2?dlss_foveado::Periphery::Temporal:dlss_foveado::Periphery::Spatial;
        settings.neural=variant>=2&&variant<8;settings.neuralBefore=variant%2!=0;settings.neuralModel=variant%3;settings.revision=variant+1;
        settings.neuralPercent=variant==7?100:85;settings.sharpen=variant>=7?.25f:0;
        settings.foveation.showRegion=variant>=7;settings.foveation.indicatorRegions=2;
        settings.dynamic=variant>=8;settings.processingLevel=variant>=8?variant-8:0;settings.processingCuts={1000,1000,1000,1000};
        for(unsigned i=0;i<3;++i){++frame;for(auto& eye:input.eyes)for(auto* image:{&eye.color,&eye.depth,&eye.motion})image->identity.frame=frame;
            lab.Begin();Output out;const auto status=pipeline.Record(input,settings,lab.command,out);if(status!=Status::Recorded){lab.Finish();if(out.ticket)pipeline.RetireRecordedAfterCompletion(out.ticket);throw std::runtime_error(pipeline.Error());}
            Output ignored;need(pipeline.Record(input,settings,lab.command,ignored)==Status::Busy,"Combined work overwritten");
            need(!pipeline.ReleaseResources(),"Unsubmitted/in-flight reconstruction resources freed");
            lab.Finish();lab.CheckPixels(out);need(pipeline.RetireRecordedAfterCompletion(out.ticket),"Combined retirement failed");
        }
        std::cout<<"PASS combined variant="<<variant<<" foveated="<<settings.foveation.enabled<<" temporalPeriphery="<<unsigned(settings.foveation.periphery)<<" neural="<<settings.neural<<" before="<<settings.neuralBefore<<" model="<<settings.neuralModel<<'\n';
    }
    // Discarding unsubmitted work must not preserve fictional image layouts.
    ++frame;for(auto& eye:input.eyes)for(auto* image:{&eye.color,&eye.depth,&eye.motion})image->identity.frame=frame;
    lab.Begin();Output cancelled;need(pipeline.Record(input,settings,lab.command,cancelled)==Status::Recorded,"Cancellation recording");
    vkCheck(lab.f.vkResetCommandPool(lab.o.device,lab.pool,0),"Discard combined commands");need(pipeline.CancelRecorded(cancelled.ticket),"Cancellation ticket");
    ++frame;for(auto& eye:input.eyes)for(auto* image:{&eye.color,&eye.depth,&eye.motion})image->identity.frame=frame;
    lab.Begin();Output resumed;need(pipeline.Record(input,settings,lab.command,resumed)==Status::Recorded,"Resume after discard");lab.Finish();lab.CheckPixels(resumed);need(pipeline.RetireRecordedAfterCompletion(resumed.ticket),"Resume retirement");
    for(unsigned cycle=0;cycle<3;++cycle){
        const auto before=AllocationAudit::bytes(),evaluations=pipeline.Evaluations();
        need(pipeline.ReleaseResources(),"Inactive reconstruction release failed");
        const auto after=AllocationAudit::bytes();
        std::cout<<"Retired route memory cycle="<<cycle<<" beforeBytes="<<before<<" afterBytes="<<after<<" releasedBytes="<<(before-after)<<'\n';
        need(before>after+VkDeviceSize(512)*576*8*2,"Inactive route retained its two full output buffers");
        need(pipeline.Evaluations()==evaluations,"Releasing inactive resources lost telemetry counters");
        ++frame;for(auto& eye:input.eyes)for(auto* image:{&eye.color,&eye.depth,&eye.motion})image->identity.frame=frame;
        lab.Begin();Output restarted;need(pipeline.Record(input,settings,lab.command,restarted)==Status::Recorded,"Reactivation after resource release");
        lab.Finish();lab.CheckPixels(restarted);need(pipeline.RetireRecordedAfterCompletion(restarted.ticket),"Reactivated retirement");
    }
    std::cout<<"PASS combined pipeline: SR="<<pipeline.Evaluations()<<" Neural="<<pipeline.NeuralEvaluations()<<" eye evaluations, eleven arrangements including dynamic banks, residual100%, indicators, sharpen, full output readback and cancellation recovery\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
