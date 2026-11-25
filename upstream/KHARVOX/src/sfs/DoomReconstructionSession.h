#pragma once
#include "DoomTemporalBridge.h"
#include "DoomFinalBridge.h"
#include "DoomTemporalParameters.h"
#include "../dlss/VulkanReconstruction.h"
#include "../overlay/DoomLayerControl.h"
#include "../dlss/VulkanFrameTimer.h"
#include <dlss_foveado/Experimental.h>
#include <chrono>

namespace kharvox::sfs {
// DOOM records the guides/scene before acquire, then the final draw after acquire,
// and submits both primaries together. GPU completion is not a recording
// prerequisite: the consumer barrier and validated same-queue submission order
// establish that dependency. Guide storage remains owned until real retirement.
class DoomReconstructionSession {
    dlss::Options options;dlss::VulkanReconstruction provider;
    dlss::VulkanFrameTimer timing;
    dlss_foveado::experimental::Controller controller;
    dlss_foveado::experimental::Settings dynamic;
    unsigned previousLevel{1};bool recordedOnce{};
    dlss::ReconstructionSettings settings;
    DoomTemporalBridge::Inputs guides{};DoomTemporalParameters temporal{};
    std::array<XrFovf,2> fovs{};VkExtent2D allocation{};
    VkCommandBuffer guideCommand{},owner{};uint64_t guideFrame{},guideTime{},ticket{},lastFrame{},lastTime{};
    VkQueue submittedQueue{};bool submitted{},idle{},reset{true};
    bool guideSubmitted{},guideCompleted{};
    VkCommandBuffer dependencyProducer{};
    bool dependencyQueued{},dependencyInvalid{},guideWrongQueue{};
    dlss::Output recordedOutput;
    bool retire(){if(!ticket||!submitted||!idle)return false;if(!provider.RetireRecordedAfterCompletion(ticket))throw std::runtime_error("Reconstruction retirement mismatch");ticket=0;owner={};submitted=idle=false;
        const auto sequence=timing.completed();const auto now=GetTickCount64();if(sequence&&settings.dynamic&&timing.intervalMs>0)controller.Update({sequence,1,now,timing.intervalMs},1,now,false);return true;}
public:
    explicit DoomReconstructionSession(dlss::Options value):options(value),provider(value,value.storageImageExtendedFormatsEnabled),timing(value){
        configure(overlay::current());
    }
    void configure(const overlay::Selection& selected){
        if(ticket)throw std::runtime_error("Configuration requires retired reconstruction commands");
        const auto parsed=dlss_foveado::Config::Parse(selected.foveation);
        if(!parsed)throw std::runtime_error("Invalid common foveation configuration");
        ++settings.revision;gap();
        settings.foveation=*parsed;settings.neural=selected.mode==4;settings.neuralModel=selected.neuralModel;settings.neuralPercent=selected.neuralPercent;settings.neuralBefore=selected.neuralBefore!=0;
        settings.appearance={selected.style,selected.intensity/100.f,selected.structure/100.f,selected.tone/100.f,selected.skinAuto?-1.f:selected.skin/100.f,selected.autoMask!=0};
        settings.detail=selected.detail/100.f;settings.color=selected.color/100.f;settings.neuralSharpness=selected.sharpness/100.f;settings.sharpen=selected.sharpen/100.f;
        settings.dynamic=selected.foveationMode==2;
        if(settings.dynamic){dynamic.mode=dlss_foveado::experimental::Mode::Dynamic;dynamic.inner=selected.inner;dynamic.outer=selected.outer;dynamic.targetMicros=selected.targetMicros;dynamic.sustainedOverloadMs=selected.overloadMs;dynamic.recoveryDelayMs=selected.recoveryMs;controller.Configure(dynamic,GetTickCount64());controller.Stabilize(GetTickCount64());}
    }
    VkCommandBuffer pendingCommand()const{return owner;}
    VkCommandBuffer guideProducer()const{return guideCommand;}
    bool pendingGuides()const{return guideFrame&&guideCommand&&!guideCompleted;}
    uint32_t family()const{return options.queueFamily;}
    uint64_t evaluations()const{return provider.Evaluations();}
    uint64_t neuralEvaluations()const{return provider.NeuralEvaluations();}
    uint64_t resetEvaluations()const{return provider.ResetEvaluations();}
    const std::string& error()const{return provider.Error();}
    const dlss::Output& output()const{return recordedOutput;}
    double gpuIntervalMs()const{return timing.intervalMs;}
    double processingMs()const{return timing.processingMs;}
    const dlss::ReconstructionSettings& selection()const{return settings;}
    const char* dynamicStatus()const{return settings.dynamic?dlss_foveado::experimental::Name(controller.State()):"Fixed";}
    double changeCpuMs()const{return controller.ChangeCpuMs();}
    double targetMs()const{return controller.EffectiveTargetMs();}
    void observeSubmission(VkQueue queue,VkCommandBuffer command,VkResult result){
        if(command==guideCommand&&result==VK_SUCCESS){guideSubmitted=queue==options.queue;guideWrongQueue=!guideSubmitted;guideCompleted=false;}
        if(command==dependencyProducer&&result==VK_SUCCESS){dependencyQueued=queue==options.queue;dependencyInvalid|=!dependencyQueued;}
        if(command==owner&&result==VK_SUCCESS){submitted=true;submittedQueue=queue;idle=false;if(queue!=options.queue)reset=true;}}
    // Called BEFORE forwarding vkQueueSubmit/2, under the host queue lock.
    // A missing/reversed/cancelled producer must never reach the GPU. Merely
    // sharing a CPU interval or queue family is not evidence of ordering.
    bool validateSubmission(VkQueue queue,const std::vector<VkCommandBuffer>& commands)const{
        if(!ticket)return true;
        bool ready=dependencyQueued;
        for(auto command:commands){
            if(command==dependencyProducer)ready=queue==options.queue&&!dependencyInvalid;
            if(command==owner&&(!ready||dependencyInvalid||queue!=options.queue))return false;
        }
        return true;
    }
    uint64_t completionToken(VkQueue queue)const{return submitted&&submittedQueue==queue?ticket:0;}
    bool completed(uint64_t token){if(!token||token!=ticket||!submitted)return false;idle=true;if(guideSubmitted)guideCompleted=true;return retire();}
    bool retiredAfterIdle(){if(guideSubmitted)guideCompleted=true;if(!ticket||!submitted)return false;idle=true;return retire();}
    void discarded(VkCommandBuffer command){
        if(ticket&&command==dependencyProducer&&!dependencyQueued)dependencyInvalid=true;
        if(command==guideCommand){guideCommand={};if(!guideCompleted)guideFrame=0;}
        if(command!=owner||!ticket)return;
        if(submitted){retire();return;}
        if(!provider.CancelRecorded(ticket))throw std::runtime_error("Reconstruction discard mismatch");timing.discarded();ticket=0;owner={};reset=true;
    }
    void gap(){reset=true;guideFrame=0;guideCommand={};guideSubmitted=guideCompleted=guideWrongQueue=false;}
    bool releaseResources(){if(ticket||pendingGuides()||!provider.ReleaseResources())return false;recordedOutput={};lastTime=lastFrame=0;timing.discarded();gap();return true;}
    bool capture(VkCommandBuffer cb,const DoomTemporalBridge::Inputs& input,const DoomTemporalParameters& parameters,uint64_t frame,uint64_t time,VkExtent2D allocated,const std::array<XrView,2>& views,XrDuration displayPeriod=0){
        controller.FramePeriod(double(displayPeriod)/1e6);
        if(ticket||pendingGuides())return false;
        if(!frame||frame<=lastFrame){gap();return false;}
        const auto ratio=overlay::current().scaleBps/10000.;
        if(std::abs(double(input.extent.width)-allocated.width*ratio)>4||std::abs(double(input.extent.height)-allocated.height*ratio)>4){gap();return false;}
        guides=input;temporal=parameters;guideCommand=cb;guideFrame=frame;guideTime=time;allocation=allocated;guideSubmitted=guideCompleted=guideWrongQueue=false;
        for(unsigned e=0;e<2;++e)fovs[e]=views[e].fov;
        return true;
    }
    bool hasGuides(VkCommandBuffer cb,uint64_t frame,VkCommandBuffer sceneProducer=VK_NULL_HANDLE)const{
        return !ticket&&!guideWrongQueue&&frame==guideFrame&&frame>lastFrame&&
            (cb==guideCommand||guideCompleted||(sceneProducer&&sceneProducer==guideCommand));
    }
    uint64_t sourceFrame()const{return guideFrame;}
    VkExtent2D inputExtent()const{return guides.extent;}
    VkExtent2D sourceAllocation()const{return allocation;}
    std::array<VkImageView,2> record(VkCommandBuffer cb,const DoomFinalBridge::Inputs& scene,VkExtent2D target,uint64_t frame,VkCommandBuffer sceneProducer=VK_NULL_HANDLE){
        recordedOutput={};
        if(!hasGuides(cb,frame,sceneProducer)||scene.extent.width!=guides.extent.width||scene.extent.height!=guides.extent.height){gap();return {};}
        dlss::PairInput input;input.mode=overlay::current().quality==0?dlss::Mode::Quality:overlay::current().quality==1?dlss::Mode::Balanced:dlss::Mode::Performance;
        input.preset=overlay::current().preset;
        input.hdr=false;input.temporalContractValidated=true;input.reset=reset||!temporal.previousMatches||lastFrame+1!=frame||!lastTime||guideTime<=lastTime||guideTime-lastTime>250000000;
        const float delta=lastTime&&guideTime>lastTime?float(double(guideTime-lastTime)/1000000.):16.667f;
        const VkFormat formats[]{VK_FORMAT_R32G32_SFLOAT,VK_FORMAT_R32_SFLOAT};
        double minX=1e6,maxX=-1e6,minY=1e6,maxY=-1e6;
        for(const auto& f:fovs){minX=std::min(minX,std::tan(double(f.angleLeft)));maxX=std::max(maxX,std::tan(double(f.angleRight)));minY=std::min(minY,-std::tan(double(f.angleUp)));maxY=std::max(maxY,-std::tan(double(f.angleDown)));}
        const double diameter=std::max(maxX-minX,maxY-minY);
        if(!std::isfinite(diameter)||diameter<=1e-5){gap();return {};}
        const auto oldGeometry=settings.geometry;
        for(unsigned e=0;e<2;++e){auto& eye=input.eyes[e];dlss::Image* images[]{&eye.motion,&eye.depth};
            for(unsigned n=0;n<2;++n){auto& image=*images[n];image.device=options.device;image.image=guides.images[n];image.view=guides.eyeViews[n][e];image.format=formats[n];image.extent=guides.extent;image.range.baseArrayLayer=e;image.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;image.identity={frame,1,e};}
            eye.color=eye.depth;eye.color.image=scene.images[0];eye.color.view=scene.eyeViews[0][e];eye.color.format=VK_FORMAT_R16G16B16A16_SFLOAT;
            eye.jitterX=temporal.jitter.pixels[e][0];eye.jitterY=temporal.jitter.pixels[e][1];eye.frameTimeMs=delta;input.output[e]=target;
            auto& v=settings.geometry[e];v.input={guides.extent.width,guides.extent.height};v.output={target.width,target.height};v.viewId=e+1;v.nasalSide=e?dlss_foveado::NasalSide::Left:dlss_foveado::NasalSide::Right;
            const auto& f=fovs[e];const double l=std::tan(double(f.angleLeft)),r=std::tan(double(f.angleRight)),t=-std::tan(double(f.angleUp)),b=-std::tan(double(f.angleDown));
            v.binocular={{(r-l)/(diameter*target.width),0,l/diameter+.5-(minX+maxX)*.5/diameter,0,(b-t)/(diameter*target.height),t/diameter+.5-(minY+maxY)*.5/diameter,0,0,1},1,1};
        }
        if(oldGeometry!=settings.geometry){++settings.revision;input.reset=true;}
        if(settings.dynamic){const auto cuts=controller.Current();dlss_foveado::experimental::Write(settings.foveation.central,cuts);settings.processingLevel=2;settings.processingCuts=dynamic.outer;
            for(unsigned level=0;level<2;++level){std::array<unsigned,4> prepared{};bool contains=true;for(unsigned i=0;i<4;++i){prepared[i]=level?(dynamic.inner[i]+dynamic.outer[i])/2:dynamic.inner[i];contains&=prepared[i]<=cuts[i];}if(contains){settings.processingLevel=level;settings.processingCuts=prepared;break;}}
        }
        const auto start=std::chrono::steady_clock::now();timing.begin(cb);
        dlss::Output output;const auto result=provider.Record(input,settings,cb,output);timing.end(cb);
        if(settings.dynamic&&recordedOnce&&previousLevel!=settings.processingLevel)controller.ReportChangeCpuMs(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());previousLevel=settings.processingLevel;recordedOnce=true;
        if(output.ticket){owner=cb;ticket=output.ticket;submitted=idle=false;dependencyProducer=guideCommand;dependencyQueued=guideSubmitted||guideCompleted||cb==guideCommand;dependencyInvalid=false;}
        if(result!=dlss::Status::Recorded){gap();return {};}
        recordedOutput=output;lastFrame=frame;lastTime=guideTime;reset=false;return {output.eyes[0].view,output.eyes[1].view};
    }
};
}
