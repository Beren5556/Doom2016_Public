#pragma once
#include "Foveation.h"
#include <algorithm>
#include <cmath>

namespace dlss_foveado::experimental {
// Explicit opt-in extension. Base API4/config3/control2 retain their wire units.
inline constexpr uint32_t Version=1;
enum class Mode : uint32_t { Disabled, Fixed, Dynamic };
using Cuts=std::array<uint32_t,4>; // T/B/R/L, basis points of full diameter.
inline Cuts Read(const Region& r){return {r.topBps,r.bottomBps,r.rightBps,r.leftBps};}
inline void Write(Region& r,const Cuts& c){r.topBps=c[0];r.bottomBps=c[1];r.rightBps=c[2];r.leftBps=c[3];}
struct Settings {
    Mode mode{Mode::Disabled};
    Cuts inner{3000,3000,3000,3000},outer{1000,1000,1000,1000};
    uint32_t targetMicros{11100};
    uint32_t sustainedOverloadMs{1000};
    uint32_t recoveryDelayMs{1000};
    bool centralDlaa{};
    bool operator==(const Settings&)const=default;
    bool Valid()const noexcept {
        if(uint32_t(mode)>2||targetMicros<5000||targetMicros>50000||sustainedOverloadMs<100||sustainedOverloadMs>10000||recoveryDelayMs<100||recoveryDelayMs>10000)return false;
        for(unsigned i=0;i<4;++i)if(inner[i]>4500||outer[i]>inner[i]||inner[i]%100||outer[i]%100)return false;
        return true;
    }
};
enum class Status { Stabilizing, Waiting, Stable, Contracting, Recovering, Minimum, Maximum, TargetUnmet, Stale, NeuralBlocked, ReconfigurationPaused, ConfirmingOverload };
inline const char* Name(Status s){
    switch(s){case Status::Stabilizing:return "Stabilizing renderer";case Status::Stable:return "Stable";case Status::Contracting:return "Adjusting: contraction";
    case Status::Recovering:return "Adjusting: recovery";case Status::Minimum:return "Minimum reached";
    case Status::Maximum:return "Maximum reached";case Status::TargetUnmet:return "Minimum reached: target unmet";
    case Status::Stale:return "Waiting for fresh GPU timing";case Status::NeuralBlocked:return "Dynamic unavailable with Neural";
    case Status::ReconfigurationPaused:return "Dynamic paused: region change exceeded the time budget. Apply to retry.";
    case Status::ConfirmingOverload:return "Waiting for sustained overload";
    default:return "Waiting for GPU timing";}
}
struct Sample {uint64_t sequence{},generation{},observedMs{};double gpuMs{};};
// One render-thread owner. No allocations, wall-time estimates or synchronous GPU waits.
class Controller {
    Settings settings{};
    double position{.5},destination{.5},average{};
    uint64_t lastMotion{},stableAfter{};
    uint64_t lastSequence{},lastChange{},lastSample{},generation{};
    unsigned count{};
    Status status{Status::Waiting};
    double changeMs{},framePeriodMs{};bool paused{};
    uint64_t recoverySince{};bool recoveryPending{};
    double recoveryTotal{};uint32_t recoverySamples{};
    uint64_t overloadSince{};bool overloadPending{};
    uint64_t decisionSince{};double decisionTotal{},decisionMs{};unsigned decisionSamples{};
    void ResetOverload(){overloadPending=false;decisionSamples=0;decisionTotal=0;decisionMs=0;}
    void ResetRecovery(){recoveryPending=false;recoverySince=0;recoveryTotal=0;recoverySamples=0;}
public:
    void Configure(Settings value,uint64_t now){settings=value;position=destination=.5;lastMotion=now;average=0;lastSequence=lastSample=0;lastChange=now;count=0;generation=0;status=Status::Waiting;paused=false;changeMs=0;overloadSince=0;ResetOverload();ResetRecovery();}
    void FramePeriod(double ms){framePeriodMs=std::isfinite(ms)&&ms>=4&&ms<=50?ms:0;}
    double FramePeriodMs()const noexcept{return framePeriodMs;}
    void Stabilize(uint64_t now){stableAfter=now+2000;average=0;count=0;ResetOverload();ResetRecovery();lastMotion=now;status=Status::Stabilizing;}
    double EffectiveTargetMs()const noexcept{return framePeriodMs>0?framePeriodMs:settings.targetMicros/1000.;}
    // Feed CPU submission cost only for an automatic dimensional change, never
    // initial feature preparation. No repeated recreation after an observed stall.
    void ReportChangeCpuMs(double ms){
        if(!std::isfinite(ms)||ms<0)return;changeMs=ms;
        if(ms>EffectiveTargetMs()){paused=true;status=Status::ReconfigurationPaused;}
    }
    Cuts Current()const noexcept {
        Cuts cuts{};for(unsigned i=0;i<4;++i){const double v=settings.inner[i]+(double(settings.outer[i])-settings.inner[i])*position;
            cuts[i]=std::clamp(uint32_t(std::lround(v/100))*100,settings.outer[i],settings.inner[i]);}return cuts;
    }
    bool Update(const Sample& sample,uint64_t expectedGeneration,uint64_t now,bool neural=false){
        if(now<stableAfter||sample.observedMs<stableAfter){status=Status::Stabilizing;ResetOverload();ResetRecovery();lastMotion=now;return false;}
        if(neural){ResetOverload();ResetRecovery();status=Status::NeuralBlocked;return false;}
        if(paused){status=Status::ReconfigurationPaused;return false;}
        if(settings.mode!=Mode::Dynamic||!settings.Valid())return false;
        if(!sample.sequence||sample.sequence<=lastSequence||sample.generation!=expectedGeneration||sample.observedMs>now||(lastSample&&sample.observedMs<lastSample)||
           now-sample.observedMs>300||!std::isfinite(sample.gpuMs)||sample.gpuMs<=0||sample.gpuMs>250){
            if(!lastSample||now-lastSample>300){ResetOverload();ResetRecovery();status=Status::Stale;}
            if(sample.generation!=expectedGeneration||sample.observedMs>now||(lastSample&&sample.observedMs<lastSample)||!std::isfinite(sample.gpuMs)||sample.gpuMs<=0||sample.gpuMs>250){ResetOverload();ResetRecovery();}
            return false;
        }
        if(generation!=expectedGeneration||(lastSample&&now-lastSample>300)){ResetOverload();ResetRecovery();}
        lastSequence=sample.sequence;lastSample=sample.observedMs;
        if(generation!=expectedGeneration){generation=expectedGeneration;count=0;average=sample.gpuMs;}
        average=count?average*.8+sample.gpuMs*.2:sample.gpuMs;++count;
        const double target=EffectiveTargetMs(),tolerance=std::max(.35,target*.04);
        const bool paced=framePeriodMs>0;
        const bool rawOverloaded=sample.gpuMs>target+tolerance;
        bool decisionReady=!paced;
        if(paced){
            // A fast interval can follow a missed VR deadline. Judge consecutive
            // short windows, not an uninterrupted run of slow individual pairs.
            // One brief dip still cannot satisfy the configured sustained hold.
            if(!decisionSamples)decisionSince=sample.observedMs;
            decisionTotal+=sample.gpuMs;++decisionSamples;
            if(sample.observedMs-decisionSince>=200){
                decisionReady=true;decisionMs=decisionTotal/decisionSamples;
                if(decisionMs<=target+tolerance)overloadPending=false;
                else if(!overloadPending){overloadSince=decisionSince;overloadPending=true;}
                decisionTotal=0;decisionSamples=0;
            }
        }else{
            if(!rawOverloaded)overloadPending=false;
            else if(!overloadPending){overloadSince=sample.observedMs;overloadPending=true;}
        }
        const auto elapsed=std::min<uint64_t>(100,now>=lastMotion?now-lastMotion:0);lastMotion=now;
        const auto move=[&](){
            const auto before=Current();const bool contraction=destination<position;
            const double step=.5*double(elapsed)/300.;
            position=contraction?std::max(destination,position-step):std::min(destination,position+step);
            status=contraction?Status::Contracting:Status::Recovering;return Current()!=before;
        };
        if(position!=destination)return move();
        if(count<6){status=Status::Waiting;return false;}
        const bool shrink=paced?decisionReady&&overloadPending:rawOverloaded&&average>target+tolerance;
        // Paced intervals alternate around the runtime period because they include
        // queue waits. Requiring EVERY sample to stay below a threshold starves
        // recovery despite a healthy mean. Observe a complete configured recovery window
        // instead. A current overload still vetoes growth; an unhealthy window
        // starts over. This remains a bounded quality probe, not GPU busy time.
        bool grow=!rawOverloaded&&average<target-tolerance;
        if(paced){
            if(!recoveryPending){ResetRecovery();recoveryPending=true;recoverySince=sample.observedMs;}
            recoveryTotal+=sample.gpuMs;++recoverySamples;grow=false;
            if(sample.observedMs-recoverySince>=settings.recoveryDelayMs){
                const bool healthy=recoveryTotal/recoverySamples<=target+tolerance*.5;
                grow=healthy&&!rawOverloaded&&average<=target+tolerance;
                // Allow a healthy window a few fresh frames to clear a single
                // deadline miss, but never retain old evidence indefinitely.
                if(!healthy||(!grow&&sample.observedMs-recoverySince>=uint64_t(settings.recoveryDelayMs)+300))ResetRecovery();
            }
        }
        if(!shrink&&!grow){status=Status::Stable;return false;}
        if(shrink&&sample.observedMs-overloadSince<settings.sustainedOverloadMs){status=Status::ConfirmingOverload;return false;}
        if(shrink&&position<=0){status=Status::TargetUnmet;return false;}
        if(grow&&position>=1){ResetRecovery();status=Status::Maximum;return false;}
        if(now-lastChange<std::min(shrink?750u:1500u,shrink?settings.sustainedOverloadMs:settings.recoveryDelayMs)){status=Status::Stable;return false;}
        // Three prepared processing levels; interpolate only the visible mask.
        // Processing always selects a cached crop that contains that mask.
        destination=std::clamp(position+(shrink?-.5:.5),0.,1.);
        if(shrink)ResetOverload();ResetRecovery();
        status=shrink?Status::Contracting:Status::Recovering;lastChange=now;
        return move();
    }
    Status State()const noexcept{return status;}
    double MeasuredMs()const noexcept{return average;}
    double ChangeCpuMs()const noexcept{return changeMs;}
};
} // namespace dlss_foveado::experimental
