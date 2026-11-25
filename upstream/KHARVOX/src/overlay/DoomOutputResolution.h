#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace kharvox::overlay {
struct OutputSize { uint32_t width{},height{}; bool operator==(const OutputSize& b)const{return width==b.width&&height==b.height;} };
struct OutputPlan { float scale{}; OutputSize source; std::array<OutputSize,2> eyes; };
struct OutputDescription {
    std::array<OutputSize,2> recommended{},maximum{},selected{};
    float scale{};bool writable{};
    std::optional<OutputPlan> plan(float value)const {
        if(!std::isfinite(value)||value<.5f||value>3.f)return {};
        OutputPlan result;result.scale=value;
        uint32_t w{},h{};
        for(unsigned e=0;e<2;++e){
            const auto base=recommended[e],limit=maximum[e];if(!base.width||!base.height)return {};
            const double x=std::round(double(base.width)*value),y=std::round(double(base.height)*value);
            if(x<1||y<1||x>limit.width||y>limit.height||x>8192||y>8192||x*y>32.*1024*1024)return {};
            result.eyes[e]={uint32_t(x),uint32_t(y)};
            w=(std::max)(w,base.width);h=(std::max)(h,base.height);
        }
        const double x=std::ceil(double(w)*value/2)*2,y=std::ceil(double(h)*value/2)*2;
        if(x>8192||y>8192||x*y>32.*1024*1024)return {};
        result.source={uint32_t(x),uint32_t(y)};return result;
    }
    uint32_t maximumWidth()const {
        // Keep the returned interval inside every eye's runtime and allocator
        // limits, including the engine's even-dimension rounding.
        float lo=.5f,hi=3.f;
        if(!plan(lo))return 0;
        for(unsigned n=0;n<28;++n){float mid=(lo+hi)/2;if(plan(mid))lo=mid;else hi=mid;}
        return uint32_t(std::floor(double(recommended[0].width)*lo));
    }
};
// One output transaction spans three independent receipts: engine images,
// replacement XR images, then an actually submitted frame. UI acknowledgement
// must never be inferred from a saved number or a successful allocation alone.
class OutputResize {
public:
    enum class Phase { Idle,Queued,Engine,XR,Frame,Accepted,Rejected };
    struct Snapshot {Phase phase{};uint64_t revision{},started{};uintptr_t chain{};OutputPlan plan;std::string error;};
private:
    mutable std::mutex mutex;
    OutputDescription description;
    Snapshot value;
public:
    OutputDescription describe()const{std::lock_guard<std::mutex> lock(mutex);return description;}
    void describe(const OutputDescription& next){std::lock_guard<std::mutex> lock(mutex);description=next;}
    Snapshot snapshot()const{std::lock_guard<std::mutex> lock(mutex);return value;}
    bool request(uint64_t revision,float scale,uint64_t now){
        std::lock_guard<std::mutex> lock(mutex);
        if(!description.writable||!revision||revision<=value.revision||
            (value.phase!=Phase::Idle&&value.phase!=Phase::Accepted&&value.phase!=Phase::Rejected))return false;
        auto p=description.plan(scale);if(!p)return false;
        value={Phase::Queued,revision,now,0,*p,{}};return true;
    }
    bool prepared(uint64_t revision){std::lock_guard<std::mutex> lock(mutex);if(value.revision!=revision||value.phase!=Phase::Queued)return false;value.phase=Phase::Engine;return true;}
    bool engineCreated(OutputSize size,uintptr_t chain){std::lock_guard<std::mutex> lock(mutex);if(value.phase!=Phase::Engine||!(size==value.plan.source)||!chain)return false;value.chain=chain;value.phase=Phase::XR;return true;}
    bool xrCommitted(uint64_t revision){std::lock_guard<std::mutex> lock(mutex);if(value.revision!=revision||value.phase!=Phase::XR)return false;description.selected=value.plan.eyes;description.scale=value.plan.scale;value.phase=Phase::Frame;return true;}
    bool presented(uintptr_t chain,const std::array<OutputSize,2>& source){
        std::lock_guard<std::mutex> lock(mutex);
        if(value.phase!=Phase::Frame||value.chain!=chain||!(source[0]==value.plan.source)||!(source[1]==value.plan.source))return false;
        // This receipt proves engine/XR geometry, independently of the active
        // reconstruction provider. A TAA fallback must not lock every control.
        value.phase=Phase::Accepted;return true;
    }
    void reject(uint64_t revision,std::string error){std::lock_guard<std::mutex> lock(mutex);if(value.revision==revision){value.phase=Phase::Rejected;value.error=std::move(error);}}
    void disable(){std::lock_guard<std::mutex> lock(mutex);description.writable=false;}
};
inline OutputResize outputResize;
}
