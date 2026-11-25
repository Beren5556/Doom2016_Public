#pragma once
#include "TemporalContext.h"
#include <array>
#include <cstring>

namespace kharvox::temporal_context {
// Fixed-size, read-only observation of the fingerprinted DOOM functions. Raw
// IEEE words preserve NaNs and signed zero; failed reads are null, never zeros
// masquerading as engine values. These CPU matrices are not shader evidence.
struct ProjectionObservation {
    std::shared_ptr<Capture> capture;
    uint64_t view{},model{},caller{},present{},altScale{};
    uint32_t readable{},flags{},billboard{};
    bool jitter{},selectedModel{},selectedView{};
    std::array<uint32_t,3> arguments{};
    std::array<uint32_t,4> rectangle{};
    std::array<uint8_t,6> selectors{};
    std::array<std::array<uint32_t,16>,6> matrices{};
    template<class Read> void readMatrices(Read&& read,uintptr_t p,bool copied){
        const size_t offsets[]={0x1130,0x1170,0x11f0,0x1270};
        for(unsigned n=0;n<4;++n){if(copied&&n>=2)continue;if(p&&read(p+offsets[n],matrices[n].data(),64))readable|=1u<<n;}
        // The model function's alternate branch and the view builder pass
        // different structure bases to the jitter helper. Keep both raw pairs.
        const size_t bytes[]={0x831,0x832,0x1111,0x1112,0x8ee,0x8ef};
        for(unsigned n=0;n<6;++n)if(p&&read(p+bytes[n],&selectors[n],1))readable|=1u<<(8+n);
    }
    template<class Read> static ProjectionObservation modelBefore(bool guarded,Read&& read,uintptr_t model,uintptr_t selected,uintptr_t view,uintptr_t selectedView,float x,float y,float z,bool jitter,uint64_t present,uint64_t caller)noexcept{
        ProjectionObservation o;if(!guarded)return o;o.capture=current();if(!o.capture||!o.capture->sample("modelMatrix",512)){o.capture.reset();return o;}
        o.model=model;o.view=view;o.selectedModel=model!=selected;o.selectedView=view!=selectedView;o.jitter=jitter;o.present=present;o.caller=caller;
        const float xyz[]={x,y,z};std::memcpy(o.arguments.data(),xyz,12);
        o.readMatrices(read,selectedView,o.selectedView);
        if(selected&&read(selected+0xb24,o.matrices[4].data(),64))o.readable|=1u<<4;
        uint8_t f{},b{};if(selected&&read(selected+0xba,&f,1)){o.flags=f;o.readable|=1u<<16;}if(selected&&read(selected+0xb9,&b,1)){o.billboard=b;o.readable|=1u<<17;}
        return o;
    }
    template<class Read> void modelAfter(Read&& read,uintptr_t output)noexcept{
        if(!capture)return;if(output&&read(output,matrices[5].data(),64))readable|=1u<<5;emit("modelMatrix");
    }
    template<class Read> static void viewAfter(bool guarded,Read&& read,uintptr_t view,uintptr_t rectangle,uint64_t altScale,uint64_t present,uint64_t caller)noexcept{
        if(!guarded)return;ProjectionObservation o;o.capture=current();if(!o.capture||!o.capture->sample("viewMatrices",64))return;o.view=view;o.altScale=altScale;o.present=present;o.caller=caller;o.readMatrices(read,view,false);
        if(rectangle&&read(rectangle,o.rectangle.data(),16))o.readable|=1u<<18;o.emit("viewMatrices");
    }
    void emit(const char* kind)noexcept{capture->protect([&]{
        std::ostringstream o;o<<"{\"fingerprint\":\"doom-2016-3d8b4393\",\"view\":"<<view<<",\"model\":"<<model<<",\"caller\":"<<caller<<",\"presentSerial\":"<<present<<",\"altScale\":"<<altScale<<",\"readableMask\":"<<readable<<",\"flags\":"<<flags<<",\"billboard\":"<<billboard<<",\"jitterArgument\":"<<(jitter?"true":"false")<<",\"selectedModelCopy\":"<<(selectedModel?"true":"false")<<",\"selectedViewCopy\":"<<(selectedView?"true":"false")<<",\"xyzBits\":"<<numbers(arguments.data(),3)<<",\"rectangleBits\":"<<((readable&(1u<<18))?numbers(rectangle.data(),4):"null")<<",\"selectors\":[";
        for(unsigned n=0;n<6;++n)o<<(n?",":"")<<((readable&(1u<<(8+n)))?std::to_string(selectors[n]):"null");o<<"],\"matricesBits\":[";
        for(unsigned n=0;n<6;++n)o<<(n?",":"")<<((readable&(1u<<n))?numbers(matrices[n].data(),16):"null");o<<"]}";capture->event(kind,o.str());
    });}
};
}
