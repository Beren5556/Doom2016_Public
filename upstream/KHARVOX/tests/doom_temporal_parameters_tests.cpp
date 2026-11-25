#include "../src/sfs/DoomTemporalFailure.h"
#include "DoomEngineJitterFixture.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace kharvox::sfs;
static void need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class T>void read(std::ifstream& input,T& value){need(bool(input.read(reinterpret_cast<char*>(&value),sizeof(value))),"Truncated temporal fixture");}
int main(int argc,char** argv)try{
    need(argc==2||argc==3,"Expected associated temporal replay and optional live scaled replay");std::ifstream input(argv[1],std::ios::binary);uint32_t magic{},count{};read(input,magic);read(input,count);need(magic==0x44545031&&count>1&&count<=128,"Bad temporal replay header");
    kharvox::doom_temporal::ViewSnapshot previous;unsigned outsideHalf=0;
    for(unsigned n=0;n<count;++n){kharvox::doom_temporal::ViewSnapshot view;uint32_t phase{};DoomTemporalUniformSnapshot u;FrameUniforms eyes;
        read(input,view.source);read(input,view.present);read(input,phase);need(phase<8,"Bad captured phase");view.phase=uint8_t(phase);read(input,view.rectangle);read(input,view.jittered);read(input,view.unjittered);read(input,view.view);read(input,u.high);read(input,u.low);read(input,eyes);
        const VkExtent2D extent{uint32_t(u.high[0]),uint32_t(u.high[1])};const std::array<float,6> viewport{0,0,u.high[0],u.high[1],0,1};
        const auto p=DoomTemporalParameters::derive(view,n?&previous:nullptr,u,eyes,extent,viewport);
        const char* rejection="unset";
        need(DoomTemporalParameters::currentMatches(view,u,extent,{},&rejection)&&!rejection,"Valid parameters reported a rejection");
        auto invalidScale=u;invalidScale.low[12]=.25f;
        need(!DoomTemporalParameters::currentMatches(view,invalidScale,extent,{},&rejection)&&std::string(rejection)=="active-allocation-extent","Missing precise scale rejection reason");
        auto invalidPhase=view;invalidPhase.phase=(phase+1)%8;
        need(!DoomTemporalParameters::currentMatches(invalidPhase,u,extent,{},&rejection)&&std::string(rejection)=="raster-jitter-phase","Missing precise jitter rejection reason");
        auto diagnostic=temporalFailureJson(view.present,extent,extent,invalidScale,eyes,{view});
        need(diagnostic.find("active-allocation-extent")!=std::string::npos&&diagnostic.find("\"matched\":false")!=std::string::npos,"Failure evidence omitted the bound data/rejection");
        if(!p)std::cerr<<"Record "<<n<<" phase="<<phase<<'\n';need(bool(p),"Captured current engine projection did not match TAA parameters");
        need(p->previousMatches==(n!=0),"Captured previous projection/phase continuity mismatch");outsideHalf+=!p->jitter.withinHalfPixel;
        // All eight phases at the user's resized output and at other supported
        // sizes. Exact Halton / nominal-fraction fixtures hid the phase-7 fault.
        for(const VkExtent2D resized: {VkExtent2D{2800,2974},VkExtent2D{3250,3452},VkExtent2D{4000,4250},VkExtent2D{5618,5970}}){
            for(float fraction:{.6667f,.5f,.58f,.99f,1.f}){
                auto scaledView=view;auto scaledUniforms=u;
                doomEngineJitterFixture(scaledView,scaledUniforms,resized,fraction);
                const auto active=DoomTemporalParameters::renderExtent(scaledUniforms,resized);
                const char* why=nullptr;
                if(!active||!DoomTemporalParameters::currentMatches(scaledView,scaledUniforms,*active,resized,&why))
                    throw std::runtime_error("Integer engine jitter rejected: "+std::to_string(resized.width)+"x"+std::to_string(resized.height)+
                        " scale="+std::to_string(fraction)+" phase="+std::to_string(phase)+" reason="+(why?why:"extent"));
                auto wrong=scaledView;wrong.phase=(wrong.phase+1)%8;
                need(!DoomTemporalParameters::currentMatches(wrong,scaledUniforms,*active,resized),"Different engine phase accepted after resize");
                wrong=scaledView;wrong.jittered[2]+=.001f;
                need(!DoomTemporalParameters::currentMatches(wrong,scaledUniforms,*active,resized),"Arbitrary jitter accepted after resize");
            }
        }
        kharvox::doom_temporal::ViewSnapshots cache;
        auto observation=view;
        auto reader=[&](uintptr_t address,void* destination,size_t size){
            const void* source=nullptr;size_t expected=0;
            if(address==view.source+0x1130){source=observation.jittered.data();expected=64;}
            else if(address==view.source+0x1170){source=observation.unjittered.data();expected=64;}
            else if(address==view.source+0x11f0){source=observation.view.data();expected=64;}
            else if(address==view.source+0x1111){source=&observation.mode;expected=1;}
            else if(address==view.source+0x1112){source=&observation.phase;expected=1;}
            else if(address==1){source=observation.rectangle.data();expected=16;}
            if(!source||size!=expected)return false;std::memcpy(destination,source,size);return true;
        };
        cache.observe(reader,view.source,1,view.present);observation.view[3]+=100;
        cache.observe(reader,view.source,1,view.present+1);
        need(cache.recent().size()==2,"Camera evidence missing observations");
        kharvox::doom_temporal::ViewSnapshot selected;
        need(cache.find([&](const auto& candidate){return DoomTemporalParameters::currentMatches(candidate,u,extent);},selected)&&selected.present==view.present,"Newest unrelated camera displaced matching bound view");
        for(unsigned k=0;k<32;++k)cache.observe(reader,view.source,1,view.present+2+k);
        need(!cache.find([&](const auto& candidate){return DoomTemporalParameters::currentMatches(candidate,u,extent);},selected),"Overwritten camera snapshot remained eligible");
        need(cache.recent().size()==32,"Camera evidence exceeded bounded ring");
        auto bad=view;bad.phase=(phase+1)%8;need(!DoomTemporalParameters::derive(bad,nullptr,u,eyes,extent,viewport),"Wrong phase accepted");
        bad=view;bad.view[3]+=100;need(!DoomTemporalParameters::derive(bad,nullptr,u,eyes,extent,viewport),"Different camera origin accepted");
        auto stale=u;stale.high[32]+=.001f;need(!DoomTemporalParameters::derive(view,nullptr,stale,eyes,extent,viewport),"Stale filter coefficients accepted");
        bad=view;bad.jittered[0]+=.01f;need(!DoomTemporalParameters::derive(bad,nullptr,u,eyes,extent,viewport),"Non-jitter projection difference accepted");
        auto wrongExtent=extent;--wrongExtent.width;need(!DoomTemporalParameters::derive(view,nullptr,u,eyes,wrongExtent,viewport),"Mismatched raster size accepted");
        if(n){auto old=previous;old.present-=1;const auto reset=DoomTemporalParameters::derive(view,&old,u,eyes,extent,viewport);need(reset&&!reset->previousMatches,"Missing frame did not invalidate temporal history");
            old=previous;old.unjittered=old.jittered;need(!DoomTemporalParameters::previousMatchesView(old,u),"Jittered previous-projection control accepted");}
        if(n==0){
            auto scaled=u;scaled.low[12]=scaled.low[13]=.6667f;scaled.low[14]=scaled.low[15]=1.f/.6667f;
            scaled.high[50]=scaled.high[2]/.6667f;scaled.high[51]=scaled.high[3]/.6667f;
            const auto active=DoomTemporalParameters::renderExtent(scaled,extent);need(bool(active),"Scaled fixture extent");
            need(DoomTemporalParameters::currentMatches(view,scaled,*active,extent),"Output-space jitter rejected under scene scaling");
            auto scene=view;scene.jittered[2]=scene.unjittered[2]+(view.jittered[2]-view.unjittered[2])/.6667f;
            scene.jittered[6]=scene.unjittered[6]+(view.jittered[6]-view.unjittered[6])/.6667f;
            need(DoomTemporalParameters::currentMatches(scene,scaled,*active,extent),"Scene-space jitter rejected");
            scene.jittered[2]+=.001f;need(!DoomTemporalParameters::currentMatches(scene,scaled,*active,extent),"Arbitrary jitter accepted");
        }
        previous=view;
    }
    need(outsideHalf==2,"Captured beyond-half-pixel jitter was silently clamped or lost");need(input.peek()==std::char_traits<char>::eof(),"Unexpected replay trailing bytes");
    if(argc==3){
        std::ifstream live(argv[2],std::ios::binary);uint32_t tag{},records{};read(live,tag);read(live,records);need(tag==0x44544631&&records>0&&records<=12,"Invalid live failure replay");
        for(unsigned n=0;n<records;++n){VkExtent2D allocation;DoomTemporalUniformSnapshot u;FrameUniforms eyes{};uint32_t candidates{};
            read(live,allocation);read(live,u.high);read(live,u.low);read(live,eyes.clip);read(live,candidates);need(candidates>0&&candidates<=32,"Invalid candidate count");
            const auto extent=DoomTemporalParameters::renderExtent(u,allocation);need(bool(extent),"Live engine fractional extent rejected");unsigned matched=0;
            for(unsigned k=0;k<candidates;++k){kharvox::doom_temporal::ViewSnapshot v;uint32_t phase{};read(live,v.source);read(live,v.present);read(live,phase);v.phase=uint8_t(phase);read(live,v.rectangle);read(live,v.jittered);read(live,v.unjittered);read(live,v.view);
                const char* why=nullptr;if(DoomTemporalParameters::currentMatches(v,u,*extent,allocation,&why)){
                    ++matched;const auto derived=DoomTemporalParameters::derive(v,nullptr,u,eyes,*extent,{0,0,float(extent->width),float(extent->height),0,1},allocation);need(bool(derived),"Matched live scaled view cannot derive jitter");
                }else if(k==0)std::cout<<"Live record "<<n<<" newest candidate rejected: "<<why<<'\n';
            }
            need(matched>0,"No live scaled engine camera matched bound uniforms");
            need(!DoomTemporalParameters::currentMatches({},u,allocation,allocation),"Full TAA viewport accepted as scaled input");
        }
        need(live.peek()==std::char_traits<char>::eof(),"Trailing scaled replay bytes");std::cout<<"PASS "<<records<<" REAL failed DOOM scaled draws now associated with engine camera/jitter\n";
    }
    std::cout<<"PASS "<<count<<" captured view/TAA pairs, previous-camera continuity, native-eye jitter, stale phase/origin/size/frame controls; two >0.5px offsets preserved\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
