#pragma once
#include "DoomTemporalBridge.h"
#include "DoomTemporalParameters.h"
#include "../dlss/VulkanDlss.h"
#include "../overlay/DoomLayerControl.h"
#include "../dlss/VulkanFrameTimer.h"
#ifdef KHARVOX_HAVE_RECONSTRUCTION
#include "../dlss/VulkanReconstruction.h"
#endif

namespace kharvox::sfs {
struct DoomReconstruction {
    dlss::Mode mode{dlss::Mode::Dlaa};
    // Separate native output, never a request to resize the input buffers.
    // The host must qualify its engine resolution mapping before using SR.
    VkExtent2D output{};
    bool engineMappingQualified{};
};
// One outstanding GPU use per provider. The caller serializes these methods.
// Successful submission + device idle retires its GPU readers; a CPU command
// pool need not be recycled yet. The per-frame engine commands are re-recorded
// before reuse. Unsubmitted work is cancelled ONLY after command discard.
class DoomDlssSession {
    dlss::Options options;
#ifdef KHARVOX_HAVE_RECONSTRUCTION
    dlss::VulkanReconstruction provider;
#else
    dlss::VulkanDlss provider;
#endif
    dlss::VulkanFrameTimer timing;
    VkCommandBuffer owner{};uint64_t ticket{},lastTime{},lastFrame{};
    VkQueue submittedQueue{};
    bool submitted{},idle{},reset{true};
    dlss::Mode previousMode{dlss::Mode::Off};VkExtent2D previousOutput{};
    dlss::Output recordedOutput;
    bool retire(){
        if(!ticket||!submitted||!idle)return false;
        if(!provider.RetireRecordedAfterCompletion(ticket))throw std::runtime_error("DLSS retirement ticket mismatch");
        timing.completed();ticket=0;owner={};submitted=idle=false;return true;
    }
public:
    explicit DoomDlssSession(dlss::Options value):options(value),provider(value
#ifdef KHARVOX_HAVE_RECONSTRUCTION
    ,value.storageImageExtendedFormatsEnabled
#endif
    ),timing(value){}
    double gpuIntervalMs()const{return timing.intervalMs;}
    double processingMs()const{return timing.processingMs;}
    VkCommandBuffer pendingCommand()const{return owner;}
    uint32_t family()const{return options.queueFamily;}
    uint64_t evaluations()const{return provider.Evaluations();}
    uint64_t resetEvaluations()const{return provider.ResetEvaluations();}
    const std::string& error()const{return provider.Error();}
    const dlss::Output& output()const{return recordedOutput;}
    void observeSubmission(VkQueue queue,VkCommandBuffer command,VkResult result){
        if(command==owner&&result==VK_SUCCESS){submitted=true;submittedQueue=queue;idle=false;if(queue!=options.queue)reset=true;}
    }
    uint64_t completionToken(VkQueue queue)const{return submitted&&submittedQueue==queue?ticket:0;}
    bool completed(uint64_t token){
        if(!token||token!=ticket||!submitted)return false;
        idle=true;return retire();
    }
    bool retiredAfterIdle(){
        if(!ticket||!submitted)return false;idle=true;return retire();
    }
    void discarded(VkCommandBuffer command){
        if(command!=owner||!ticket)return;
        // A submitted buffer may be reset after completion before SFS reaches
        // its idle boundary. Keep its ticket for retirement, never cancel it.
        if(submitted){retire();return;}
        if(!provider.CancelRecorded(ticket))throw std::runtime_error("DLSS cancellation ticket mismatch");
        timing.discarded();ticket=0;owner={};reset=true;
    }
    void gap(){reset=true;}
    bool releaseResources(){if(ticket||!provider.ReleaseResources())return false;recordedOutput={};lastTime=lastFrame=0;timing.discarded();gap();return true;}
    std::array<VkImageView,2> record(VkCommandBuffer command,const DoomTemporalBridge::Inputs& resources,
        const DoomTemporalParameters& temporal,uint64_t frame,uint64_t timeNs,std::optional<float> preExposure,
        DoomReconstruction reconstruction={}){
        if(ticket||!frame||frame<=lastFrame||!preExposure||!std::isfinite(*preExposure)||*preExposure<=0){gap();return {};}
        auto target=reconstruction.output;
        if(reconstruction.mode==dlss::Mode::Dlaa&&!target.width&&!target.height)target=resources.extent;
        const bool same=target.width==resources.extent.width&&target.height==resources.extent.height;
        if(reconstruction.mode<dlss::Mode::Dlaa||reconstruction.mode>dlss::Mode::UltraPerformance||
           target.width<resources.extent.width||target.height<resources.extent.height||
           (reconstruction.mode==dlss::Mode::Dlaa?!same:(!reconstruction.engineMappingQualified||same))){gap();return {};}
        recordedOutput={};
        dlss::PairInput input;input.mode=reconstruction.mode;input.temporalContractValidated=true;
        if(overlay::enabled())input.preset=overlay::current().preset;
        input.depthInverted=false;input.motionJittered=false;
        input.reset=reset||previousMode!=input.mode||previousOutput.width!=target.width||previousOutput.height!=target.height||
            !temporal.previousMatches||lastFrame+1!=frame||!lastTime||timeNs<=lastTime||timeNs-lastTime>250000000;
        const float delta=lastTime&&timeNs>lastTime?float(double(timeNs-lastTime)/1000000):16.667f;
        const VkFormat formats[]{VK_FORMAT_R32G32_SFLOAT,VK_FORMAT_R32_SFLOAT,VK_FORMAT_R16G16B16A16_SFLOAT};
        for(unsigned e=0;e<2;++e){auto& eye=input.eyes[e];
            dlss::Image* images[]{&eye.motion,&eye.depth,&eye.color};
            for(unsigned n=0;n<3;++n){auto& image=*images[n];image.device=options.device;image.image=resources.images[n];image.view=resources.eyeViews[n][e];
                image.format=formats[n];image.extent=resources.extent;image.range.baseArrayLayer=e;
                image.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;image.identity={frame,1,e};}
            input.output[e]=target;eye.jitterX=temporal.jitter.pixels[e][0];eye.jitterY=temporal.jitter.pixels[e][1];
            eye.preExposure=*preExposure;eye.frameTimeMs=delta;
        }
        dlss::Output output;timing.begin(command);
#ifdef KHARVOX_HAVE_RECONSTRUCTION
        dlss::ReconstructionSettings settings;if(overlay::enabled())settings.sharpen=overlay::current().sharpen/100.f;
        const auto result=provider.Record(input,settings,command,output);
#else
        const auto result=provider.Record(input,command,output);
#endif
        timing.end(command);
        if(output.ticket){ticket=output.ticket;owner=command;submitted=idle=false;}
        if(result!=dlss::Status::Recorded){gap();return {};}
        recordedOutput=output;lastFrame=frame;lastTime=timeNs;previousMode=input.mode;previousOutput=target;reset=false;return {output.eyes[0].view,output.eyes[1].view};
    }
};
}
